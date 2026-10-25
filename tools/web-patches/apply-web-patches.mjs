#!/usr/bin/env node
// Applies / checks / reverts the Print Advisor patches to the compiled
// Creality web bundles (doc/MCP/web-bundle-patches.md). Node 20+, no deps.
//
//   node tools/web-patches/apply-web-patches.mjs [--check|--apply|--revert]
//        [--root <dir>] [--backup-dir <dir>] [--only <id,id>]
//        [--accept-unmarked] [--all-or-nothing]
//
//   --check   (default) report every patch: applied / applicable / NOT FOUND /
//             AMBIGUOUS; exit code 1 if any patch is NOT FOUND or AMBIGUOUS.
//             Writes nothing.
//   --apply   apply what is applicable (idempotent; a patch is applied whole
//             or not at all, a patch whose `requires` are missing is refused);
//             exit code 1 if any patch is NOT FOUND or AMBIGUOUS, and then the
//             page may be PARTIALLY patched (see the summary).
//   --all-or-nothing  with --apply: write nothing unless every selected patch
//             is applied or applicable.
//   --accept-unmarked  code of a patch found WITHOUT its /*PA:..*/ marker
//             (written by hand, or upstream code that looks like it) counts as
//             applied. Without it such a patch is AMBIGUOUS (exit 1).
//   --revert  restore the bundles saved by --apply; refused (exit 1) when there
//             is no backup or the file changed after the last --apply.
//   --root    folder that contains resources/web (default: the repository).
//   --backup-dir  default tools/web-patches/.backup for the repository,
//             <root>/.web-patches-backup for any other --root; it may not be
//             <root> or inside <root>/resources/web.

import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { patches } from "./patches.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, "..", "..");

// true when `child` is `parent` or inside it (case-insensitive on Windows)
function inside(parent, child) {
  const r = path.relative(path.resolve(parent), path.resolve(child));
  return r === "" || (!r.startsWith("..") && !path.isAbsolute(r));
}

function parseArgs(argv) {
  const o = { mode: "check", root: repoRoot, backupDir: null, only: null, acceptUnmarked: false, allOrNothing: false };
  const value = (i, a) => {
    const v = argv[i];
    if (v === undefined || v.trim() === "" || v.startsWith("-")) throw new Error(`${a} needs a value`);
    return v;
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--check" || a === "--apply" || a === "--revert") o.mode = a.slice(2);
    else if (a === "--root") o.root = path.resolve(value(++i, a));
    else if (a === "--backup-dir") o.backupDir = path.resolve(value(++i, a));
    else if (a === "--only") {
      const ids = value(++i, a).split(",").map((s) => s.trim()).filter(Boolean);
      const unknown = ids.filter((id) => !patches.some((p) => p.id === id));
      if (!ids.length || unknown.length) throw new Error(`--only: unknown patch id(s) ${unknown.join(",") || "(none given)"}`);
      o.only = new Set(ids);
    }
    else if (a === "--accept-unmarked") o.acceptUnmarked = true;
    else if (a === "--all-or-nothing") o.allOrNothing = true;
    else if (a === "-h" || a === "--help") { o.mode = "help"; }
    else throw new Error(`unknown argument: ${a}`);
  }
  if (o.mode === "help") return o;
  if (!fs.existsSync(o.root) || !fs.statSync(o.root).isDirectory()) throw new Error(`--root ${o.root} is not a folder`);
  if (!o.backupDir)
    o.backupDir = path.resolve(o.root) === repoRoot
      ? path.join(here, ".backup")
      : path.join(o.root, ".web-patches-backup");
  o.webDir = path.join(o.root, "resources", "web");
  if (inside(o.backupDir, o.root) || inside(o.webDir, o.backupDir))
    throw new Error(`--backup-dir ${o.backupDir} must not be <root>, contain it, or be inside ${o.webDir}`);
  return o;
}

