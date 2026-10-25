#!/usr/bin/env node
// Merges gettext catalogues: the first file is the base (Creality's .po, its
// header is kept), every following file overrides entries with the same
// msgctxt+msgid and adds the new ones. Used by build_exe_only.bat before msgfmt
// so that our strings live in localization/i18n/it/PrintAdvisor_it.po.
//
//   node tools/web-patches/merge-po.mjs <out.po> <base.po> <overlay.po>...
//
// - `msgctxt ""` and no msgctxt are different keys (as for msgfmt);
// - an obsolete (#~) base entry with the key of a live entry is dropped, or
//   msgfmt would report a duplicate definition;
// - a fuzzy overlay entry does not replace a translated base entry (warning).
//
// Node 20+, no dependencies. Files must be UTF-8; the output uses the line
// ending of the base file.

import fs from "node:fs";

function parse(text) {
  const eol = text.includes("\r\n") ? "\r\n" : "\n";
  const blocks = text.replace(/^﻿/, "").split(/\r?\n(?:[ \t]*\r?\n)+/).filter((b) => b.trim() !== "");
  return { eol, entries: blocks.map(entry) };
}

// Key = [ \u0001 msgctxt ] \u0004 msgid (the raw quoted contents, concatenated).
function entry(block) {
  const lines = block.split(/\r?\n/);
  const f = {};
  let cur = null, obsolete = false, hasMsg = false, fuzzy = false;
  for (let l of lines) {
    if (l.startsWith("#~")) { obsolete = true; l = l.replace(/^#~\s?/, ""); }
    else if (l.startsWith("#,")) { if (/(^|[\s,])fuzzy([\s,]|$)/.test(l.slice(2))) fuzzy = true; continue; }
    else if (l.startsWith("#")) continue;
    const m = /^(msgctxt|msgid_plural|msgid|msgstr(?:\[\d+\])?)\s+"(.*)"\s*$/.exec(l);
    if (m) { cur = m[1]; f[cur] = m[2]; hasMsg = true; continue; }
    const c = /^"(.*)"\s*$/.exec(l);
    if (c && cur) f[cur] += c[1];
  }
  const key = hasMsg ? `${f.msgctxt === undefined ? "" : "\u0001" + f.msgctxt}\u0004${f.msgid ?? ""}` : null;
  const strs = Object.keys(f).filter((k) => k.startsWith("msgstr"));
  const translated = !fuzzy && strs.length > 0 && strs.every((k) => f[k] !== "");
  return { block, key, obsolete, fuzzy, translated, msgid: f.msgid ?? "",
    header: hasMsg && f.msgid === "" && f.msgctxt === undefined };
}

const [out, base, ...overlays] = process.argv.slice(2);
if (!out || !base || !overlays.length) {
  console.error("usage: node merge-po.mjs <out.po> <base.po> <overlay.po>...");
  process.exit(2);
}
const b = parse(fs.readFileSync(base, "utf8"));
const result = b.entries.slice();
const index = new Map();
result.forEach((e, i) => { if (e.key && !e.obsolete && !e.header) index.set(e.key, i); });
let replaced = 0, added = 0, kept = 0;
for (const file of overlays) {
  for (const e of parse(fs.readFileSync(file, "utf8")).entries) {
    if (!e.key || e.header || e.obsolete) continue;
    if (index.has(e.key)) {
      const old = result[index.get(e.key)];
      if (e.fuzzy && old.translated) {
        console.warn(`merge-po: WARNING ${file}: fuzzy entry "${e.msgid.slice(0, 60)}" ignored, the base translation is kept`);
        kept++;
        continue;
      }
      result[index.get(e.key)] = e; replaced++;
    }
    else { index.set(e.key, result.length); result.push(e); added++; }
  }
}
// obsolete (#~) entries stay at the end, as msgmerge writes them; the ones
// whose key is now live are dropped
const live = result.filter((e) => !e.obsolete);
const old = result.filter((e) => e.obsolete && !(e.key && index.has(e.key)));
const dropped = result.length - live.length - old.length;
const text = [...live, ...old].map((e) => e.block.split(/\r?\n/).join(b.eol)).join(b.eol + b.eol) + b.eol;
fs.writeFileSync(out, text, "utf8");
console.log(`merge-po: ${replaced} replaced, ${added} added, ${kept} fuzzy ignored, ${dropped} obsolete dropped -> ${out}`);
