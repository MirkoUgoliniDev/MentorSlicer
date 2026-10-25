#!/usr/bin/env node
// Slices every fixture of tests/regression/fixtures with the MentorSlicer CLI and writes the
// G-code plus a manifest (doc/specs/F0-baseline-verifica-spec.md, sections 1.7-1.8).
//
//   node tests/regression/slice-fixtures.mjs --out <dir> [--exe <path>] [--only a,b] [--runs 1|2]
//                                            [--timeout-min 15] [--keep-temp]
//
// - --exe defaults to <repo>\build\src\Release\MentorSlicer.exe (MentorSlicer_Slicer.dll must be
//   next to it: the exe is only a launcher). For a build made before the F8 rename pass --exe; the
//   DLL is then looked up as <exe name>_Slicer.dll.
// - Every fixture gets a fresh, empty --datadir and its own TEMP/TMP under <out>\_work\<name>, so
//   the CLI never touches %APPDATA% and leaves nothing in the user's temp folder.
// - --runs 2 slices everything twice (<out>\run1, <out>\run2), compares the two outputs with
//   compare-gcode.mjs and copies run1 into <out> as the result. Any difference is FAIL
//   "nondeterministic".
// - Writes <out>\slice-manifest.json.
//
// Exit codes: 0 all sliced; 1 at least one slice failed or nondeterministic; 2 usage or environment.
// Never builds, never starts the GUI. Do not run a build task while this runs: the build task closes
// every MentorSlicer.exe under build*, the CLI included.

