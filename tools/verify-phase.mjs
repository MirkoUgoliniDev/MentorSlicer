#!/usr/bin/env node
// Single verification entry point for the lean cleanup phases
// (doc/specs/F0-baseline-verifica-spec.md, section 4).
//
//   node tools/verify-phase.mjs --phase F1 [--slice] [--exe <path>] [--skip build,grep,gcode,web,sidecar,smoke]
//                               [--require-live] [--report <file>] [--no-manual]
//
// Steps: 0 context, 1 build is current, 2 removed names are gone, 3 G-code against the F0 baseline,
// 4 web patches (--check), 5 sidecar (typecheck + smoke), 6 manual checklist (printed).
// It never builds, never starts or closes MentorSlicer.exe (only CLI slices through
// tests/regression/slice-fixtures.mjs with --slice) and never calls a tool that changes something.
//
// Exit codes: 0 no FAIL; 1 at least one FAIL; 2 usage or environment error.
// Node 20+, no dependencies. Works from any cwd (the repository root is found from this file).

import { execFileSync, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { basename, dirname, join, relative, resolve, sep } from 'node:path';
import { createInterface } from 'node:readline';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const repo = resolve(here, '..');
const PHASES = ['F0', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9'];
const STEPS = ['build', 'grep', 'gcode', 'web', 'sidecar', 'smoke'];
const isWin = process.platform === 'win32';
const npmCmd = isWin ? 'npm.cmd' : 'npm';

// ---- report ----
const report = [];
const counts = { ok: 0, warn: 0, skip: 0, fail: 0 };
const failedSteps = new Set();
let currentStep = '';
function line(level, msg) {
  const text = `${level.padEnd(4)} ${msg}`;
  console.log(text);
  report.push(text);
  if (level === 'ok') counts.ok++;
  else if (level === 'WARN') counts.warn++;
  else if (level === 'SKIP') counts.skip++;
  else if (level === 'FAIL') { counts.fail++; failedSteps.add(currentStep); }
}
const ok = (m) => line('ok', m), warn = (m) => line('WARN', m), skip = (m) => line('SKIP', m), fail = (m) => line('FAIL', m), info = (m) => line('info', m);
function heading(step, title) {
  currentStep = step;
  const text = `\n## ${title}`;
  console.log(text);
  report.push(text);
}

function usage(msg) {
  if (msg) console.error(`error: ${msg}`);
  console.error('usage: verify-phase.mjs --phase F0..F9 [--slice] [--exe <path>] [--skip build,grep,gcode,web,sidecar,smoke] [--require-live] [--report <file>] [--no-manual]');
  process.exit(2);
}

function parseArgs(argv) {
  const o = { phase: null, slice: false, exe: null, skip: new Set(), requireLive: false, report: null, manual: true };
  const value = (i, a) => { if (argv[i] === undefined || argv[i].startsWith('--')) usage(`${a} needs a value`); return argv[i]; };
  for (let i = 0; i < argv.length; ++i) {
    const a = argv[i];
    if (a === '--phase') o.phase = value(++i, a).toUpperCase();
    else if (a === '--slice') o.slice = true;
    else if (a === '--exe') o.exe = resolve(value(++i, a));
    else if (a === '--skip') for (const s of value(++i, a).split(',').map((x) => x.trim()).filter(Boolean)) { if (!STEPS.includes(s)) usage(`--skip: unknown step ${s}`); o.skip.add(s); }
    else if (a === '--require-live') o.requireLive = true;
    else if (a === '--report') o.report = resolve(value(++i, a));
    else if (a === '--no-manual') o.manual = false;
    else if (a === '--help' || a === '-h') usage();
    else usage(`unknown argument ${a}`);
  }
  if (!o.phase) usage('--phase is required');
  if (!PHASES.includes(o.phase)) usage(`--phase must be one of ${PHASES.join(', ')}`);
  return o;
}

// ---- helpers ----
const rel = (p) => relative(repo, p).split(sep).join('/');
const sha256File = (f) => createHash('sha256').update(readFileSync(f)).digest('hex');
const fmtTime = (d) => `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')} ${d.toTimeString().slice(0, 8)}`;
const hhmm = (d) => d.toTimeString().slice(0, 5);

function run(cmd, args, opts = {}) {
  const r = spawnSync(cmd, args, { cwd: opts.cwd ?? repo, encoding: 'utf8', windowsHide: true, maxBuffer: 256 * 1024 * 1024, stdio: ['ignore', 'pipe', 'pipe'], ...opts.spawn });
  return { status: r.status === null ? null : r.status | 0, stdout: r.stdout ?? '', stderr: r.stderr ?? '', error: r.error, signal: r.signal };
}
function git(args) {
  const r = run('git', args);
  if (r.error) throw r.error;
  return r;
}
const gitOut = (args) => git(args).stdout.trim();
const gitLines = (args) => gitOut(args).split('\n').map((l) => l.trim()).filter(Boolean);

function powershell(script) {
  const r = run('powershell', ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-Command', script]);
  return r.error ? '' : r.stdout;
}

// MentorSlicer.exe processes: [{ pid, path }]
function runningApp() {
  if (!isWin) return [];
  const out = powershell("Get-CimInstance Win32_Process -Filter \"Name='MentorSlicer.exe'\" | ForEach-Object { \"$($_.ProcessId)`t$($_.ExecutablePath)\" }");
  return out.split(/\r?\n/).filter(Boolean).map((l) => { const [pid, path] = l.split('\t'); return { pid: parseInt(pid, 10), path: path ?? '' }; });
}

// ---- step 0: context ----
function stepContext(o) {
  heading('context', 'Step 0: context');
  info(`phase ${o.phase}, repository ${repo}, node ${process.version}, ${new Date().toISOString()}`);
  const branch = gitOut(['rev-parse', '--abbrev-ref', 'HEAD']);
  const short = gitOut(['rev-parse', '--short', 'HEAD']);
  const describe = gitOut(['describe', '--tags', '--always']);
  info(`git: branch ${branch}, HEAD ${short}, describe ${describe}`);
  if (branch !== 'lean') warn(`branch is ${branch}, the cleanup phases are verified on "lean"`);
  else ok('branch lean');
  const dirty = gitLines(['status', '--porcelain']);
  if (dirty.length) {
    warn(`${dirty.length} uncommitted change(s): ${dirty.slice(0, 8).join(' | ')}${dirty.length > 8 ? ' | ...' : ''}`);
  } else ok('working tree clean');
}

// ---- step 1: build ----
function stepBuild(o) {
  heading('build', 'Step 1: build is current');
  const buildDir = o.exe ? dirname(o.exe) : join(repo, 'build', 'src', 'Release');
  const exe = o.exe ?? join(buildDir, 'MentorSlicer.exe');
  // <exe name>_Slicer.dll, so a pre-F8 executable given with --exe is checked against its own DLL.
  const dll = join(buildDir, `${basename(exe).replace(/\.exe$/i, '')}_Slicer.dll`);
  const have = { exe: existsSync(exe), dll: existsSync(dll) };
  if (!have.dll) fail(`missing ${rel(dll)}`);
  if (!have.exe) fail(`missing ${rel(exe)}`);
  if (!have.dll || !have.exe) return null;
  const dllTime = statSync(dll).mtime, exeTime = statSync(exe).mtime;

  const tracked = gitLines(['ls-files', '--', 'src', 'cmake', 'CMakeLists.txt', 'version.inc']);
  const untracked = gitLines(['ls-files', '-o', '--exclude-standard', '--', 'src']);
  const inputs = [...new Set([...tracked, ...untracked])];
  let newest = null, newestTime = 0, count = 0;
  const stale = [];
  for (const f of inputs) {
    const p = join(repo, f);
    if (!existsSync(p)) continue; // deleted in the working tree
    const t = statSync(p).mtime;
    count++;
    if (t.getTime() > newestTime) { newestTime = t.getTime(); newest = f; }
    // Only the launcher sources build MentorSlicer.exe (src/CMakeLists.txt); everything else goes into the DLL.
    const isLauncher = f === 'src/CrealityPrint_app_msvc.cpp' || f === 'src/platform/msw/CrealityPrint.rc.in';
    const against = isLauncher ? { name: 'MentorSlicer.exe', time: exeTime } : { name: 'MentorSlicer_Slicer.dll', time: dllTime };
    if (t.getTime() > against.time.getTime()) stale.push({ f, t, against });
  }
  if (stale.length) {
    stale.sort((a, b) => b.t - a.t);
    for (const s of stale.slice(0, 10)) fail(`build is stale: ${s.f} (${fmtTime(s.t)}) is newer than ${s.against.name} (${fmtTime(s.against.time)})`);
    if (stale.length > 10) fail(`build is stale: ${stale.length - 10} more input(s) newer than the build`);
  } else {
    ok(`build: MentorSlicer_Slicer.dll ${fmtTime(dllTime)} newer than all ${count} inputs (newest ${newest} ${newest ? hhmm(new Date(newestTime)) : ''})`);
  }
  const headTime = new Date(parseInt(gitOut(['log', '-1', '--format=%ct']), 10) * 1000);
  if (!stale.length && dllTime < headTime) warn(`build: DLL older than HEAD commit ${fmtTime(headTime)} (only docs/scripts changed after the build)`);

  // Italian catalogue: resources/i18n/it/*.mo against localization/i18n/it/*.po (rebuilt by build_exe_only.bat).
  const moDir = join(repo, 'resources', 'i18n', 'it'), poDir = join(repo, 'localization', 'i18n', 'it');
  const mos = existsSync(moDir) ? readdirSync(moDir).filter((f) => f.endsWith('.mo')) : [];
  const pos = existsSync(poDir) ? readdirSync(poDir).filter((f) => f.endsWith('.po')) : [];
  if (mos.length && pos.length) {
    const moTime = Math.max(...mos.map((f) => statSync(join(moDir, f)).mtime.getTime()));
    const poNewest = pos.map((f) => ({ f, t: statSync(join(poDir, f)).mtime.getTime() })).sort((a, b) => b.t - a.t)[0];
    if (poNewest.t > moTime) warn(`i18n: localization/i18n/it/${poNewest.f} (${fmtTime(new Date(poNewest.t))}) is newer than resources/i18n/it/*.mo (${fmtTime(new Date(moTime))}); the next build rebuilds the catalogue`);
    else info(`i18n: resources/i18n/it/${mos.join(',')} ${fmtTime(new Date(moTime))} newer than every localization/i18n/it/*.po`);
  } else info(`i18n: ${mos.length} .mo in resources/i18n/it, ${pos.length} .po in localization/i18n/it`);

  const procs = runningApp();
  const fromBuild = procs.filter((p) => p.path.toLowerCase().startsWith(buildDir.toLowerCase()));
  if (procs.length) info(`running: ${procs.map((p) => `${p.pid} ${p.path || '(path not readable)'}`).join('; ')}${fromBuild.length ? '' : ' (none from the build folder)'}`);
  else info('running: no MentorSlicer.exe');
  return { exe, dll, dllTime, buildDir, fromBuild, procs };
}

// ---- step 2: removed names ----
function parseRemovedList(file) {
  const entries = [];
  for (const raw of readFileSync(file, 'utf8').split(/\r?\n/)) {
    const l = raw.replace(/\s+#.*$/, '').trim();
    if (!l || l.startsWith('#')) continue;
    const m = /^(word|re|file|allow):(.+)$/.exec(l);
    if (!m) { entries.push({ kind: 'bad', value: raw }); continue; }
    entries.push({ kind: m[1], value: m[2].trim() });
  }
  return entries;
}

function stepRemoved(o) {
  heading('grep', 'Step 2: removed names are gone');
  const listFile = join(repo, 'tools', 'verify', `removed-${o.phase}.txt`);
  if (!existsSync(listFile)) {
    if (o.phase === 'F0') skip(`no ${rel(listFile)} (nothing is removed in F0)`);
    else fail(`missing ${rel(listFile)}: write the removed-names list at the start of the phase (tools/verify/README.md)`);
  } else {
    const entries = parseRemovedList(listFile);
    const allow = entries.filter((e) => e.kind === 'allow').map((e) => `:(exclude)${e.value}`);
    const excludes = ['.', ':(exclude)doc/**', ':(exclude)*.md', ':(exclude)**/*.md', ':(exclude)tools/verify/**', ':(exclude)tests/regression/**',
      ':(exclude)localization/i18n/*.po', ':(exclude)localization/i18n/*.pot', ':(exclude)localization/i18n/**/*.po', ':(exclude)clang-tidy/**', ...allow];
    let checked = 0;
    for (const e of entries) {
      if (e.kind === 'bad') { fail(`${rel(listFile)}: cannot parse line: ${e.value}`); continue; }
      if (e.kind === 'allow') continue;
      if (e.kind === 'file') {
        checked++;
        if (existsSync(join(repo, e.value))) fail(`file still exists: ${e.value}`);
        else ok(`file gone: ${e.value}`);
        continue;
      }
      checked++;
      const args = ['grep', '-n', '-I', '--untracked', ...(e.kind === 'word' ? ['-w', '-F'] : ['-E']), '-e', e.value, '--', ...excludes];
      const r = run('git', args);
      if (r.status === 1) ok(`${e.kind}: ${e.value} not found`);
      else if (r.status === 0) {
        const hits = r.stdout.split(/\r?\n/).filter(Boolean);
        fail(`${e.kind}: ${e.value} still present (${hits.length} match(es)):`);
        // Minified web bundles have lines of 2 MB: show file:line and a short excerpt around the match only.
        for (const h of hits.slice(0, 20)) {
          const m = /^(.*?:\d+:)(.*)$/.exec(h);
          let text = m ? m[2] : h;
          if (text.length > 200) {
            const at = e.kind === 'word' ? text.indexOf(e.value) : text.search(new RegExp(e.value));
            const start = Math.max(0, (at < 0 ? 0 : at) - 60);
            text = `${start > 0 ? '...' : ''}${text.slice(start, start + 200)}...`;
          }
          const shown = `       ${m ? m[1] : ''} ${text}`;
          report.push(shown); console.log(shown);
        }
      } else fail(`git grep error for ${e.value}: ${r.stderr.trim() || r.error?.message || `exit ${r.status}`}`);
    }
    info(`${rel(listFile)}: ${checked} entries checked, ${allow.length} allow path(s)`);
  }

  // localization/i18n/list.txt: every entry must exist.
  const listTxt = join(repo, 'localization', 'i18n', 'list.txt');
  if (!existsSync(listTxt)) fail('missing localization/i18n/list.txt');
  else {
    const entries = readFileSync(listTxt, 'utf8').split(/\r?\n/).map((l) => l.trim()).filter((l) => l && !l.startsWith('#'));
    const missing = entries.filter((e) => !existsSync(join(repo, e)));
    if (missing.length) { fail(`list.txt: ${missing.length} of ${entries.length} entries missing: ${missing.slice(0, 10).join(', ')}${missing.length > 10 ? ', ...' : ''}`); }
    else ok(`list.txt: ${entries.length} entries, all present`);
  }

  // CMake reference check: source names in src/**/CMakeLists.txt and src/**/*.cmake must exist.
  const cmakeFiles = gitLines(['ls-files', '--', 'src/**/CMakeLists.txt', 'src/**/*.cmake', 'src/CMakeLists.txt']);
  // A name is resolved against the folder of the CMake file and every ancestor up to the repository
  // root (an included .cmake keeps the CMAKE_CURRENT_SOURCE_DIR of the includer). Generated files
  // (configure_file, a .in template next to the name) and probe lines (if(EXISTS ...), find_*) are
  // skipped. A bare name (no folder part, prefixed later with list(TRANSFORM ... PREPEND) as in
  // src/tracy) is accepted when a source with that basename exists anywhere under src. A reference
  // whose folder exists nowhere in the tree is an external probe: info only.
  const srcBasenames = new Set([...gitLines(['ls-files', '--', 'src']), ...gitLines(['ls-files', '-o', '--exclude-standard', '--', 'src'])]
    .filter((f) => /\.(cpp|hpp|h)$/.test(f) && existsSync(join(repo, f))).map((f) => f.slice(f.lastIndexOf('/') + 1)));
  const broken = [], external = [];
  let refs = 0;
  // Lines that are not source lists: probes, generated files and add_custom_command arguments.
  const skipLine = /configure_file\s*\(|\bmessage\s*\(|\bif\s*\(|EXISTS|find_(path|file|library|package)\s*\(|\binclude\s*\(|\b(COMMAND|OUTPUT|BYPRODUCTS|DEPENDS)\b/;
  for (const cf of cmakeFiles) {
    const full = join(repo, cf);
    if (!existsSync(full)) continue;
    const roots = [];
    for (let d = dirname(full); ; d = dirname(d)) { roots.push(d); if (resolve(d) === resolve(repo) || dirname(d) === d) break; }
    const cmakeLines = readFileSync(full, 'utf8').split(/\r?\n/).map((raw) => raw.replace(/#.*$/, ''));
    // Files this CMake file generates itself (add_custom_command OUTPUT / BYPRODUCTS).
    const generated = new Set();
    for (const l of cmakeLines) if (/\b(OUTPUT|BYPRODUCTS)\b/.test(l)) for (const m of l.matchAll(/[\w][\w./-]*\.(?:cpp|hpp|h)(?![\w.])/g)) generated.add(m[0]);
    for (const l of cmakeLines) {
      if (skipLine.test(l)) continue;
      for (const m of l.matchAll(/(?<![\w}/$-])[\w][\w./-]*\.(?:cpp|hpp|h)(?![\w.])/g)) {
        const name = m[0];
        if (name.includes('${') || name.startsWith('.') || generated.has(name)) continue;
        refs++;
        if (roots.some((r) => existsSync(join(r, name)) || existsSync(join(r, `${name}.in`)))) continue;
        const folder = dirname(name);
        if (folder === '.' && srcBasenames.has(name)) continue;
        if (folder !== '.' && !roots.some((r) => existsSync(join(r, folder)))) external.push(`${cf}: ${name}`);
        else if (!broken.includes(`${cf}: ${name}`)) broken.push(`${cf}: ${name}`);
      }
    }
  }
  if (broken.length) { for (const b of broken.slice(0, 30)) fail(`CMake references a missing source: ${b}`); if (broken.length > 30) fail(`CMake: ${broken.length - 30} more missing references`); }
  else ok(`CMake: ${refs} source references in ${cmakeFiles.length} files, all present`);
  if (external.length) info(`CMake: ${external.length} reference(s) to folders outside the tree (external probes, not checked): ${[...new Set(external)].slice(0, 5).join('; ')}`);
}

// ---- step 3: G-code ----
function stepGcode(o, build) {
  heading('gcode', 'Step 3: G-code');
  const regDir = join(repo, 'tests', 'regression');
  const fixturesDir = join(regDir, 'fixtures');
  const baselineDir = join(regDir, 'baseline', 'F0');
  const outDir = join(regDir, 'out', o.phase);
  const manifestFile = join(baselineDir, 'manifest.json');
  const manifest = existsSync(manifestFile) ? JSON.parse(readFileSync(manifestFile, 'utf8')) : null;
  const fixtures = existsSync(fixturesDir) ? readdirSync(fixturesDir).filter((f) => f.toLowerCase().endsWith('.3mf')).sort().map((f) => f.replace(/\.3mf$/i, '')) : [];
  if (!fixtures.length) { skip(`no fixture in ${rel(fixturesDir)} (make them in the GUI: spec section 2.3)`); }

  // Fixtures the baseline marks as GUI exports cannot be sliced here.
  const guiFixtures = fixtures.filter((n) => manifest?.fixtures?.[n]?.source === 'gui');
  const cliFixtures = fixtures.filter((n) => !guiFixtures.includes(n));
  for (const n of guiFixtures) {
    info(`${n}: baseline source "gui". Export it by hand: File -> Apri progetto ${rel(join(fixturesDir, n + '.3mf'))}, Slice piatto, Esporta file G-code -> ${rel(join(outDir, n + '.gcode'))}`);
  }

  if (o.slice && cliFixtures.length) {
    const args = [join(regDir, 'slice-fixtures.mjs'), '--out', outDir, ...(o.exe ? ['--exe', o.exe] : []), ...(guiFixtures.length ? ['--only', cliFixtures.join(',')] : [])];
    info(`node ${args.map(rel).join(' ')}`);
    const r = run(process.execPath, args, { spawn: { stdio: ['ignore', 'inherit', 'inherit'] } });
    if (r.status === 0) ok(`slice-fixtures: ${cliFixtures.length} fixture(s) sliced into ${rel(outDir)}`);
    else if (r.status === 2) fail(`slice-fixtures: usage or environment error (exit 2)`);
    else {
      fail(`slice-fixtures: exit ${r.status}`);
      const sm = join(outDir, 'slice-manifest.json');
      if (existsSync(sm)) for (const [n, e] of Object.entries(JSON.parse(readFileSync(sm, 'utf8')).fixtures ?? {})) for (const err of e.errors ?? []) fail(`  ${n}: ${err}`);
    }
  } else if (o.slice) skip('slice-fixtures: nothing to slice');
  else if (fixtures.length) info(`no --slice: comparing what is already in ${rel(outDir)}`);

  // Fixture integrity against the baseline manifest.
  if (fixtures.length) {
    if (!manifest) {
      (o.phase === 'F0' ? skip : fail)(`no ${rel(manifestFile)}: ${o.phase === 'F0' ? 'written by runbook step 9' : 'regenerate the baseline at pre-lean'}`);
    } else {
      for (const n of fixtures) {
        const expected = manifest.fixtures?.[n]?.fixture_sha256;
        const actual = sha256File(join(fixturesDir, `${n}.3mf`));
        if (!expected) warn(`fixture ${n}: not in the baseline manifest`);
        else if (expected !== actual) fail(`fixture changed: ${n}.3mf (sha256 ${actual.slice(0, 12)} != baseline ${expected.slice(0, 12)}); regenerate the baseline at pre-lean, never at the candidate`);
        else ok(`fixture ${n}.3mf unchanged since the baseline`);
      }
    }
  }

  // compare-gcode in directory mode.
  const hasBaseline = existsSync(baselineDir) && readdirSync(baselineDir).some((f) => /\.gcode(\.gz)?$/i.test(f));
  const hasOut = existsSync(outDir) && readdirSync(outDir).some((f) => /\.gcode$/i.test(f));
  if (!hasBaseline) (o.phase === 'F0' ? skip : fail)(`no baseline G-code in ${rel(baselineDir)}${o.phase === 'F0' ? ' (runbook step 9)' : ''}`);
  else if (!hasOut) fail(`no candidate G-code in ${rel(outDir)} (run with --slice)`);
  else {
    const args = [join(regDir, 'compare-gcode.mjs'), '--baseline-dir', baselineDir, '--candidate-dir', outDir, '--phase', o.phase];
    info(`node ${args.map(rel).join(' ')}`);
    const r = run(process.execPath, args);
    const out = r.stdout.trim();
    for (const l of out.split(/\r?\n/)) { console.log(`     ${l}`); report.push(`     ${l}`); }
    const result = out.split(/\r?\n/).find((l) => l.startsWith('RESULT:')) ?? '';
    if (r.status === 0) ok(`compare-gcode: ${result}`);
    else fail(`compare-gcode exit ${r.status}: ${result || r.stderr.trim()}`);
  }

  // The existing trimmed fixture of tests/print-advisor.
  const checker = join(repo, 'tests', 'print-advisor', 'check-gcode.mjs');
  const checkRun = (args, label, expectedOk) => {
    const r = run(process.execPath, [checker, ...args]);
    const last = r.stdout.trim().split(/\r?\n/).filter((l) => /^(PASSED|FAILED):/.test(l)).pop() ?? '';
    if (r.status === 0) {
      const m = /PASSED: (\d+) ok/.exec(last);
      const n = m ? parseInt(m[1], 10) : null;
      if (expectedOk != null && n !== expectedOk) warn(`${label}: ${last} (baseline recorded ${expectedOk} ok)`);
      else ok(`${label}: ${last}`);
      return n;
    }
    fail(`${label}: exit ${r.status} ${last}`);
    for (const l of r.stdout.split(/\r?\n/).filter((l) => l.startsWith('FAIL')).slice(0, 15)) { console.log(`       ${l}`); report.push(`       ${l}`); }
    if (r.stderr.trim()) { console.log(`       ${r.stderr.trim()}`); report.push(`       ${r.stderr.trim()}`); }
    return null;
  };
  if (existsSync(checker)) {
    checkRun([], 'check-gcode (tests/print-advisor fixtures)', null);
    const expectedFiles = existsSync(fixturesDir) ? readdirSync(fixturesDir).filter((f) => f.endsWith('.expected.json')).sort() : [];
    for (const ef of expectedFiles) {
      const n = ef.replace(/\.expected\.json$/, '');
      const gcode = join(outDir, `${n}.gcode`);
      if (!existsSync(gcode)) { fail(`check-gcode ${n}: ${rel(gcode)} not found`); continue; }
      checkRun([gcode, '--expected', join(fixturesDir, ef)], `check-gcode ${n}`, manifest?.fixtures?.[n]?.check_ok ?? null);
    }
  } else skip(`no ${rel(checker)}`);
}

// ---- step 4: web patches ----
function stepWeb(o) {
  heading('web', 'Step 4: web patches');
  const script = join(repo, 'tools', 'web-patches', 'apply-web-patches.mjs');
  if (!existsSync(script)) { fail(`missing ${rel(script)}`); return; }
  const r = run(process.execPath, [script, '--check']);
  const out = (r.stdout + (r.stderr ? `\n${r.stderr}` : '')).trim();
  const outDir = join(repo, 'tests', 'regression', 'out', o.phase);
  mkdirSync(outDir, { recursive: true });
  writeFileSync(join(outDir, 'web-patches.txt'), out + '\n');
  const summary = out.split(/\r?\n/).find((l) => /\d+ applied, \d+ applicable, \d+ NOT FOUND, \d+ AMBIGUOUS/.test(l)) ?? '';
  const m = /(\d+) applied, (\d+) applicable, (\d+) NOT FOUND, (\d+) AMBIGUOUS/.exec(summary);
  if (r.status === 0 && m && m[3] === '0' && m[4] === '0') ok(`web patches: ${summary} (saved to ${rel(join(outDir, 'web-patches.txt'))})`);
  else fail(`web patches: exit ${r.status}, ${summary || out.split(/\r?\n/).pop()}`);
  if (m && m[2] !== '0') warn(`web patches: ${m[2]} applicable but not applied (resources/web differs from the patched state)`);
}

// ---- step 5: sidecar ----
// The descriptor of the MentorSlicer data folder (one folder since F8, no version subfolders).
function newestEndpoint() {
  if (!process.env.APPDATA) return null;
  const ep = join(process.env.APPDATA, 'MentorSlicer', 'cache', 'print-advisor', 'endpoint.json');
  return existsSync(ep) ? ep : null;
}

function stepSidecar(o, build) {
  heading('sidecar', 'Step 5: sidecar');
  const dir = join(repo, 'tools', 'print-advisor-mcp');
  if (!existsSync(join(dir, 'package.json'))) { fail(`missing ${rel(dir)}/package.json`); return; }
  if (!existsSync(join(dir, 'node_modules'))) { fail(`${rel(dir)}/node_modules missing: run npm install there first`); return; }
  const tc = run(npmCmd, ['run', 'typecheck', '--silent'], { cwd: dir, spawn: { shell: isWin } });
  const tcOut = (tc.stdout + tc.stderr).trim();
  if (tc.status === 0) ok(`typecheck: tsc --noEmit${tcOut ? ` (${tcOut.split(/\r?\n/).length} line(s) of output)` : ', no output'}`);
  else { fail(`typecheck: exit ${tc.status}`); for (const l of tcOut.split(/\r?\n/).slice(0, 20)) { console.log(`       ${l}`); report.push(`       ${l}`); } }

  if (o.skip.has('smoke')) { skip('smoke: skipped (--skip smoke)'); return; }
  const sm = run(npmCmd, ['run', 'smoke', '--silent'], { cwd: dir, spawn: { shell: isWin } });
  const out = (sm.stdout + (sm.stderr ? `\n${sm.stderr}` : '')).trim();
  const outDir = join(repo, 'tests', 'regression', 'out', o.phase);
  mkdirSync(outDir, { recursive: true });
  writeFileSync(join(outDir, 'smoke.txt'), out + '\n');
  const first = out.split(/\r?\n/).find((l) => l.startsWith('Print Advisor MCP smoke test')) ?? '';
  const last = out.split(/\r?\n/).filter((l) => /^(PASSED|FAILED):/.test(l)).pop() ?? '';
  const live = first.includes('(app running)');
  if (!live) {
    (o.requireLive ? fail : skip)(`smoke: ${first || 'no header line'} ${last}${o.requireLive ? ' (--require-live)' : ' (open the dev build and enable Integrazione AI -> Print Advisor MCP for a live smoke)'}`);
    if (sm.status !== 0 && !o.requireLive) fail(`smoke (offline part): exit ${sm.status} ${last}`);
    return;
  }
  if (sm.status === 0) ok(`smoke: ${first} ${last} (saved to ${rel(join(outDir, 'smoke.txt'))})`);
  else { fail(`smoke: exit ${sm.status} ${last}`); for (const l of out.split(/\r?\n/).filter((l) => l.startsWith('FAIL')).slice(0, 15)) { console.log(`       ${l}`); report.push(`       ${l}`); } }

  // Did the smoke talk to the dev build? The descriptor pid must be a MentorSlicer.exe under build\src\Release.
  const ep = process.env.MENTORSLICER_ADVISOR_DESCRIPTOR || newestEndpoint();
  if (!ep) { warn('smoke: endpoint.json not found, cannot tell which instance answered'); return; }
  let pid = null;
  try { pid = JSON.parse(readFileSync(ep, 'utf8')).pid ?? null; } catch { warn(`smoke: cannot parse ${ep}`); return; }
  const procs = build?.procs ?? runningApp();
  const buildDir = (build?.buildDir ?? join(repo, 'build', 'src', 'Release')).toLowerCase();
  const p = procs.find((x) => x.pid === pid);
  if (!p) warn(`smoke: descriptor pid ${pid} is not a running MentorSlicer.exe (stale endpoint.json?)`);
  else if (!p.path.toLowerCase().startsWith(buildDir)) warn(`smoke ran against the stable copy (pid ${pid}, ${p.path}); close it and keep only the dev build open`);
  else ok(`smoke: descriptor pid ${pid} is the dev build (${p.path})`);
}

// ---- step 6: manual checklist ----
async function stepManual(o, build) {
  heading('manual', 'Step 6: manual checklist');
  const text = [
    `CONTROLLO MANUALE (circa 3 minuti) - fase ${o.phase}, build ${build?.dllTime ? fmtTime(build.dllTime) : 'n/d'}`,
    '[ ] Avvio senza errori (nessuna finestra di errore, log senza "error" nuovi rispetto a F0)',
    '[ ] Prepara: carico un modello e faccio "Slice piatto"',
    '[ ] Anteprima: si vede il percorso, i layer scorrono',
    '[ ] Dispositivo: K1C collegata, telemetria (temperature, ventole) e telecamera',
    '[ ] Si apre la finestra di invio (senza inviare)',
    '[ ] Print Advisor attivo (Integrazione AI -> Print Advisor MCP -> Abilita) e smoke "app running" PASSED',
    '[ ] Si aprono le Preferenze',
    '[ ] Chiusura pulita (nessun processo MentorSlicer.exe/msedgewebview2.exe rimasto)',
    'Solo dopo F1, F3, F4: rete 5 minuti -> powershell -NoProfile -ExecutionPolicy Bypass -File tools\\net-watch.ps1 -Minutes 5',
    'Solo se la fase tocca Dispositivo o invio: doc/MCP/printer-test-checklist.md, a stampante ferma',
  ];
  for (const l of text) { console.log(l); report.push(l); }
  if (process.stdin.isTTY && process.stdout.isTTY) {
    const rl = createInterface({ input: process.stdin, output: process.stdout });
    const answer = await new Promise((res) => rl.question('Esito manuale registrato? (s/n) ', res));
    rl.close();
    const a = answer.trim().toLowerCase();
    report.push(`Esito manuale registrato: ${a === 's' || a === 'si' || a === 'sì' || a === 'y' ? 'si' : a === 'n' || a === 'no' ? 'no' : `"${answer.trim()}"`}`);
  } else report.push('Esito manuale registrato: (nessuna risposta, stdin non interattivo)');
}

// ---- main ----
async function main() {
  const o = parseArgs(process.argv.slice(2));
  const major = parseInt(process.versions.node.split('.')[0], 10);
  if (major < 20) { console.error(`error: Node 20+ required (found ${process.version})`); process.exit(2); }
  if (!existsSync(join(repo, 'CMakeLists.txt')) || !existsSync(join(repo, 'src'))) { console.error(`error: ${repo} does not look like the repository root`); process.exit(2); }
  try { git(['rev-parse', '--git-dir']); } catch (e) { console.error(`error: git not available: ${e.message}`); process.exit(2); }
  const reportFile = o.report ?? join(repo, 'tests', 'regression', 'out', o.phase, 'verify-report.txt');
  report.push(`verify-phase ${o.phase} - ${new Date().toISOString()} - ${process.argv.slice(2).join(' ')}`);

  let build = null;
  try {
    stepContext(o);
    if (!o.skip.has('build')) build = stepBuild(o); else { heading('build', 'Step 1: build is current'); skip('skipped (--skip build)'); }
    if (!o.skip.has('grep')) stepRemoved(o); else { heading('grep', 'Step 2: removed names are gone'); skip('skipped (--skip grep)'); }
    if (!o.skip.has('gcode')) stepGcode(o, build); else { heading('gcode', 'Step 3: G-code'); skip('skipped (--skip gcode)'); }
    if (!o.skip.has('web')) stepWeb(o); else { heading('web', 'Step 4: web patches'); skip('skipped (--skip web)'); }
    if (!o.skip.has('sidecar')) stepSidecar(o, build); else { heading('sidecar', 'Step 5: sidecar'); skip('skipped (--skip sidecar)'); }
    if (o.manual) await stepManual(o, build);
  } catch (e) {
    console.error(`error: ${e.stack || e.message}`);
    report.push(`error: ${e.message}`);
    mkdirSync(dirname(reportFile), { recursive: true });
    writeFileSync(reportFile, report.join('\n') + '\n');
    process.exit(2);
  }

  const last = counts.fail
    ? `VERIFY ${o.phase}: FAILED (${counts.fail} fail: ${[...failedSteps].join(', ')}; ${counts.ok} ok, ${counts.warn} warn, ${counts.skip} skip)`
    : `VERIFY ${o.phase}: PASSED (${counts.ok} ok, ${counts.warn} warn, ${counts.skip} skip)`;
  console.log(`\n${last}`);
  report.push('', last);
  mkdirSync(dirname(reportFile), { recursive: true });
  writeFileSync(reportFile, report.join('\n') + '\n');
  console.log(`report: ${reportFile}`);
  process.exit(counts.fail ? 1 : 0);
}

main();