function listJs(dir) {
  const out = [];
  if (!fs.existsSync(dir)) return out;
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) out.push(...listJs(p));
    else if (e.isFile() && e.name.endsWith(".js")) out.push(p);
  }
  return out;
}

// Reads a bundle as text and refuses files that would not round-trip byte-exact.
function readText(file) {
  const buf = fs.readFileSync(file);
  const text = buf.toString("utf8");
  if (!Buffer.from(text, "utf8").equals(buf)) throw new Error(`${file} is not valid UTF-8`);
  return text;
}

// Locates each target file by content, once per (dir, fileContains).
function locateTargets(root, list) {
  const targets = new Map();
  for (const p of list) {
    const key = `${p.dir}|${p.fileContains}`;
    if (targets.has(key)) continue;
    const found = listJs(path.join(root, p.dir)).filter((f) => readText(f).includes(p.fileContains));
    targets.set(key, found.length === 1
      ? { file: found[0], error: null }
      : { file: null, error: found.length ? `${found.length} files contain ${p.fileContains}` : `no .js in ${p.dir} contains ${p.fileContains}` });
  }
  return targets;
}

function countMatches(text, re) {
  const g = new RegExp(re.source, re.flags.includes("g") ? re.flags : re.flags + "g");
  const ms = [];
  for (const m of text.matchAll(g)) { ms.push(m); if (ms.length > 1) break; }
  return ms;
}

// Captured names that equal a name declared by the inserted code.
function clashes(e, v) {
  const L = e.locals || [];
  if (Array.isArray(L)) {
    const caps = new Set(Object.values(v));
    return L.filter((l) => caps.has(l));
  }
  return Object.entries(L).filter(([k, names]) => v[k] !== undefined && names.includes(v[k])).map(([k]) => `${k}=${v[k]}`);
}

// Runs one patch on `text`. Returns { text, status, details }.
// status: "applied" (nothing to do), "applicable" (text changed), "notfound",
// "ambiguous" (code present without its marker, see --accept-unmarked).
function runPatch(p, text, o) {
  let cur = text, changed = 0, already = 0;
  const details = [];
  for (const [k, e] of p.edits.entries()) {
    const tag = p.edits.length > 1 ? `#${k + 1} ` : "";
    if (e.applied.test(cur)) {
      if (e.marker && !cur.includes(e.marker)) {
        if (!o.acceptUnmarked)
          return { text, status: "ambiguous", details: [`${tag}code present WITHOUT marker ${e.marker}: upstream code or a hand edit? check it, then use --accept-unmarked`] };
        details.push(`${tag}present without marker (accepted)`);
      }
      already++;
      continue;
    }
    const ms = countMatches(cur, e.find);
    if (ms.length !== 1)
      return { text, status: "notfound", details: [`${tag}${ms.length ? "anchor is ambiguous (more than one match)" : "anchor not found"}`] };
    const m = ms[0];
    try {
      const v = { ...(m.groups || {}), ...(e.vars ? e.vars(cur, m) : {}) };
      const clash = clashes(e, v);
      if (clash.length) throw new Error(`captured name clashes with inserted locals: ${clash.join(",")}`);
      const rep = e.replace(m, v);
      cur = cur.slice(0, m.index) + rep + cur.slice(m.index + m[0].length);
    } catch (err) {
      return { text, status: "notfound", details: [`${tag}${err.message}`] };
    }
    changed++;
  }
  if (!changed) return { text, status: "applied", details };
  if (already) details.push(`${already}/${p.edits.length} edits were already present`);
  return { text: cur, status: "applicable", details };
}

// The code of patch `p` is already in `text` (used for `requires` on patches
// that are not run in this invocation, e.g. with --only).
function presentIn(p, text, o) {
  return p.edits.every((e) => (e.marker ? text.includes(e.marker) || (o.acceptUnmarked && e.applied.test(text)) : e.applied.test(text)));
}

function rel(root, f) { return path.relative(root, f).split(path.sep).join("/"); }
function sha256(buf) { return crypto.createHash("sha256").update(buf).digest("hex"); }
function hashFile(f) { return sha256(fs.readFileSync(f)); }