import { spawnSync, execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, mkdirSync, readdirSync, readFileSync, renameSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { basename, dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { compareFiles, loadRules, normalize, readGcode } from './compare-gcode.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const repo = resolve(here, '..', '..');
const fixturesDir = join(here, 'fixtures');

// Meaning of the CLI exit codes (src/libslic3r/Utils.hpp).
const EXIT_MEANING = {
  0: 'ok', '-1': 'environment error', '-2': 'invalid parameters', '-3': 'file not found', '-4': 'file list in invalid order',
  '-5': 'config file error', '-6': 'data file error', '-7': 'invalid printer technology', '-8': 'unsupported operation',
  '-14': 'out of memory', '-17': 'process not compatible', '-18': 'invalid values in the 3MF',
  '-19': 'post_process scripts present (use --normative-check=0)', '-21': 'object arrange failed',
  '-24': '3MF version not supported', '-50': 'no object fully on the plate', '-51': 'print validation error',
  '-52': 'object partly outside the plate', '-53': 'export cache directory create failed', '-54': 'export cache write failed',
  '-55': 'import cache not found', '-56': 'import cache data not usable', '-57': 'import cache load failed',
  '-58': 'slicing time exceeds the limit', '-59': 'triangle count exceeds the limit', '-60': 'no suitable objects after skip',
  '-61': 'filament / bed type conflict', '-62': 'filaments with different temperatures', '-63': 'object collision in sequential print',
  '-64': 'object collision in by-layer print', '-65': 'spiral mode: invalid parameters',
  '-100': 'slicing error (empty layers or G-code overlap)', '-101': 'G-code path conflicts',
};

function usage(msg) {
  if (msg) console.error(`error: ${msg}`);
  console.error('usage: slice-fixtures.mjs --out <dir> [--exe <path>] [--only a,b] [--runs 1|2] [--timeout-min 15] [--keep-temp]');
  process.exit(2);
}

function parseArgs(argv) {
  const o = { out: null, exe: join(repo, 'build', 'src', 'Release', 'MentorSlicer.exe'), only: null, runs: 1, timeoutMin: 15, keepTemp: false };
  const value = (i, a) => { if (argv[i] === undefined || argv[i].startsWith('--')) usage(`${a} needs a value`); return argv[i]; };
  for (let i = 0; i < argv.length; ++i) {
    const a = argv[i];
    if (a === '--out') o.out = resolve(value(++i, a));
    else if (a === '--exe') o.exe = resolve(value(++i, a));
    else if (a === '--only') o.only = new Set(value(++i, a).split(',').map((s) => s.trim()).filter(Boolean));
    else if (a === '--runs') o.runs = parseInt(value(++i, a), 10);
    else if (a === '--timeout-min') o.timeoutMin = parseFloat(value(++i, a));
    else if (a === '--keep-temp') o.keepTemp = true;
    else if (a === '--help' || a === '-h') usage();
    else usage(`unknown argument ${a}`);
  }
  if (!o.out) usage('--out is required');
  if (![1, 2].includes(o.runs)) usage('--runs must be 1 or 2');
  if (!(o.timeoutMin > 0)) usage('--timeout-min must be a positive number');
  return o;
}

const sha256File = (file) => createHash('sha256').update(readFileSync(file)).digest('hex');
const isoMtime = (file) => statSync(file).mtime.toISOString();

function git(args) {
  try { return execFileSync('git', args, { cwd: repo, encoding: 'utf8', windowsHide: true }).trim(); }
  catch { return null; }
}

function checkEnvironment(o, fixtures) {
  const problems = [];
  // The launcher loads "<PROCESS_NAME>_Slicer.dll" (cmake/BuildInfoUtil.cmake).
  const dll = join(dirname(o.exe), `${basename(o.exe).replace(/\.exe$/i, '')}_Slicer.dll`);
  if (!existsSync(o.exe)) problems.push(`exe not found: ${o.exe}`);
  if (!existsSync(dll)) problems.push(`${basename(dll)} not found next to the exe: ${dll}`);
  for (const p of [o.exe, o.out, ...fixtures]) if (/\s/.test(p)) problems.push(`path contains whitespace (the CLI argument joining breaks): ${p}`);
  const appdata = process.env.APPDATA ? resolve(process.env.APPDATA).toLowerCase() : null;
  if (appdata && resolve(o.out).toLowerCase().startsWith(appdata)) problems.push(`--out must not be inside %APPDATA%: ${o.out}`);
  if (resolve(o.out).toLowerCase() === resolve(repo).toLowerCase()) problems.push('--out must not be the repository root');
  if (!fixtures.length) problems.push(`no fixture found in ${fixturesDir}`);
  return { problems, dll };
}

// Deletes <outDir>/<name>.gcode and <outDir>/<name>.plate_<n>.gcode from an earlier run.
function removeOutputs(outDir, name) {
  if (!existsSync(outDir)) return;
  for (const f of readdirSync(outDir)) {
    const lower = f.toLowerCase(), prefix = name.toLowerCase();
    if (lower === `${prefix}.gcode` || (lower.startsWith(`${prefix}.plate_`) && lower.endsWith('.gcode'))) rmSync(join(outDir, f), { force: true });
  }
}

// Slices one fixture into <outDir>. Returns the per-fixture manifest entry.
function sliceOne(o, fixture, outDir, rules) {
  const name = fixture.replace(/\.3mf$/i, '');
  const fixturePath = join(fixturesDir, fixture);
  const work = join(outDir, '_work', name);
  const datadir = join(work, 'datadir'), tmp = join(work, 'tmp'), plates = join(work, 'plates');
  const log = join(outDir, `${name}.log`);
  rmSync(work, { recursive: true, force: true });
  for (const d of [datadir, tmp, plates]) mkdirSync(d, { recursive: true });
  rmSync(log, { force: true });
  // Remove the outputs of an earlier run first: a failed slice must never leave a stale G-code behind.
  removeOutputs(outDir, name);

  const args = ['--cli', '--slice', '0', '--need-gcode-file', '--allow-newer-file', '--outputdir', plates, '--datadir', datadir,
    '--debug', '3', '--logfile', log, fixturePath];
  const entry = { fixture, fixture_sha256: sha256File(fixturePath), status: 'FAIL', exit_code: null, seconds: null, gcode: null, bytes: null,
    sha256_raw: null, sha256_normalized: null, lines: null, layers: null, preset_root: null, warnings: [], errors: [], command: [o.exe, ...args] };
  console.log(`-- ${name}: slicing ...`);
  const t0 = Date.now();
  const r = spawnSync(o.exe, args, {
    env: { ...process.env, TEMP: tmp, TMP: tmp }, windowsHide: true, timeout: o.timeoutMin * 60_000, stdio: ['ignore', 'pipe', 'pipe'],
    encoding: 'utf8', maxBuffer: 64 * 1024 * 1024,
  });
  entry.seconds = Math.round((Date.now() - t0) / 100) / 10;
  const logText = existsSync(log) ? readFileSync(log, 'latin1') : '';
  const logLines = logText.split(/\r?\n/).filter((l) => l !== '');

  if (r.error) {
    entry.errors.push(r.error.code === 'ETIMEDOUT' ? `timeout after ${o.timeoutMin} min (process killed)` : `spawn error: ${r.error.message}`);
  }
  const code = r.status === null ? null : r.status | 0; // Windows may report -2 as 4294967294
  entry.exit_code = code;
  if (code !== 0) {
    if (code !== null) entry.errors.push(`exit code ${code}: ${EXIT_MEANING[String(code)] ?? 'unknown (see src/libslic3r/Utils.hpp)'}`);
    else if (r.signal) entry.errors.push(`killed by ${r.signal}`);
    entry.log_tail = logLines.slice(-30);
    if (r.stderr?.trim()) entry.stderr = r.stderr.trim().slice(-4000);
  }

  // Log checks (section 1.8, 3.5).
  const merge = logLines.map((l) => /refreshed Creality project with GUI preset merge: root=(.*?), print=/.exec(l)).find(Boolean);
  if (merge) {
    entry.preset_root = merge[1];
    if (!/resources[\\/]profiles[\\/]?$/i.test(merge[1])) entry.warnings.push(`preset root is not <resources>\\profiles: ${merge[1]}`);
  } else if (logLines.some((l) => l.includes('Unable to refresh Creality project presets'))) {
    entry.warnings.push('preset merge failed: the CLI used the embedded project settings (deterministic, but not the GUI merge path)');
  } else if (code === 0) {
    entry.warnings.push('no "refreshed Creality project with GUI preset merge" line in the log');
  }

  // Collect the plate G-codes.
  const produced = existsSync(plates) ? readdirSync(plates).filter((f) => /^plate_\d+\.gcode$/i.test(f)).sort() : [];
  if (code === 0 && !produced.length) entry.errors.push('exit 0 but no plate_<n>.gcode was written');
  if (produced.length > 1) entry.warnings.push(`more than one plate sliced: ${produced.join(', ')} (one expected)`);
  for (const f of produced) {
    const dest = produced.length === 1 ? join(outDir, `${name}.gcode`) : join(outDir, `${name}.${f}`);
    rmSync(dest, { force: true });
    renameSync(join(plates, f), dest);
    if (produced.length === 1 || !entry.gcode) {
      entry.gcode = dest;
      entry.bytes = statSync(dest).size;
      entry.sha256_raw = sha256File(dest);
      const text = readGcode(dest);
      const n = normalize(text, rules);
      entry.sha256_normalized = n.hash;
      entry.lines = n.lines.length;
      entry.layers = n.lines.reduce((c, l) => c + (l.startsWith(';LAYER_CHANGE') ? 1 : 0), 0);
      entry.warnings.push(...n.warnings);
    }
  }
  if (!o.keepTemp) rmSync(work, { recursive: true, force: true });
  entry.status = entry.errors.length ? 'FAIL' : 'ok';
  const tag = entry.status === 'ok' ? 'ok  ' : 'FAIL';
  console.log(`${tag} ${name}: exit ${code}, ${entry.seconds}s${entry.gcode ? `, ${entry.lines} lines, ${entry.layers} layers` : ''}${entry.preset_root ? `, root=${entry.preset_root}` : ''}`);
  for (const w of entry.warnings) console.log(`WARN ${name}: ${w}`);
  for (const e of entry.errors) console.log(`FAIL ${name}: ${e}`);
  if (entry.log_tail?.length) { console.log(`     last log lines (${log}):`); for (const l of entry.log_tail) console.log(`     | ${l}`); }
  if (entry.stderr) console.log(`     stderr: ${entry.stderr}`);
  return entry;
}

function main() {
  const o = parseArgs(process.argv.slice(2));
  let fixtures = existsSync(fixturesDir) ? readdirSync(fixturesDir).filter((f) => f.toLowerCase().endsWith('.3mf')).sort() : [];
  if (o.only) {
    const missing = [...o.only].filter((n) => !fixtures.includes(`${n}.3mf`));
    if (missing.length) usage(`--only: fixture not found: ${missing.join(', ')}`);
    fixtures = fixtures.filter((f) => o.only.has(f.replace(/\.3mf$/i, '')));
  }
  const { problems, dll } = checkEnvironment(o, fixtures.map((f) => join(fixturesDir, f)));
  if (problems.length) { for (const p of problems) console.error(`error: ${p}`); process.exit(2); }
  mkdirSync(o.out, { recursive: true });
  const { rules } = loadRules();

  const manifest = {
    phase: null, exe: { path: o.exe, bytes: statSync(o.exe).size, mtime: isoMtime(o.exe), sha256: sha256File(o.exe) },
    dll: { path: dll, bytes: statSync(dll).size, mtime: isoMtime(dll), sha256: sha256File(dll) },
    git_head: git(['rev-parse', 'HEAD']), git_branch: git(['rev-parse', '--abbrev-ref', 'HEAD']),
    git_dirty_src: (git(['status', '--porcelain', '--', 'src']) || '').split('\n').filter(Boolean),
    node: process.version, date: new Date().toISOString(), runs: o.runs, deterministic: o.runs === 2 ? true : null,
    out: o.out, fixtures: {},
  };
  console.log(`exe ${o.exe}\ndll ${dll} (${manifest.dll.bytes} bytes, ${manifest.dll.mtime})\ngit ${manifest.git_branch} ${manifest.git_head}${manifest.git_dirty_src.length ? ` (dirty: ${manifest.git_dirty_src.length} file(s) under src)` : ''}`);
  console.log(`fixtures: ${fixtures.map((f) => f.replace(/\.3mf$/i, '')).join(', ')}; runs: ${o.runs}\n`);

  let failed = 0;
  if (o.runs === 1) {
    for (const f of fixtures) {
      const e = sliceOne(o, f, o.out, rules);
      manifest.fixtures[f.replace(/\.3mf$/i, '')] = e;
      if (e.status !== 'ok') failed++;
    }
  } else {
    const runDirs = [join(o.out, 'run1'), join(o.out, 'run2')];
    const runs = [{}, {}];
    for (const [i, dir] of runDirs.entries()) {
      console.log(`== run ${i + 1} -> ${dir}`);
      mkdirSync(dir, { recursive: true });
      for (const f of fixtures) runs[i][f.replace(/\.3mf$/i, '')] = sliceOne(o, f, dir, rules);
      console.log('');
    }
    console.log('== determinism check (run1 against run2)');
    for (const f of fixtures) {
      const name = f.replace(/\.3mf$/i, '');
      const e = { ...runs[0][name], run2: { exit_code: runs[1][name].exit_code, seconds: runs[1][name].seconds, sha256_raw: runs[1][name].sha256_raw, sha256_normalized: runs[1][name].sha256_normalized } };
      if (runs[1][name].status !== 'ok') e.errors = [...e.errors, ...runs[1][name].errors.map((x) => `run2: ${x}`)];
      if (runs[0][name].gcode && runs[1][name].gcode) {
        const c = compareFiles(runs[0][name].gcode, runs[1][name].gcode, { name, rulesLoaded: { rules, warnings: [] } });
        if (c.status === 'IDENTICAL') console.log(`ok   ${name}: deterministic (normalised sha256 ${c.sha256.slice(0, 12)})`);
        else {
          manifest.deterministic = false;
          const first = c.diff?.shown?.[0];
          e.errors.push(`nondeterministic: run1 and run2 differ${first ? ` (first: line ${first.line}, ${first.where})` : ''}`);
          console.log(`FAIL ${name}: nondeterministic${first ? ` (first: line ${first.line}, ${first.where})\n     - ${first.a}\n     + ${first.b}` : ''}`);
          for (const x of c.config?.changed ?? []) console.log(`     CONFIG: changed ${x.key}: ${x.from} -> ${x.to}`);
        }
      }
      // run1 is the result: copy its G-code and log next to the manifest (after removing stale ones).
      removeOutputs(o.out, name);
      rmSync(join(o.out, `${name}.log`), { force: true });
      for (const [src, suffix] of [[runs[0][name].gcode, '.gcode'], [join(runDirs[0], `${name}.log`), '.log']]) {
        if (src && existsSync(src)) { copyFileSync(src, join(o.out, `${name}${suffix}`)); if (suffix === '.gcode') e.gcode = join(o.out, `${name}${suffix}`); }
      }
      e.status = e.errors.length ? 'FAIL' : 'ok';
      if (e.status !== 'ok') failed++;
      manifest.fixtures[name] = e;
    }
  }
  const manifestFile = join(o.out, 'slice-manifest.json');
  writeFileSync(manifestFile, JSON.stringify(manifest, null, 2) + '\n');
  const okCount = Object.values(manifest.fixtures).filter((e) => e.status === 'ok').length;
  console.log(`\nSLICE: ${failed ? 'FAILED' : 'PASSED'} (${okCount} ok, ${failed} failed${o.runs === 2 ? `, deterministic: ${manifest.deterministic}` : ''}) -> ${manifestFile}`);
  process.exit(failed ? 1 : 0);
}

main();
