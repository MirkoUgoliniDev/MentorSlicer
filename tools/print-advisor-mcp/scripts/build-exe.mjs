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

// Puts `source` at `target`, moving a running target out of the way first.
function replaceFile(source, target) {
  removeOldCopies(dirname(target));
  if (existsSync(target)) {
    try {
      rmSync(target);
    } catch {
      renameSync(target, `${target}.old-${Date.now()}`);
    }
  }
  copyFileSync(source, target);
}

async function build() {
  const sources = [join(root, 'src'), join(root, 'package-lock.json'), fileURLToPath(import.meta.url)];
  if (!force && existsSync(exe) && statSync(exe).mtimeMs > Math.max(...sources.map(newestMtime))) {
    console.log(`${exeName} is up to date.`);
    return;
  }

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

  // The node.exe signature does not survive the injection; Windows runs the file anyway.
  const staging = `${exe}.tmp`;
  copyFileSync(process.execPath, staging);
  await postject.inject(staging, 'NODE_SEA_BLOB', readFileSync(blob), {
    sentinelFuse: 'NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2',
    machoSegmentName: process.platform === 'darwin' ? 'NODE_SEA' : undefined,
  });
  replaceFile(staging, exe);
  rmSync(staging);
  console.log(`Built ${exe} (Node ${process.versions.node}).`);
}

try {
  await build();
  if (copyTo) {
    const target = join(resolve(copyTo), exeName);
    // copyFileSync keeps the modification time, so a different time means a different build.
    if (!existsSync(target) || statSync(target).mtimeMs !== statSync(exe).mtimeMs || statSync(target).size !== statSync(exe).size) {
      replaceFile(exe, target);
      console.log(`Copied to ${target}.`);
    } else {
      removeOldCopies(dirname(target));
    }
  }
} catch (error) {
  console.error(`build-exe: ${error instanceof Error ? error.message : error}`);
  process.exit(1);
}
