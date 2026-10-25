#!/usr/bin/env node
// Checks the G-code invariants of the Print Advisor engine features (heat soak, brim_layers).
//
// Usage:
//   node tests/print-advisor/check-gcode.mjs <file.gcode> [--expected <file.json>]
//   node tests/print-advisor/check-gcode.mjs            (runs every fixture with its .expected.json)
//
// The settings are read from the CONFIG block at the end of the G-code, so the script also works
// on a full slice. The expected JSON pins values for a fixture (see fixtures/README.md).
// Exit code 0 when every check passes, 1 otherwise.

import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const MAX_DWELL_MS = 3_600_000;

function parseConfig(lines) {
  const config = {};
  const start = lines.findIndex((l) => l.startsWith('; CONFIG_BLOCK_START'));
  if (start < 0) return config;
  for (let i = start + 1; i < lines.length && !lines[i].startsWith('; CONFIG_BLOCK_END'); ++i) {
    const m = /^; ([A-Za-z0-9_]+) = (.*)$/.exec(lines[i]);
    if (m) config[m[1]] = m[2];
  }
  return config;
}

const isCommand = (l) => l.trim() !== '' && !l.trimStart().startsWith(';');
const command = (l) => l.split(';')[0].trim();
// Slack for the clearance check: G-code rounding (0.001 mm), arc fitting and polygon simplification.
const CLEARANCE_TOLERANCE = 0.05;

function pointSegmentDistance(p, a, b) {
  const dx = b.x - a.x, dy = b.y - a.y;
  const len2 = dx * dx + dy * dy;
  const t = len2 > 0 ? Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2)) : 0;
  return Math.hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

function segmentDistance(s, t) {
  const cross = (o, p, q) => (p.x - o.x) * (q.y - o.y) - (p.y - o.y) * (q.x - o.x);
  const d1 = cross(t.a, t.b, s.a), d2 = cross(t.a, t.b, s.b), d3 = cross(s.a, s.b, t.a), d4 = cross(s.a, s.b, t.b);
  if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) return 0;
  return Math.min(pointSegmentDistance(s.a, t.a, t.b), pointSegmentDistance(s.b, t.a, t.b),
                  pointSegmentDistance(t.a, s.a, s.b), pointSegmentDistance(t.b, s.a, s.b));
}

// Outer wall loops of a layer: consecutive wall segments joined end to start, each closed back to
// its first point (a seam gap or wipe is negligible for the inside test).
function wallLoops(layer) {
  const loops = [];
  let loop = null;
  for (const s of layer.wallSegs) {
    if (loop && Math.hypot(s.a.x - loop[loop.length - 1].x, s.a.y - loop[loop.length - 1].y) < 1e-3) loop.push(s.b);
    else { loop = [s.a, s.b]; loops.push(loop); }
  }
  return loops.filter((l) => l.length >= 3);
}

// Number of brim segments whose midpoint lies inside the layer islands, approximated by the outer
// wall loops with the even-odd rule (holes have outer walls too, so they count as outside).
function brimSegmentsInsideIslands(layer) {
  const loops = wallLoops(layer);
  if (!loops.length) return 0;
  let inside = 0;
  for (const s of layer.brimSegs) {
    const p = { x: (s.a.x + s.b.x) / 2, y: (s.a.y + s.b.y) / 2 };
    let odd = false;
    for (const loop of loops)
      for (let i = 0, j = loop.length - 1; i < loop.length; j = i++) {
        const a = loop[i], b = loop[j];
        if ((a.y > p.y) !== (b.y > p.y) && p.x < ((b.x - a.x) * (p.y - a.y)) / (b.y - a.y) + a.x) odd = !odd;
      }
    if (odd) ++inside;
  }
  return inside;
}

