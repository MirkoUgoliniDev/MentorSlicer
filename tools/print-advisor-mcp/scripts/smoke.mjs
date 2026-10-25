#!/usr/bin/env node
// Smoke test of the Print Advisor MCP sidecar (npm run smoke).
//
// 1. Spawns the sidecar over stdio with the official MCP client SDK and compares the tool list
//    with scripts/tools-expected.json.
// 2. If MentorSlicer answers (descriptor present, process alive), calls ONLY read-only tools and
//    checks: success shape, compact JSON, response time, no secret-looking keys and no session
//    token anywhere in the results.
// 3. Without the app (or with --offline): checks the Italian "MentorSlicer non è aperto" error.
//
// SAFETY: this script never calls a tool that changes something. The allowlist below is the only
// set of tools it can call; any other name throws before the request is sent.
//
// Usage: npm run smoke [-- --offline] [-- --update-expected] [-- --exe <mentorslicer-mcp.exe>]
//   --exe tests the packaged executable instead of src/index.ts run with tsx.
//   MENTORSLICER_ADVISOR_DESCRIPTOR=<endpoint.json>  optional; otherwise
//   %APPDATA%\MentorSlicer\cache\print-advisor\endpoint.json is used.

import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..');
const expectedFile = join(here, 'tools-expected.json');
const args = new Set(process.argv.slice(2));
const offline = args.has('--offline');
const updateExpected = args.has('--update-expected');
const argList = process.argv.slice(2);
const exePath = argList.includes('--exe') ? argList[argList.indexOf('--exe') + 1] : undefined;

// Read-only tools the smoke test may call, with the maximum accepted response time (ms).
// GUI-thread routes (preflight, model analysis, overhangs) wait behind the GUI: wider limit.
const READ_ONLY = {
  get_print_context: 6000,
  get_parameter_schema: 6000,
  get_bed_type: 20000,
  get_slice_result: 6000,
  list_archived_prints: 16000,
  get_printer_status: 6000,
  list_profiles: 6000,
  get_change_history: 6000,
  get_preheat_status: 6000,
  get_monitor_status: 6000,
  list_print_logs: 6000,
  analyze_model: 20000,
  preflight_check: 20000,
  get_overhang_regions: 30000,
  explain_printer_error: 2000,
  get_material_guide: 2000,
  compute_calibration_result: 2000,
  diagnose_defect: 2000,
};
const SECRET_KEY = /printhost_|password|apikey|token|secret/i;
const NOT_RUNNING = 'MentorSlicer non è aperto'; // must match notRunning in src/appClient.ts

const failures = [];
const passes = [];
const fail = (msg) => { failures.push(msg); console.log(`FAIL ${msg}`); };
const pass = (msg) => { passes.push(msg); console.log(`ok   ${msg}`); };

function findDescriptor() {
  if (process.env.MENTORSLICER_ADVISOR_DESCRIPTOR) return process.env.MENTORSLICER_ADVISOR_DESCRIPTOR;
  // One data folder since F8 (no version subfolders).
  const file = join(process.env.APPDATA ?? '', 'MentorSlicer', 'cache', 'print-advisor', 'endpoint.json');
  return existsSync(file) ? file : undefined;
}

function processAlive(pid) {
  try { process.kill(pid, 0); return true; } catch (e) { return e.code === 'EPERM'; }
}

// Session token, kept only in memory to check it never leaks into a result. Never printed.
function readToken(file) {
  try { return JSON.parse(readFileSync(file, 'utf8')).token; } catch { return undefined; }
}

function appLooksAlive(file) {
  if (!file || !existsSync(file)) return false;
  try {
    const d = JSON.parse(readFileSync(file, 'utf8'));
    return typeof d.pid !== 'number' || processAlive(d.pid);
  } catch { return false; }
}

function secretKeys(value, path = '$', out = []) {
  if (Array.isArray(value)) value.forEach((v, i) => secretKeys(v, `${path}[${i}]`, out));
  else if (value && typeof value === 'object')
    for (const [k, v] of Object.entries(value)) {
      if (SECRET_KEY.test(k)) out.push(`${path}.${k}`);
      secretKeys(v, `${path}.${k}`, out);
    }
  return out;
}

