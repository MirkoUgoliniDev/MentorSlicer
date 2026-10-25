#!/usr/bin/env node
// Builds a small G-code fixture from a real MentorSlicer slice: header (without thumbnails),
// executable block up to the first `--layers` layers, and the full CONFIG block.
//
// Usage:
//   node tests/print-advisor/make-fixture.mjs [source.gcode] [--layers 5] [--out <file>]
// Without a source, the newest %LOCALAPPDATA%\Temp\mentorslicer_model\**\Metadata\*.gcode is used.
// After writing, run check-gcode.mjs on the result and update the expected JSON if needed.

import { existsSync, readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { basename, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const argv = process.argv.slice(2);
const option = (name, fallback) => {
  const i = argv.indexOf(name);
  if (i < 0) return fallback;
  const v = argv[i + 1];
  argv.splice(i, 2);
  return v;
};
const layers = Number(option('--layers', '5'));
const out = option('--out', join(here, 'fixtures', 'brim3-soak10.gcode'));

function newestSlice() {
  const base = join(process.env.LOCALAPPDATA ?? '', 'Temp', 'mentorslicer_model');
  const found = [];
  const walk = (dir, depth) => {
    if (depth > 4 || !existsSync(dir)) return;
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const p = join(dir, entry.name);
      if (entry.isDirectory()) walk(p, depth + 1);
      else if (entry.name.endsWith('.gcode') && basename(dir) === 'Metadata') found.push(p);
    }
  };
  walk(base, 0);
  return found.sort((a, b) => statSync(b).mtimeMs - statSync(a).mtimeMs)[0];
}

const source = argv[0] ?? newestSlice();
if (!source || !existsSync(source)) {
  console.error('No source G-code found. Slice a plate in MentorSlicer or pass a file.');
  process.exit(1);
}
const text = readFileSync(source, 'utf8');
const eol = text.includes('\r\n') ? '\r\n' : '\n';
const lines = text.split(/\r?\n/);
const configStart = lines.findIndex((l) => l.startsWith('; CONFIG_BLOCK_START'));
const configEnd = lines.findIndex((l) => l.startsWith('; CONFIG_BLOCK_END'));
if (configStart < 0 || configEnd < configStart) {
  console.error('The source has no CONFIG block.');
  process.exit(1);
}
const totalLayers = lines.filter((l) => l === ';LAYER_CHANGE').length;
let seen = 0;
let cut = configStart;
for (let i = 0; i < configStart; ++i)
  if (lines[i] === ';LAYER_CHANGE' && ++seen > layers) { cut = i; break; }

const kept = [];
let inThumb = false;
for (let i = 0; i < cut; ++i) {
  const l = lines[i];
  if (l.startsWith('; THUMBNAIL_BLOCK_START')) inThumb = true;
  if (!inThumb) kept.push(l);
  if (l.startsWith('; THUMBNAIL_BLOCK_END')) inThumb = false;
}
kept.push(`; PRINT_ADVISOR_FIXTURE: trimmed after ${Math.min(layers, totalLayers)} of ${totalLayers} layers`);
kept.push(...lines.slice(configStart, configEnd + 1));
writeFileSync(out, kept.join(eol) + eol);
console.log(`${out}: ${kept.length} lines, ${Math.min(layers, totalLayers)} of ${totalLayers} layers, from ${source}`);