function printTable(rows) {
  const head = ["Patch", "Status", "File", "Title / detail"];
  const w = head.map((h, i) => Math.max(h.length, ...rows.map((r) => r[i].length)));
  const line = (r) => r.map((c, i) => (i === 3 ? c : c.padEnd(w[i]))).join("  ");
  console.log(line(head));
  console.log(w.map((n, i) => "-".repeat(i === 3 ? Math.min(n, 60) : n)).join("  "));
  for (const r of rows) console.log(line(r));
}

const OK = new Set(["applied", "applicable"]);

function checkOrApply(o) {
  const list = patches.filter((p) => !o.only || o.only.has(p.id));
  const targets = locateTargets(o.root, patches);
  const tkey = (p) => `${p.dir}|${p.fileContains}`;
  const texts = new Map(), originals = new Map(), status = new Map(), fileOf = new Map();
  const load = (f) => { if (!texts.has(f)) { const s = readText(f); texts.set(f, s); originals.set(f, s); } return texts.get(f); };
  const rows = [];
  for (const p of list) {
    const t = targets.get(tkey(p));
    let res;
    if (!t.file) res = { status: "notfound", details: [t.error] };
    else {
      fileOf.set(p.id, t.file);
      // a required patch counts if it succeeded in this run or, when it is not
      // run (--only), if its marked code is already in the file
      const missing = (p.requires || []).filter((id) => {
        if (status.has(id)) return !OK.has(status.get(id));
        const rp = patches.find((x) => x.id === id), rt = rp && targets.get(tkey(rp));
        return !rt || !rt.file || !presentIn(rp, load(rt.file), o);
      });
      if (missing.length) res = { status: "notfound", details: [`requires patch ${missing.join(", ")} (not applied)`] };
      else {
        res = runPatch(p, load(t.file), o);
        if (res.status === "applicable") texts.set(t.file, res.text);
      }
    }
    status.set(p.id, res.status);
    const label = res.status === "applied" ? "applied"
      : res.status === "applicable" ? (o.mode === "apply" ? "APPLIED NOW" : "applicable")
      : res.status === "ambiguous" ? "AMBIGUOUS" : "NOT FOUND";
    rows.push([p.id, label, t.file ? rel(o.root, t.file) : p.dir, [p.title, ...res.details].join(" | ")]);
  }
  console.log(`Print Advisor web patches - ${o.mode} - root ${o.root}\n`);
  printTable(rows);

  const failed = [...status].filter(([, s]) => !OK.has(s)).map(([id]) => id);
  let failures = failed.length;
  if (o.mode === "apply") {
    const changedFiles = [...texts].filter(([f, t]) => t !== originals.get(f));
    if (o.allOrNothing && failed.length) {
      console.log(`\n--all-or-nothing: patch(es) ${failed.join(", ")} not applicable, NOTHING written`);
    } else {
      for (const [file, text] of changedFiles) {
        const orig = originals.get(file);
        const bak = path.join(o.backupDir, rel(o.root, file)), shaFile = bak + ".sha256";
        if (!inside(o.backupDir, bak) || inside(o.webDir, bak)) throw new Error(`backup path ${bak} is outside the backup folder`);
        // keep the first backup, unless the file on disk is a fresh upstream
        // bundle (none of our edits present): then the backup is renewed
        const fresh = !patches.some((p) => p.edits.some((e) => e.applied.test(orig)));
        if (fs.existsSync(bak) && !fresh && fs.existsSync(shaFile)
            && fs.readFileSync(shaFile, "utf8").trim() !== sha256(Buffer.from(orig, "utf8"))) {
          console.log(`\nSKIPPED ${rel(o.root, file)}: it changed after the last --apply, so ${bak} may not be its original.`
            + "\n  Check it, then move the backup away and run --apply again.");
          failures++;
          continue;
        }
        if (!fs.existsSync(bak) || fresh) {
          fs.mkdirSync(path.dirname(bak), { recursive: true });
          fs.writeFileSync(bak, orig, "utf8");
          console.log(`\nbackup: ${bak}`);
        }
        const out = Buffer.from(text, "utf8");
        fs.writeFileSync(file, out);
        fs.writeFileSync(shaFile, sha256(out) + "\n", "utf8");
        console.log(`written: ${rel(o.root, file)}`);
      }
      if (failed.length) {
        const byFile = new Map();
        for (const id of failed) {
          const f = fileOf.get(id);
          const k = f ? rel(o.root, f) : "(file not found)";
          byFile.set(k, [...(byFile.get(k) || []), id]);
        }
        console.log("\nWARNING: the pages may be PARTIALLY patched:");
        for (const [f, ids] of byFile) console.log(`  ${f}: patch(es) ${ids.join(", ")} NOT applied`);
        console.log("  Fix them (doc/MCP/web-bundle-patches.md) or --revert; --all-or-nothing writes nothing in this case.");
      }
    }
  }
  const n = (s) => [...status.values()].filter((x) => x === s).length;
  console.log(`\n${n("applied")} applied, ${n("applicable")} ${o.mode === "apply" ? "applied now" : "applicable"}, ${n("notfound")} NOT FOUND, ${n("ambiguous")} AMBIGUOUS`);
  if (n("notfound")) console.log("See doc/MCP/web-bundle-patches.md to port the NOT FOUND patches by hand.");
  if (n("ambiguous")) console.log("AMBIGUOUS: the code is there without our /*PA*/ marker; check it by hand, then --accept-unmarked.");
  return failures ? 1 : 0;
}

