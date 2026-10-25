import type { McpServer } from '@modelcontextprotocol/server';
import * as z from 'zod/v4';
import { confirmationNote, errorResult, jsonResult, mutationTimeoutMs, readTimeoutMs, requestApp, slowReadTimeoutMs } from '../appClient.js';
import { k1cExhaustNote } from './device.js';

type Json = Record<string, unknown>;

// Log names come from list_print_logs; the app also refuses anything that is not in its log folder.
const logNameSchema = z.string().min(1).max(200).regex(/^[^/\\:]+$/).refine((name) => !name.includes('..'), 'invalid log name')
  .describe('Log name exactly as returned by list_print_logs');

const num = (value: unknown): number | null => (typeof value === 'number' && Number.isFinite(value) ? value : null);
const round1 = (value: number | null) => (value === null ? null : Math.round(value * 10) / 10);

// Known outcomes on the user's Creality K1C, partly recorded before the print logs existed. Returned
// only when the selected printer is a K1C. result uses the record_print_verdict values.
const knownK1cAsaPrints = [
  { date: null, result: 'detach', part: 'first ASA test print', bed_surface: 'smooth PEI, no glue', max_chamber_c: 37,
    notes: 'Detached at 48%. Rear exhaust (case) fan at 100% during the print, chamber about 37 C.' },
  { date: '2026-10-18', result: 'ok', part: '20 mm calibration cube', bed_surface: 'smooth PEI, no glue', max_chamber_c: 50,
    notes: 'Small part. Chamber reached about 50 C (the K1C sensor reads 5-10 C high).' },
  { date: '2026-10-19', result: 'detach', part: '130 mm boss (130x130x40 mm)', bed_surface: 'smooth PEI, no glue', room_c: 16, chamber_range_c: '33-38',
    notes: 'Cold room; the firmware re-enabled the rear exhaust whenever the chamber reached about 37 C; the part lifted off the plate and the print was stopped at 57%.' },
  { date: '2026-10-19', result: 'ok', part: '130 mm boss (same part)', bed_surface: 'smooth PEI WITH glue stick', chamber_range_c: '33-37',
    notes: 'Printed well with glue stick on smooth PEI, although the firmware still cycled the exhaust and the chamber stayed at 33-37 C. Glue stick on smooth PEI was decisive for large ASA parts.' },
];

// get_print_log returns at most this many events and verdicts (the newest ones).
const maxLogItems = 200;

function capLogLists(log: Json): Json {
  const result: Json = { ...log };
  for (const key of ['events', 'verdicts']) {
    const list = result[key];
    if (Array.isArray(list) && list.length > maxLogItems) {
      result[key] = list.slice(-maxLogItems);
      result[`${key}_truncated`] = { total: list.length, returned: maxLogItems, kept: 'newest' };
    }
  }
  return result;
}

// Material families, matched on the name prefix so variants count too (ABS+, ASA-CF, ASA Aero,
// ABS-GF, PA6-CF, PA-GF, PC-CF, PPA-CF...). Shrink-prone: ASA, ABS, PA/PAHT, PPA, PPS and PC, but
// not PCTG (a PETG-like copolyester); PETG and PLA do not match.
const isShrinkProne = (material: string) => /^(ASA|ABS|PA|PPA|PPS|PC(?!TG))/i.test(material.trim());
// ASA/ABS family: glue and K1C outcome hints, drying and retraction advice for stringing.
const isAsaAbsFamily = (material: string) => /^(ASA|ABS)/i.test(material.trim());
const isAsaFamily = (material: string) => /^ASA/i.test(material.trim());

// suggest_recipe reads many logs: each call is short and the whole scan has a deadline.
const recipeCallTimeoutMs = readTimeoutMs;
const recipeDeadlineMs = 30000;

// True when the printer profile or a connected LAN printer is a K1C. Failures count as unknown.
async function printerIsK1c(): Promise<boolean | null> {
  const isK1c = (value: unknown) => typeof value === 'string' && /(^|[^A-Za-z0-9])K1C([^A-Za-z0-9]|$)/i.test(value);
  const [context, status] = await Promise.allSettled([
    requestApp<{ machine?: Json }>('/v1/context', undefined, recipeCallTimeoutMs),
    requestApp<{ devices?: Json[] }>('/v1/printer_status', undefined, recipeCallTimeoutMs),
  ]);
  if (context.status === 'fulfilled' && typeof context.value.machine?.printer_model === 'string')
    return isK1c(context.value.machine.printer_model);
  if (status.status === 'fulfilled' && Array.isArray(status.value.devices) && status.value.devices.length)
    return status.value.devices.some((device) => isK1c(device.model) || isK1c(device.modelName));
  return null;
}