// Smallest edge-to-edge distance between the brim and the outer walls of a layer (undefined when
// one of them is missing). Wall segments are filtered by bounding box around each brim segment.
// own: true measures each brim against the walls of its own object, false against the others.
function brimWallClearance(layer, own) {
  if (!layer.brimSegs.length || !layer.wallSegs.length) return undefined;
  const walls = layer.wallSegs.map((s) => ({ ...s,
    minX: Math.min(s.a.x, s.b.x), maxX: Math.max(s.a.x, s.b.x), minY: Math.min(s.a.y, s.b.y), maxY: Math.max(s.a.y, s.b.y) }));
  let best = Infinity;
  for (const s of layer.brimSegs) {
    const minX = Math.min(s.a.x, s.b.x), maxX = Math.max(s.a.x, s.b.x), minY = Math.min(s.a.y, s.b.y), maxY = Math.max(s.a.y, s.b.y);
    for (const w of walls) {
      if ((w.object === s.object) !== own) continue;
      const reach = (s.w + w.w) / 2 + (Number.isFinite(best) ? best : 1e9);
      if (w.minX - maxX > reach || minX - w.maxX > reach || w.minY - maxY > reach || minY - w.maxY > reach) continue;
      best = Math.min(best, segmentDistance(s, w) - (s.w + w.w) / 2);
    }
  }
  return Number.isFinite(best) ? best : undefined;
}