const descriptor = offline
  ? join(root, 'scripts', '__missing_descriptor__.json')
  : findDescriptor();
const token = descriptor && !offline ? readToken(descriptor) : undefined;
const live = !offline && appLooksAlive(descriptor);

const transport = new StdioClientTransport({
  command: exePath ?? process.execPath,
  args: exePath ? [] : [join(root, 'node_modules', 'tsx', 'dist', 'cli.mjs'), join(root, 'src', 'index.ts')],
  cwd: root,
  env: { ...process.env, MENTORSLICER_ADVISOR_DESCRIPTOR: descriptor ?? join(root, 'scripts', '__missing_descriptor__.json') },
  stderr: 'pipe',
});
let stderrText = '';
transport.stderr?.on('data', (chunk) => { stderrText += chunk.toString(); });
const client = new Client({ name: 'print-advisor-smoke', version: '1.0.0' });

async function callReadOnly(name, toolArgs = {}) {
  if (!(name in READ_ONLY)) throw new Error(`smoke.mjs refuses to call ${name}: not in the read-only allowlist`);
  const started = Date.now();
  const result = await client.callTool({ name, arguments: toolArgs }, { timeout: READ_ONLY[name] + 5000 });
  const ms = Date.now() - started;
  const text = (result.content ?? []).filter((c) => c.type === 'text').map((c) => c.text).join('');
  return { result, text, ms };
}

// Checks one live read-only result; returns the parsed JSON (or undefined).
function checkLive(name, { result, text, ms }) {
  const label = `${name} (${ms} ms)`;
  if (result.isError) { fail(`${label}: isError: ${text.slice(0, 200)}`); return undefined; }
  let parsed;
  try { parsed = JSON.parse(text); } catch { fail(`${label}: result is not JSON`); return undefined; }
  const problems = [];
  if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) problems.push('not a JSON object');
  else if (parsed.success === false) problems.push(`success=false: ${String(parsed.error).slice(0, 200)}`);
  if (text.includes('\n')) problems.push('not compact JSON (contains newlines)');
  if (ms > READ_ONLY[name]) problems.push(`slow: ${ms} ms > ${READ_ONLY[name]} ms`);
  const keys = secretKeys(parsed);
  if (keys.length) problems.push(`secret-looking keys: ${keys.slice(0, 5).join(', ')}`);
  if (token && text.includes(token)) problems.push('session token found in the result');
  if (problems.length) fail(`${label}: ${problems.join('; ')}`);
  else pass(`${label}: ${text.length} chars`);
  return parsed;
}