function revert(o) {
  const list = patches.filter((p) => !o.only || o.only.has(p.id));
  const targets = locateTargets(o.root, list);
  const seen = new Set();
  let restored = 0, failures = 0;
  for (const p of list) {
    const t = targets.get(`${p.dir}|${p.fileContains}`);
    if (!t.file) {
      if (!seen.has(t.error)) { seen.add(t.error); console.log(`cannot revert patch ${p.id}: ${t.error}`); failures++; }
      continue;
    }
    if (seen.has(t.file)) continue;
    seen.add(t.file);
    const name = rel(o.root, t.file);
    const bak = path.join(o.backupDir, name), shaFile = bak + ".sha256";
    if (!inside(o.backupDir, bak) || inside(o.webDir, bak) || path.resolve(bak) === path.resolve(t.file))
      throw new Error(`backup path ${bak} is outside the backup folder`);
    if (!fs.existsSync(bak)) { console.log(`no backup for ${name}`); failures++; continue; }
    if (!fs.existsSync(shaFile)) { console.log(`REFUSED ${name}: ${shaFile} is missing, cannot tell whether the backup belongs to it`); failures++; continue; }
    const want = fs.readFileSync(shaFile, "utf8").trim(), have = hashFile(t.file);
    if (want !== have) {
      console.log(`REFUSED ${name}: it changed after the last --apply (sha256 ${have.slice(0, 12)} != ${want.slice(0, 12)}), the backup may belong to another version`);
      failures++;
      continue;
    }
    fs.copyFileSync(bak, t.file);
    fs.rmSync(bak);
    fs.rmSync(shaFile);
    restored++;
    console.log(`restored: ${name}`);
  }
  console.log(`${restored} file(s) restored${failures ? `, ${failures} problem(s)` : ""}`);
  return failures ? 1 : 0;
}

try {
  const o = parseArgs(process.argv.slice(2));
  if (o.mode === "help") {
    console.log("usage: node tools/web-patches/apply-web-patches.mjs [--check|--apply|--revert] [--root <dir>] [--backup-dir <dir>] [--only <ids>] [--accept-unmarked] [--all-or-nothing]");
    process.exit(0);
  }
  process.exit(o.mode === "revert" ? revert(o) : checkOrApply(o));
} catch (err) {
  console.error(`ERROR: ${err.message}`);
  process.exit(2);
}