function checkFile(file, expected) {
  const results = [];
  const ok = (m) => results.push({ ok: true, m });
  const bad = (m) => results.push({ ok: false, m });
  const lines = readFileSync(file, 'utf8').split(/\r?\n/);
  const config = parseConfig(lines);
  if (!Object.keys(config).length) { bad('no CONFIG block: slice with MentorSlicer (not a binary G-code)'); return results; }

  const num = (k, d = 0) => (config[k] === undefined ? d : Number(String(config[k]).split(',')[0]));
  const list = (k) => String(config[k] ?? '').split(',').map((v) => Number(v.trim()));
  const at = (values, i) => (values.length ? values[Math.min(i, values.length - 1)] : 0);
  // Heat soak as the engine resolves it (GCode.cpp): over the used filaments with a soak time
  // > 0, the longest time and the hottest bed, where a filament's heat_soak_bed_temp of 0 means
  // its own initial layer bed temperature for the plate type (curr_bed_type).
  // Used filaments: the T<n> commands of the file (a trimmed fixture may miss later tool changes).
  const bedKey = {
    'Cool Plate': 'cool_plate_temp_initial_layer', 'Engineering Plate': 'eng_plate_temp_initial_layer',
    'High Temp Plate': 'hot_plate_temp_initial_layer', 'Textured PEI Plate': 'textured_plate_temp_initial_layer',
    'Epoxy Resin Plate': 'epoxy_resin_plate_temp_initial_layer', 'Customized Plate': 'customized_plate_temp_initial_layer',
  }[config.curr_bed_type];
  const usedFilaments = [...new Set(lines.map((l) => /^T(\d+)\b/.exec(command(l))?.[1]).filter((t) => t !== undefined).map(Number))];
  if (!usedFilaments.length) usedFilaments.push(0);
  const soakTimes = list('heat_soak_time');
  const soakBeds = list('heat_soak_bed_temp');
  const firstLayerBeds = bedKey && config[bedKey] !== undefined ? list(bedKey) : [];
  let soakMin = 0;
  let soakBed = 0;
  for (const id of usedFilaments) {
    const minutes = at(soakTimes, id) || 0;
    if (minutes <= 0) continue;
    soakMin = Math.max(soakMin, minutes);
    let bed = Math.min(Math.max(at(soakBeds, id) || 0, 0), 120);
    if (bed <= 0) bed = at(firstLayerBeds, id) || 0;
    soakBed = Math.max(soakBed, bed);
  }
  soakMin = Math.min(Math.max(soakMin, 0), 60);
  const airFiltration = num('support_air_filtration') === 1;
  const auxFan = num('auxiliary_fan') === 1;
  const klipper = config.gcode_flavor === 'klipper';
  const extruders = String(config.nozzle_diameter ?? '0.4').split(',').length;
  const singleExtruder = extruders === 1 || num('single_extruder_multi_material') === 1;
  const brimLayers = Math.max(1, num('brim_layers', 1));
  const brimGap = num('brim_object_gap', 0);
  // Gap of the brim layers above the first; older fixtures (before the option) have none.
  const brimUpperGap = num('brim_layers_object_gap', brimGap);
  const brimOn = (config.brim_type ?? 'no_brim') !== 'no_brim' && num('brim_width') > 0;
  const startGcode = (config.machine_start_gcode ?? '').split('\\n').map((l) => l.trim()).find((l) => l && !l.startsWith(';')) ?? '';
  const startWord = startGcode.split(/\s+/)[0];

  // ---- heat soak ----
  const soakIdx = lines.map((l, i) => (l.startsWith('; HEAT_SOAK') ? i : -1)).filter((i) => i >= 0);
  if (soakMin <= 0) {
    if (soakIdx.length) bad(`heat_soak_time=0 but ${soakIdx.length} HEAT_SOAK comment(s) found`);
    else ok('heat soak disabled: no HEAT_SOAK block');
  } else if (soakBed <= 0) {
    if (soakIdx.length === 1 && lines[soakIdx[0]].includes('skipped')) ok('heat soak skipped with bed 0');
    else bad('soak bed temperature resolves to 0 (soak and first-layer bed 0): expected one "; HEAT_SOAK skipped" comment');
  } else if (soakIdx.length !== 1) {
    bad(`expected exactly one HEAT_SOAK block, found ${soakIdx.length}`);
  } else {
    let i = soakIdx[0];
    const header = `; HEAT_SOAK: ${soakMin} min at bed ${soakBed}`;
    if (lines[i] === header) ok(`soak comment "${header}"`);
    else bad(`soak comment is "${lines[i]}", expected "${header}"`);
    const body = [];
    for (++i; i < lines.length && body.length < 6; ++i) if (isCommand(lines[i])) body.push({ i, c: command(lines[i]) });
    const expectedBody = [];
    if (airFiltration) expectedBody.push('M106 P1 S0');
    if (auxFan) expectedBody.push('M106 P2 S0');
    expectedBody.push(`M190 S${soakBed}`, `G4 P${soakMin * 60000}`);
    const got = body.slice(0, expectedBody.length).map((b) => b.c);
    if (got.join('|') === expectedBody.join('|')) ok(`soak body: ${got.join(', ')}`);
    else bad(`soak body is [${got.join(', ')}], expected [${expectedBody.join(', ')}]`);
    const dwell = Number(/^G4 P(\d+)/.exec(got[got.length - 1] ?? '')?.[1]);
    if (dwell > 0 && dwell <= MAX_DWELL_MS) ok(`dwell ${dwell} ms <= ${MAX_DWELL_MS}`);
    else bad(`dwell ${dwell} ms out of range (1..${MAX_DWELL_MS})`);
    const next = body[expectedBody.length];
    if (!startWord) bad('machine_start_gcode is empty: cannot check the soak placement');
    else if (next && next.c.split(/\s+/)[0] === startWord) ok(`soak immediately before machine_start_gcode (${startWord}, line ${next.i + 1})`);
    else if (klipper && singleExtruder) bad(`after the soak comes "${next?.c}", expected ${startWord} (no extra M190/M140 on Klipper single extruder)`);
    else {
      // Slicer-heated printers may set the first-layer bed temperature between soak and start G-code.
      const between = body.slice(expectedBody.length).findIndex((b) => b.c.split(/\s+/)[0] === startWord);
      if (between >= 0) ok(`soak before machine_start_gcode with ${between} slicer temperature line(s) in between`);
      else bad(`machine_start_gcode (${startWord}) not found right after the soak`);
    }
    // Klipper single extruder: the start macro owns heating, nothing between the soak and START_PRINT.
    if (klipper && singleExtruder && next) {
      const extra = [];
      for (let k = soakIdx[0] + 1; k < next.i; ++k)
        if (/^M1(90|40)\b/.test(command(lines[k])) && !command(lines[k]).startsWith(`M190 S${soakBed}`)) extra.push(command(lines[k]));
      if (extra.length) bad(`extra bed commands between soak and ${startWord}: ${extra.join(', ')}`);
    }
    // The soak must come before the first layer.
    const firstLayer = lines.indexOf(';LAYER_CHANGE');
    if (firstLayer >= 0 && soakIdx[0] > firstLayer) bad('HEAT_SOAK block after the first layer change');
  }

  // ---- brim_layers ----
  // Layers are split by ;LAYER_CHANGE + ;Z:. Brim extrusions are G1 moves with E>0 after
  // ;TYPE:Brim, until the next ;TYPE: or layer change. Objects come from "; printing object".
  // The XY position is tracked (absolute XY, G2/G3 arcs split into short chords) to collect the
  // extruded segments of the brim and of the outer walls, used for the brim clearance check.
  const layers = [];
  let cur = null;
  let object = '_none';
  let inBrim = false;
  let brimBlock = null;
  let type = '';
  let width = 0;
  let pos = { x: NaN, y: NaN };
  for (let i = 0; i < lines.length; ++i) {
    const l = lines[i];
    if (l === ';LAYER_CHANGE') {
      cur = { z: NaN, line: i + 1, blocks: [], brimSegs: [], wallSegs: [] };
      layers.push(cur);
      inBrim = false;
      continue;
    }
    if (l.startsWith(';WIDTH:')) { width = Number(l.slice(7)); continue; }
    const move = /^G([0-3])\s/.exec(l);
    if (move) {
      const words = command(l).split(/\s+/).slice(1);
      const word = (c) => { const w = words.find((t) => t[0] === c); return w === undefined ? undefined : Number(w.slice(1)); };
      const to = { x: word('X') ?? pos.x, y: word('Y') ?? pos.y };
      const e = word('E') ?? 0;
      // Unretracts (E without X/Y) are not extrusions along a path.
      const kind = !cur || e <= 0 || (word('X') === undefined && word('Y') === undefined) ? null : inBrim ? 'brim' : (type === 'Outer wall' || type === 'Overhang wall') ? 'wall' : null;
      if (kind && Number.isFinite(pos.x) && Number.isFinite(to.x)) {
        let pts = [pos, to];
        if (move[1] === '2' || move[1] === '3') {
          const c = { x: pos.x + (word('I') ?? 0), y: pos.y + (word('J') ?? 0) };
          const r = Math.hypot(pos.x - c.x, pos.y - c.y);
          let a0 = Math.atan2(pos.y - c.y, pos.x - c.x);
          let a1 = Math.atan2(to.y - c.y, to.x - c.x);
          if (move[1] === '3' && a1 <= a0) a1 += 2 * Math.PI;
          if (move[1] === '2' && a1 >= a0) a1 -= 2 * Math.PI;
          const n = Math.max(1, Math.ceil(Math.abs(a1 - a0) * r / 0.2));
          pts = [];
          for (let k = 0; k <= n; ++k) {
            const a = a0 + ((a1 - a0) * k) / n;
            pts.push({ x: c.x + r * Math.cos(a), y: c.y + r * Math.sin(a) });
          }
          pts[pts.length - 1] = to;
        }
        const dst = kind === 'brim' ? cur.brimSegs : cur.wallSegs;
        for (let k = 1; k < pts.length; ++k) dst.push({ a: pts[k - 1], b: pts[k], w: width, object });
      }
      pos = to;
    }
    if (!cur) continue;
    if (l.startsWith(';Z:') && Number.isNaN(cur.z)) { cur.z = Number(l.slice(3)); continue; }
    if (l.startsWith('; printing object ')) object = l.slice('; printing object '.length);
    else if (l.startsWith('; stop printing object ')) object = '_none';
    else if (l.startsWith(';TYPE:')) {
      type = l.slice(6);
      inBrim = l === ';TYPE:Brim';
      if (inBrim) { brimBlock = { object, e: 0, moves: 0 }; cur.blocks.push(brimBlock); }
    } else if (inBrim && /^G1 /.test(l)) {
      const e = /\bE(-?[\d.]+)/.exec(l);
      if (e && Number(e[1]) > 0 && /\b[XY]-?[\d.]/.test(l)) { brimBlock.e += Number(e[1]); brimBlock.moves++; }
    }
  }
  if (!layers.length) bad('no ;LAYER_CHANGE markers');
  const fixtureNote = lines.find((l) => l.startsWith('; PRINT_ADVISOR_FIXTURE:'));
  const perObject = (layer) => {
    const map = new Map();
    for (const b of layer.blocks) {
      const s = map.get(b.object) ?? { blocks: 0, e: 0, moves: 0 };
      s.blocks++; s.e += b.e; s.moves += b.moves;
      map.set(b.object, s);
    }
    return map;
  };
  if (!brimOn) {
    const any = layers.findIndex((L) => L.blocks.length);
    if (any >= 0) bad(`brim disabled but ;TYPE:Brim found on layer ${any}`);
    else ok('brim disabled: no brim');
  } else if (layers.length) {
    const base = perObject(layers[0]);
    const h0 = layers[0].z;
    if (!base.size) bad('brim enabled but no brim on the first layer');
    for (let n = 0; n < layers.length; ++n) {
      const L = layers[n];
      const m = perObject(L);
      const h = n === 0 ? L.z : L.z - layers[n - 1].z;
      if (n < brimLayers) {
        for (const [obj, b0] of base) {
          const s = m.get(obj);
          if (!s || s.moves === 0) { bad(`layer ${n} (z ${L.z}): no brim extrusion for ${obj}`); continue; }
          if (s.blocks !== b0.blocks) bad(`layer ${n}: ${obj} has ${s.blocks} brim block(s), layer 0 has ${b0.blocks} (printed more than once?)`);
          // The repeated brim rescales the flow to the layer height.
          const ratio = s.e / b0.e;
          const want = h / h0;
          // With brim_layers_object_gap larger than brim_object_gap the repeated brim is clipped
          // around the object, so it may only extrude less than the rescaled first-layer brim.
          const clipped = n > 0 && brimUpperGap > brimGap;
          if (n > 0 && (clipped ? ratio > want * 1.1 : Math.abs(ratio - want) > 0.1 * want))
            bad(`layer ${n}: brim E ratio ${ratio.toFixed(3)} vs height ratio ${want.toFixed(3)}${clipped ? ' (clipped brim may be shorter, never longer)' : ''}`);
          if (clipped && s.moves < 2) bad(`layer ${n}: brim for ${obj} almost gone after clipping with gap ${brimUpperGap}`);
        }
        for (const obj of m.keys()) if (!base.has(obj)) bad(`layer ${n}: brim for ${obj} that has none on layer 0`);
        // Clearance between the printed brim and the outer walls on this layer: centre distance
        // minus half the brim width minus half the wall width, i.e. brim edge to the slice contour.
        // Enforced only when brim_layers_object_gap > brim_object_gap: from the brim's own object
        // at least brim_layers_object_gap; from other objects at least the smaller of that and the
        // gap layer 1 keeps from them (brim_object_gap), as the engine does.
        // The gaps come from the global CONFIG block: per-object overrides (3MF) are not in the
        // G-code, so a file with per-object brim gaps may be judged against the wrong value.
        if (n > 0) {
          const inside = brimSegmentsInsideIslands(L);
          if (inside) bad(`layer ${n}: ${inside} brim segment(s) inside an object island (outer wall loops)`);
          for (const [own, what, need] of [[true, 'own object', brimUpperGap], [false, 'other objects', Math.min(brimUpperGap, brimGap)]]) {
            const clearance = brimWallClearance(L, own);
            if (clearance === undefined) { if (own) ok(`layer ${n}: no brim/outer wall segments to measure the clearance`); continue; }
            if (brimUpperGap <= brimGap)
              ok(`layer ${n}: brim edge ${clearance.toFixed(3)} mm from the walls of ${what} (info: brim_layers_object_gap not above brim_object_gap)`);
            else if (clearance >= need - CLEARANCE_TOLERANCE)
              ok(`layer ${n}: brim edge ${clearance.toFixed(3)} mm from the walls of ${what} >= ${need}`);
            else bad(`layer ${n}: brim edge ${clearance.toFixed(3)} mm from the walls of ${what} < ${need}`);
          }
        }
        if (n > 0 && ![...base.keys()].some((o) => !m.get(o)))
          ok(`layer ${n} (z ${L.z}): brim ${[...m.values()].map((s) => `${s.blocks}x ${s.moves} moves E${s.e.toFixed(2)}`).join('; ')}`);
      } else if (L.blocks.length) {
        bad(`layer ${n} (z ${L.z}) >= brim_layers ${brimLayers} has ${L.blocks.length} brim block(s)`);
      }
    }
    if (layers.length < brimLayers) bad(`only ${layers.length} layer(s) in the file, brim_layers is ${brimLayers}`);
    else if (layers.length === brimLayers) bad('no layer after the brim layers in the file: cannot check the brim stops');
    else ok(`no brim on layers ${brimLayers}..${layers.length - 1}${fixtureNote ? ' (trimmed fixture)' : ''}`);
  }

  // ---- expected values ----
  if (expected) {
    const actual = {
      heat_soak_time: soakMin, heat_soak_bed_temp: soakBed, support_air_filtration: airFiltration,
      auxiliary_fan: auxFan, gcode_flavor: config.gcode_flavor, brim_layers: brimLayers,
      brim_object_gap: brimGap, brim_layers_object_gap: brimUpperGap,
      layers_in_file: layers.length, first_layer_z: layers[0]?.z,
      brim_objects: layers[0] ? perObject(layers[0]).size : 0,
      brim_blocks_per_object_layer: layers[0] && perObject(layers[0]).size ? [...perObject(layers[0]).values()][0].blocks : 0,
      machine_start_command: startWord,
    };
    for (const [k, v] of Object.entries(expected)) {
      if (k.startsWith('_')) continue;
      if (!(k in actual)) { bad(`expected key ${k} is unknown to the checker`); continue; }
      if (actual[k] === v) ok(`${k} = ${JSON.stringify(v)}`);
      else bad(`${k} = ${JSON.stringify(actual[k])}, expected ${JSON.stringify(v)}`);
    }
  }
  return results;
}