async function main() {
  console.log(`Print Advisor MCP smoke test (${offline ? 'offline forced' : live ? 'app running' : 'app not running'})`);
  await client.connect(transport);
  pass(`sidecar connected over stdio (${client.getServerVersion()?.name ?? '?'} ${client.getServerVersion()?.version ?? ''})`);

  // Tool list vs committed expectation.
  const names = (await client.listTools()).tools.map((t) => t.name).sort();
  if (updateExpected) {
    writeFileSync(expectedFile, JSON.stringify({ count: names.length, tools: names }, null, 2) + '\n');
    console.log(`updated ${expectedFile}`);
  }
  const expected = JSON.parse(readFileSync(expectedFile, 'utf8'));
  const missing = expected.tools.filter((n) => !names.includes(n));
  const extra = names.filter((n) => !expected.tools.includes(n));
  for (const n of missing) console.log(`  - missing: ${n}`);
  for (const n of extra) console.log(`  + new:     ${n}`);
  if (missing.length || extra.length || names.length !== expected.count)
    fail(`tool list: ${names.length} tools, expected ${expected.count} (run with --update-expected after an intended change)`);
  else pass(`tool list: ${names.length} tools match tools-expected.json`);
  for (const n of Object.keys(READ_ONLY))
    if (!names.includes(n)) fail(`read-only tool ${n} is not exposed`);

  // Local tools: no app needed.
  const err = await callReadOnly('explain_printer_error', { code: 2565 });
  const errJson = (() => { try { return JSON.parse(err.text); } catch { return undefined; } })();
  if (err.result.isError || !errJson?.success || errJson.known !== true) fail(`explain_printer_error 2565: ${err.text.slice(0, 200)}`);
  else pass(`explain_printer_error 2565 (${err.ms} ms): known`);
  const guide = await callReadOnly('get_material_guide', { material: 'ASA' });
  if (guide.result.isError || !JSON.parse(guide.text).success) fail('get_material_guide ASA');
  else pass(`get_material_guide ASA (${guide.ms} ms)`);
  // Temperature tower: block 2 from 250 C is 250 - 2 * 5 = 240 C.
  const calib = await callReadOnly('compute_calibration_result', { test: 'temperature', start: 250, block_index: 2 });
  const calibJson = (() => { try { return JSON.parse(calib.text); } catch { return undefined; } })();
  if (calib.result.isError || calibJson?.value !== 240 || calibJson?.profile_key !== 'nozzle_temperature')
    fail(`compute_calibration_result temperature: ${calib.text.slice(0, 200)}`);
  else pass(`compute_calibration_result temperature (${calib.ms} ms): 240`);
  const defect = await callReadOnly('diagnose_defect', { defect: 'stringing' });
  const defectJson = (() => { try { return JSON.parse(defect.text); } catch { return undefined; } })();
  if (defect.result.isError || !Array.isArray(defectJson?.follow_up_questions) || defectJson.write_enabled !== false)
    fail(`diagnose_defect stringing: ${defect.text.slice(0, 200)}`);
  else pass(`diagnose_defect stringing (${defect.ms} ms)`);

  if (!live) {
    // Without the app every app-backed tool must return the Italian "not open" error.
    for (const name of ['get_print_context', 'get_printer_status', 'list_profiles']) {
      const r = await callReadOnly(name);
      if (r.result.isError && r.text.includes(NOT_RUNNING)) pass(`${name} without app (${r.ms} ms): "${NOT_RUNNING}…"`);
      else fail(`${name} without app: expected "${NOT_RUNNING}", got: ${r.text.slice(0, 200)}`);
      if (r.ms > 3000) fail(`${name} without app: slow (${r.ms} ms)`);
    }
    return;
  }

  // Live, read-only.
  const context = checkLive('get_print_context', await callReadOnly('get_print_context'));
  if (context) for (const key of ['context_id', 'presets', 'machine_limits', 'bed', 'editable_profile_values'])
    if (!(key in context)) fail(`get_print_context: missing key ${key}`);
  for (const name of ['get_printer_status', 'list_profiles', 'get_change_history', 'get_preheat_status',
    'get_monitor_status', 'list_print_logs', 'preflight_check', 'get_bed_type', 'get_slice_result', 'list_archived_prints'])
    checkLive(name, await callReadOnly(name));
  const schema = checkLive('get_parameter_schema', await callReadOnly('get_parameter_schema', { profile_class: 'filament', limit: 5 }));
  if (schema && !(Array.isArray(schema.options) && schema.options.length > 0)) fail('get_parameter_schema: no options');
  const model = checkLive('analyze_model', await callReadOnly('analyze_model'));
  const objects = Array.isArray(model?.objects) ? model.objects.length : 0;
  if (objects > 0)
    checkLive('get_overhang_regions', await callReadOnly('get_overhang_regions', { object_index: 0 }));
  else console.log('  skip get_overhang_regions: no object on the plate');
}

try {
  await main();
} catch (error) {
  fail(`unexpected error: ${error instanceof Error ? error.message : String(error)}`);
  if (stderrText) console.log(`sidecar stderr:\n${stderrText.slice(-2000)}`);
} finally {
  await client.close().catch(() => {});
}
console.log(`\n${failures.length ? 'FAILED' : 'PASSED'}: ${passes.length} ok, ${failures.length} failed`);
process.exit(failures.length ? 1 : 0);
