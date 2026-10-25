#!/usr/bin/env node
// Builds mentorslicer-mcp.exe: the MCP sidecar as a single executable (Node.js single executable
// application), so the user does not need Node.js or npm.
//
//   node scripts/build-exe.mjs [--copy-to <folder>] [--force]
//
// 1. bundles src/index.ts and its dependencies into dist/mentorslicer-mcp.cjs (esbuild);
// 2. turns it into a SEA blob and injects it into a copy of the running node.exe (postject);
// 3. with --copy-to, copies the executable next to MentorSlicer.exe.
// Skipped when dist/mentorslicer-mcp.exe is newer than the sources (unless --force).
// A copy that is running (an MCP client keeps it open) cannot be overwritten on Windows, but it can be
// renamed: the old file becomes mentorslicer-mcp.exe.old-<time> and is deleted by a later run.

import { execFileSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, readdirSync, readFileSync, renameSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const dist = join(root, 'dist');
const exeName = process.platform === 'win32' ? 'mentorslicer-mcp.exe' : 'mentorslicer-mcp';
const exe = join(dist, exeName);

const argv = process.argv.slice(2);
const force = argv.includes('--force');
const copyIndex = argv.indexOf('--copy-to');
const copyTo = copyIndex >= 0 ? argv[copyIndex + 1] : undefined;
if (copyIndex >= 0 && !copyTo) {
  console.error('--copy-to needs a folder');
  process.exit(2);
}

function newestMtime(path) {
  const stat = statSync(path);
  if (!stat.isDirectory()) return stat.mtimeMs;
  let newest = 0;
  for (const entry of readdirSync(path)) newest = Math.max(newest, newestMtime(join(path, entry)));
  return newest;
}

// Deletes leftovers of earlier replacements (still running ones stay until the next run).
function removeOldCopies(folder) {
  if (!existsSync(folder)) return;
  for (const entry of readdirSync(folder)) {
    if (entry.startsWith(`${exeName}.old-`)) {
      try { rmSync(join(folder, entry)); } catch { /* still in use */ }
    }
  }
}

// Removes `target`; a running copy is renamed out of the way instead.
function removeFile(target) {
  if (!existsSync(target)) return;
  try {
    rmSync(target);
  } catch {
    renameSync(target, `${target}.old-${Date.now()}`);
  }
}

// Puts `source` at `target`, moving a running target out of the way first.
function replaceFile(source, target) {
  removeOldCopies(dirname(target));
  removeFile(target);
  copyFileSync(source, target);
}

// node_modules must match package-lock.json (e.g. after a dependency update): npm ci when the
// lock file is newer than the one npm wrote into node_modules.
function ensureDependencies() {
  const installed = join(root, 'node_modules', '.package-lock.json');
  if (existsSync(installed) && statSync(installed).mtimeMs >= statSync(join(root, 'package-lock.json')).mtimeMs) return;
  console.log('Installing the sidecar packages (npm ci)...');
  // npm is a .cmd script on Windows: run it through the shell.
  execFileSync(process.platform === 'win32' ? 'npm.cmd' : 'npm', ['ci', '--no-audit', '--no-fund'],
    { cwd: root, stdio: 'inherit', shell: process.platform === 'win32' });
}

// signtool of the newest Windows SDK, if installed (used only to strip node.exe's signature).
function findSigntool() {
  if (process.platform !== 'win32') return undefined;
  const kits = join(process.env['ProgramFiles(x86)'] ?? 'C:\\Program Files (x86)', 'Windows Kits', '10', 'bin');
  if (!existsSync(kits)) return undefined;
  const versions = readdirSync(kits).filter((d) => /^\d+\./.test(d)).sort((a, b) =>
    a.split('.').map(Number).reduce((r, v, i) => r || v - (b.split('.').map(Number)[i] ?? 0), 0)).reverse();
  for (const v of versions) {
    const tool = join(kits, v, 'x64', 'signtool.exe');
    if (existsSync(tool)) return tool;
  }
  return undefined;
}

async function build() {
  // The Node version is part of the build: a Node update (security fixes) rebuilds the executable.
  const versionFile = join(dist, 'node-version.txt');
  const sameNode = existsSync(versionFile) && readFileSync(versionFile, 'utf8').trim() === process.versions.node;
  const sources = [join(root, 'src'), join(root, 'package.json'), join(root, 'package-lock.json'), fileURLToPath(import.meta.url)];
  if (!force && sameNode && existsSync(exe) && statSync(exe).mtimeMs > Math.max(...sources.map(newestMtime))) {
    console.log(`${exeName} is up to date.`);
    return;
  }

  ensureDependencies();
  let esbuild, postject;
  try {
    esbuild = await import('esbuild');
    postject = (await import('postject')).default;
  } catch {
    throw new Error(`esbuild/postject missing: run "npm install" in ${root}`);
  }

  mkdirSync(dist, { recursive: true });
  const bundle = join(dist, 'mentorslicer-mcp.cjs');
  await esbuild.build({
    entryPoints: [join(root, 'src', 'index.ts')],
    bundle: true,
    platform: 'node',
    format: 'cjs',
    target: 'node20',
    outfile: bundle,
    legalComments: 'none',
    logLevel: 'warning',
  });

  const blob = join(dist, 'sea-prep.blob');
  const config = join(dist, 'sea-config.json');
  writeFileSync(config, JSON.stringify({
    main: bundle,
    output: blob,
    disableExperimentalSEAWarning: true,
    useSnapshot: false,
    useCodeCache: false,
  }, null, 2));
  execFileSync(process.execPath, ['--experimental-sea-config', config], { stdio: 'inherit' });

  // The node.exe signature does not survive the injection. An invalid signature looks worse to
  // antivirus and SmartScreen than none, so it is removed first when signtool is available.
  const staging = `${exe}.tmp`;
  try {
    copyFileSync(process.execPath, staging);
    const signtool = findSigntool();
    if (signtool) execFileSync(signtool, ['remove', '/s', staging], { stdio: 'inherit' });
    else console.warn('build-exe: signtool not found, the executable keeps the invalid node.exe signature.');
    await postject.inject(staging, 'NODE_SEA_BLOB', readFileSync(blob), {
      sentinelFuse: 'NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2',
      machoSegmentName: process.platform === 'darwin' ? 'NODE_SEA' : undefined,
    });
    replaceFile(staging, exe);
  } finally {
    rmSync(staging, { force: true });
  }
  writeFileSync(versionFile, process.versions.node);
  console.log(`Built ${exe} (Node ${process.versions.node}).`);
}

try {
  await build();
  if (copyTo) {
    const target = join(resolve(copyTo), exeName);
    // On Windows copyFileSync keeps the modification time, so a different time means a different build.
    if (!existsSync(target) || statSync(target).mtimeMs !== statSync(exe).mtimeMs || statSync(target).size !== statSync(exe).size) {
      replaceFile(exe, target);
      console.log(`Copied to ${target}.`);
    } else {
      removeOldCopies(dirname(target));
    }
  }
} catch (error) {
  console.error(`build-exe: ${error instanceof Error ? error.message : error}`);
  // Never leave an older executable next to MentorSlicer.exe: publish-stable and make-release
  // stop on a missing file instead of shipping a sidecar that may not match the app.
  if (copyTo) removeFile(join(resolve(copyTo), exeName));
  process.exit(1);
}