let targets = [];
const argv = process.argv.slice(2);
if (argv.length) {
  const ei = argv.indexOf('--expected');
  const expectedFile = ei >= 0 ? argv[ei + 1] : undefined;
  const file = argv.find((a, i) => !a.startsWith('--') && (ei < 0 || i !== ei + 1));
  if (!file) { console.error('usage: check-gcode.mjs <file.gcode> [--expected <file.json>]'); process.exit(2); }
  targets.push({ file, expectedFile });
} else {
  const dir = join(here, 'fixtures');
  for (const f of readdirSync(dir).filter((f) => f.endsWith('.gcode'))) {
    const e = join(dir, f.replace(/\.gcode$/, '.expected.json'));
    targets.push({ file: join(dir, f), expectedFile: existsSync(e) ? e : undefined });
  }
}

let failed = 0;
for (const { file, expectedFile } of targets) {
  console.log(`== ${file}${expectedFile ? ` (expected: ${expectedFile})` : ''}`);
  const expected = expectedFile ? JSON.parse(readFileSync(expectedFile, 'utf8')) : undefined;
  const results = checkFile(file, expected);
  for (const r of results) console.log(`${r.ok ? 'ok  ' : 'FAIL'} ${r.m}`);
  const f = results.filter((r) => !r.ok).length;
  failed += f;
  console.log(`${f ? 'FAILED' : 'PASSED'}: ${results.length - f} ok, ${f} failed\n`);
}
process.exit(failed ? 1 : 0);