function matchesMaterial(header: Json, material: string): boolean {
  const pattern = new RegExp(`(^|[^A-Za-z])${material.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}([^A-Za-z]|$)`, 'i');
  const fields = [header.filament, header.process, header.file].flat().filter((value): value is string => typeof value === 'string');
  return fields.some((value) => pattern.test(value));
}

export function registerMonitorTools(server: McpServer) {
  server.registerTool('preheat_chamber', {
    description: `Warm the chamber of an enclosed printer before printing (ASA, ABS): heats the bed of the idle printer selected in MentorSlicer and returns immediately; MentorSlicer then tracks the chamber sensor in the background. Call get_preheat_status every few minutes instead of waiting in this call. Only one preheat is followed at a time: a preheat on another printer is refused while the first one is still followed (running or waiting for its automatic switch-off); end it first. The preheat never leaves the bed hot indefinitely: if the printer is still idle, MentorSlicer switches the bed OFF by itself (no dialog, a safety action) after max_minutes, 20 minutes after the chamber reaches target_chamber_temp or stops rising (plateau: less than 1 C over 10 minutes, judged from 15 minutes on), and when Print Advisor or MentorSlicer stops. If the telemetry is lost the preheat ends with outcome telemetry_lost and the bed MAY STILL BE HOT: the switch-off is sent when the telemetry returns, so tell the user to check the printer (or reopen the Device tab). If the user starts a print, the monitor stops and the bed stays as the print needs. bed_temp 0 ends the preheat and turns the bed off. The bed command is sent asynchronously through the Device page: success here means it was handed over, not that the printer accepted it; confirm with get_printer_status (print_control_results.set_bed_temp with the request_id returned here, and targetBedTemp0). If the page reports that a preheat start failed, the preheat is no longer followed (get_preheat_status outcome command_failed); if a switch-off (bed_temp 0) failed, MentorSlicer sends it directly to the printer, or as soon as the printer is idle, and notifies the user. The user starts the print; the Print Advisor never starts a print. ${confirmationNote} Measured on the K1C: with the bed alone the chamber settles at 35-37 C after 5-6 minutes; the K1-series chamber sensor can read 5-10 C high.`,
    inputSchema: z.object({
      bed_temp: z.number().int().min(0).max(120).describe('Bed temperature in °C (0 = end the preheat and switch the bed off); must not exceed the printer maximum (K1C: 100 °C)'),
      target_chamber_temp: z.number().min(20).max(70).optional().describe('Chamber temperature in °C at which the preheat is considered done (the bed is switched off 20 min later if no print starts)'),
      max_minutes: z.number().int().min(5).max(120).optional().describe('Maximum preheat duration in minutes (5-120, default 60); after it the bed is switched off if the printer is still idle'),
    }),
  }, async ({ bed_temp, target_chamber_temp, max_minutes }) => {
    try {
      const started = await requestApp<Json>('/v1/preheat', {
        bed_temp,
        ...(target_chamber_temp !== undefined && bed_temp > 0 ? { target_chamber_temp } : {}),
        ...(max_minutes !== undefined && bed_temp > 0 ? { max_minutes } : {}),
      });
      return jsonResult({
        ...started,
        next_step: started.success === false ? null
          : bed_temp === 0 ? 'Preheat ended; the bed switch-off was handed to the Device page: confirm targetBedTemp0 = 0 with get_printer_status.' : 'Call get_preheat_status in 3-5 minutes to read chamber, bed, rise over the last 5 minutes and the outcome. The bed is switched off automatically if no print starts.',
      });
    } catch (error) { return errorResult(error, 'Unable to preheat the chamber.'); }
  });

  server.registerTool('get_preheat_status', {
    description: 'State of the current or last chamber preheat started with preheat_chamber: active, printer address, start/end time (ms), chamber baseline, bed and chamber targets, current chamber and bed temperatures, chamber rise over the last 5 minutes, readings, and outcome: running; target = chamber target reached; plateau = chamber rose less than 1 C in 10 minutes, checked from minute 15 (both keep the bed hot for 20 more minutes); idle_after_target / idle_after_plateau = no print started within 20 minutes, bed switched off; timeout = max_minutes passed, bed switched off; print_started = a print started, the bed stays as the print needs; command_failed = the Device page refused the bed command, the preheat is not followed (auto_off stays pending only if the printer still reports a bed setpoint); telemetry_lost = no printer data, the bed may still be hot (ask the user to check); cancelled, replaced, stopped (bed switched off when Print Advisor stopped), none. On plateau below the target, suggest checking the lid and the exhaust fan or starting the print. Read-only.',
    inputSchema: z.object({}),
  }, async () => {
    try { return jsonResult(await requestApp<Json>('/v1/preheat_status')); }
    catch (error) { return errorResult(error, 'Unable to read the preheat status.'); }
  });

  server.registerTool('get_monitor_status', {
    description: 'State of the MentorSlicer background monitor: exhaust guard (armed or not, for which print), slow cooldown progress, chamber preheat, per-printer state with the name of the open print log, and the last monitor events (exhaust re-enabled by the firmware, bed drop after soak, filament runout). Read-only.',
    inputSchema: z.object({}),
  }, async () => {
    try { return jsonResult(await requestApp<Json>('/v1/monitor_status')); }
    catch (error) { return errorResult(error, 'Unable to read the monitor status.'); }
  });

  server.registerTool('set_exhaust_guard', {
    description: `Arm or disarm the exhaust guard for the print in progress on the selected printer. Some firmwares switch the rear exhaust fan back on during the print, which cools the chamber and warps ASA/ABS; when armed, MentorSlicer re-sends exhaust off (fanCase 0) each time the firmware turns it on during that print. ${k1cExhaustNote} So on the K1C the guard does not keep the exhaust off: it keeps sending fanCase 0, which the firmware ignores, and records the re-enables in the print log; after 6 consecutive ignored sends it slows down to one send every 2 minutes (get_monitor_status: ignored_sends, slowed_down) and returns to the normal rate when the exhaust goes off. Arming requires the active filament profile to have activate_air_filtration=1 and during_print_exhaust_fan_speed=0, a print running or paused, and a known print identity (printStartTime and file name). ONE in-app confirmation covers all the automatic re-sends of that print, without further dialogs. The guard disarms itself when the print ends, when a different print is detected (outcome new_print), after a telemetry gap longer than 2 minutes, and it pauses (stops forcing the exhaust off) when the chamber reaches its ceiling, maxBoxTemp - 5 C (55 C on the K1C). enabled=false disarms it immediately. Follow it with get_monitor_status. ${confirmationNote}`,
    inputSchema: z.object({ enabled: z.boolean() }),
  }, async ({ enabled }) => {
    try { return jsonResult(await requestApp<Json>('/v1/exhaust_guard', { enabled })); }
    catch (error) { return errorResult(error, 'Unable to change the exhaust guard.'); }
  });

  server.registerTool('slow_cooldown', {
    description: `Cool the bed slowly after a print to reduce warping and stress (ASA, ABS, PC): starting at "from" °C, the bed target is lowered by step_c every minutes_per_step; the cooldown ends at "to" °C and the bed then stays at "to" (to=0 switches the bed off at the end). ONE in-app confirmation covers all the steps, which are sent without further dialogs. "from" must not exceed the current bed temperature or target by more than 5 °C (a cooldown never heats the bed); this is checked again after the confirmation. Only when the printer is idle or the print is complete, failed or aborted. The cooldown stops by itself if a print starts, if the bed target is changed outside Print Advisor (outcome external_change) or if telemetry is lost; then the bed keeps the last target sent. Use {cancel: true} to stop a running cooldown. Follow it with get_monitor_status. ${confirmationNote}`,
    inputSchema: z.object({
      from: z.number().int().min(0).max(120).optional().describe('Starting bed temperature in °C'),
      to: z.number().int().min(0).max(120).optional().describe('Final bed temperature in °C, lower than from'),
      step_c: z.number().int().min(1).max(50).default(10).describe('Decrease per step in °C'),
      minutes_per_step: z.number().int().min(1).max(60).default(5),
      cancel: z.boolean().optional().describe('true = stop the running cooldown; the other fields are ignored'),
    }),
  }, async ({ from, to, step_c, minutes_per_step, cancel }) => {
    try {
      if (cancel) return jsonResult(await requestApp<Json>('/v1/slow_cooldown', { cancel: true }));
      if (from === undefined || to === undefined)
        return jsonResult({ success: false, error: 'from and to are required unless cancel is true' });
      if (to >= from)
        return jsonResult({ success: false, error: 'to must be lower than from' });
      return jsonResult(await requestApp<Json>('/v1/slow_cooldown', { from, to, step_c, minutes_per_step }));
    } catch (error) { return errorResult(error, 'Unable to start the slow cooldown.'); }
  });

  server.registerTool('list_print_logs', {
    description: 'List the print logs recorded by MentorSlicer (newest first): name, start time (ms), result (complete, failed, aborted, lost, monitor_stopped, running, unknown), G-code file, printer address and the user verdict if recorded. Use get_print_log for details. Read-only.',
    inputSchema: z.object({}),
  }, async () => {
    try { return jsonResult(await requestApp<Json>('/v1/print_logs')); }
    catch (error) { return errorResult(error, 'Unable to list the print logs.'); }
  });

  server.registerTool('get_print_log', {
    description: `Read one print log: header (file, printer, filament/process profiles, soak seconds, joined_mid_print), evenly sampled temperature/fan points (max_points, default 200, at most 500), total_points, events (exhaust_reenabled, bed_drop_after_soak, filament_runout), summary (result complete|failed|aborted|lost|monitor_stopped, duration_s, start_chamber, max_chamber, min_chamber_after_start, exhaust_reenables, bed_drop, runout, joined_mid_print) and the recorded verdicts. Only the last ${maxLogItems} events and the last ${maxLogItems} verdicts are returned; events_truncated / verdicts_truncated give the totals when more exist. Temperatures in °C. Read-only.`,
    inputSchema: z.object({
      name: logNameSchema,
      max_points: z.number().int().min(2).max(500).optional(),
    }),
  }, async ({ name, max_points }) => {
    try { return jsonResult(capLogLists(await requestApp<Json>('/v1/print_log', { name, ...(max_points ? { max_points } : {}) }, slowReadTimeoutMs))); }
    catch (error) { return errorResult(error, 'Unable to read the print log.'); }
  });

  server.registerTool('record_print_verdict', {
    description: 'Record how a finished print turned out, as told by the user (ok, warp, detach, stringing, under_extrusion, other) with an optional note, in its print log. suggest_recipe uses these verdicts. Only appends to the log; nothing is sent to the printer. One verdict per print: a second verdict on the same log (also one already given in the end-of-print dialog) is refused with HTTP 400. Refused with HTTP 409 while a dialog is open in MentorSlicer (for example the end-of-print verdict dialog, which records the same verdict itself): do not retry on your own. If the call times out, check with get_print_log before recording again.',
    inputSchema: z.object({
      name: logNameSchema,
      verdict: z.enum(['ok', 'warp', 'detach', 'stringing', 'under_extrusion', 'other']),
      note: z.string().max(500).optional(),
    }),
  }, async ({ name, verdict, note }) => {
    try { return jsonResult(await requestApp<Json>('/v1/print_verdict', { name, verdict, ...(note ? { note } : {}) }, mutationTimeoutMs)); }
    catch (error) { return errorResult(error, 'Unable to record the verdict.'); }
  });

  server.registerTool('suggest_recipe', {
    description: 'Digest of the past prints of a material (matched in the filament/process profile names or the G-code file name of the print logs): per print start/max chamber temperature, soak minutes, exhaust re-enables by the firmware, result and user verdict, plus the known K1C ASA outcomes (only when the selected printer is a K1C) and HEURISTIC hints. Completed prints without a user verdict count as unknown, not as good. The scan reads at most 40 logs and stops after 30 s (partial=true). Advice only: it never changes anything; turn hints into actions with configure_heat_soak, preheat_chamber, set_exhaust_guard or preview_profile_changes after the user agrees.',
    inputSchema: z.object({
      material: z.string().trim().min(2).max(20).regex(/^[A-Za-z0-9+\- ]+$/).describe('Material as it appears in profile names, e.g. ASA, ABS, PETG'),
      room_chamber_c: z.number().min(-10).max(50).optional().describe('Current room temperature in °C, if the user knows it'),
    }),
  }, async ({ material, room_chamber_c }) => {
    try {
      const deadline = Date.now() + recipeDeadlineMs;
      const k1c = await printerIsK1c();
      const list = await requestApp<{ logs?: Json[] }>('/v1/print_logs', undefined, recipeCallTimeoutMs);
      const names = (list.logs ?? []).map((log) => log.name).filter((name): name is string => typeof name === 'string').slice(0, 40);
      const prints: Json[] = [];
      let scanned = 0;
      let partial = false;
      for (const name of names) {
        const left = deadline - Date.now();
        if (left < 500) { partial = true; break; }
        const log = await requestApp<Json>('/v1/print_log', { name, max_points: 2 }, Math.min(recipeCallTimeoutMs, left));
        scanned++;
        const header = (log.header ?? {}) as Json;
        if (!matchesMaterial(header, material)) continue;
        const summary = (log.summary ?? {}) as Json;
        const verdicts = Array.isArray(log.verdicts) ? log.verdicts as Json[] : [];
        const last = verdicts[verdicts.length - 1];
        const soak = num(header.soak_seconds);
        prints.push({
          name, file: header.file ?? null, filament: header.filament ?? null, process: header.process ?? null,
          result: log.running ? 'running' : summary.result ?? 'unknown',
          duration_min: num(summary.duration_s) === null ? null : Math.round((summary.duration_s as number) / 60),
          soak_minutes: soak === null ? null : round1(soak / 60),
          start_chamber: num(summary.start_chamber), max_chamber: num(summary.max_chamber),
          min_chamber_after_start: num(summary.min_chamber_after_start),
          exhaust_reenables: num(summary.exhaust_reenables), bed_drop: summary.bed_drop ?? null,
          joined_mid_print: summary.joined_mid_print ?? header.joined_mid_print ?? null,
          verdict: last?.value ?? null, verdict_note: last?.note || null,
        });
      }

      // Only a user verdict makes a print good: a completed print may still have warped.
      const good = prints.filter((p) => p.verdict === 'ok');
      const unrated = prints.filter((p) => p.verdict === null && p.result === 'complete');
      const bad = prints.filter((p) => ['warp', 'detach'].includes(String(p.verdict)) || ['failed', 'aborted'].includes(String(p.result)));
      const maxOf = (list: Json[]) => list.map((p) => num(p.max_chamber)).filter((v): v is number => v !== null);
      const hints: string[] = [];
      const goodMax = maxOf(good), badMax = maxOf(bad);
      if (goodMax.length) hints.push(`Prints judged good reached a chamber maximum of ${Math.min(...goodMax)}-${Math.max(...goodMax)} °C: aim at least for the lower value.`);
      if (badMax.length) hints.push(`Warped/detached/failed prints peaked at ${Math.min(...badMax)}-${Math.max(...badMax)} °C.`);
      if (bad.some((p) => (num(p.exhaust_reenables) ?? 0) > 0))
        hints.push(k1c === true
          ? `In failed prints the firmware re-enabled the exhaust fan. ${k1cExhaustNote} set_exhaust_guard only logs the re-enables on the K1C.`
          : 'In failed prints the firmware re-enabled the exhaust fan: arm set_exhaust_guard at the start of the next print and check with get_monitor_status that the fan really stays off (some firmwares ignore the off command above a chamber threshold).');
      if (k1c === true && isAsaAbsFamily(material))
        hints.push('On the K1C, glue stick on smooth PEI was decisive for large ASA parts (not yet tested with ABS, but likely to help too): the 130 mm boss detached at 57% without glue and printed well with glue, with the same 33-37 °C chamber.');
      if (room_chamber_c !== undefined && room_chamber_c < 20)
        hints.push(`Cold room (${room_chamber_c} °C): use a longer soak (15-20 min, preheat_chamber with a chamber target), keep the lid closed and shield the printer from drafts; prefer smaller parts or split large ones.`);
      if (isShrinkProne(material))
        hints.push('Shrinking material: no part cooling for the first 3-5 layers, wide brim (8-10 mm, optionally on 2-3 layers), exhaust off during print, slow_cooldown after the print. Large parts warp far more than small test parts.');
      if (isAsaAbsFamily(material) && prints.some((p) => p.verdict === 'stringing'))
        hints.push('Stringing was reported with this material: first dry the filament (ASA/ABS are hygroscopic, typically 80 °C for 4-6 h), then print the retraction test (start_calibration retraction, end at most 10 mm) and save the best length with compute_calibration_result; lower the nozzle temperature only afterwards, within the filament range.');
      if (unrated.length)
        hints.push(`${unrated.length} completed print(s) have no verdict and count as unknown: ask the user how they turned out and save it with record_print_verdict.`);
      const showK1c = k1c === true && isAsaFamily(material);
      if (!prints.length) hints.push(showK1c ? 'No print log matches this material yet: rely on the known K1C outcomes below and on get_material_guide.' : 'No print log matches this material yet: rely on get_material_guide.');

      return jsonResult({
        success: true, material, room_chamber_c: room_chamber_c ?? null,
        logs_scanned: scanned, logs_available: names.length, partial, prints,
        printer_is_k1c: k1c,
        known_k1c_asa_prints: showK1c ? knownK1cAsaPrints : [],
        heuristic_hints: hints,
        disclaimer: 'Heuristic advice from a few prints, not a validated recipe. The K1C chamber sensor reads 5-10 °C high. Nothing has been changed.',
      });
    } catch (error) { return errorResult(error, 'Unable to build the recipe digest.'); }
  });
}
