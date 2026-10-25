import type { McpServer } from '@modelcontextprotocol/server';
import * as z from 'zod/v4';
import { confirmationNote, errorResult, jsonResult, requestApp, slowReadTimeoutMs } from '../appClient.js';
import { explainPrinterError } from '../errorCodes.js';

const reasonSchema = z.string().max(500).optional().describe('Short reason shown in the MentorSlicer confirmation dialog');
const verifyNote = 'MentorSlicer sends the command over the LAN connection of the Device tab (which must be open): verify the effect with get_printer_status a few seconds later.';
export const k1cExhaustNote = 'On the K1C the firmware switches the rear exhaust on when the chamber reaches about 35-37 C and, while the chamber stays above that threshold, ignores every external switch-off (fanCase 0 and M106 P1 S0); it switches it off by itself when the chamber cools down. Real remedies there: aluminium tape over the rear grille (remove it for PLA) or changing the firmware threshold (needs root access).';

type Device = Record<string, unknown> & { err?: { errcode?: unknown; key?: unknown } };

const nonZero = (value: unknown) => value !== undefined && value !== null && value !== '' && value !== 0 && value !== '0' && value !== -1;

// Adds error_explained to every device that reports an active error.
function withErrorExplanations(status: Record<string, unknown>) {
  const devices = Array.isArray(status.devices) ? status.devices as Device[] : [];
  return {
    ...status,
    devices: devices.map((device) => {
      const err = device.err;
      if (!err || typeof err !== 'object' || !(nonZero(err.errcode) || nonZero(err.key))) return device;
      return { ...device, error_explained: explainPrinterError(err.errcode, err.key) };
    }),
  };
}

async function deviceControl(body: Record<string, unknown>, fallback: string) {
  try { return jsonResult(await requestApp<Record<string, unknown>>('/v1/device_control', body)); }
  catch (error) { return errorResult(error, fallback); }
}

export function registerDeviceTools(server: McpServer) {
  server.registerTool('get_printer_status', {
    description: 'Live state of the LAN printers MentorSlicer is connected to: state (printing, paused, complete, failed, aborted...), progress, layer, elapsed and remaining time, nozzle/bed/chamber temperatures with targets and maximums, part/side/case fan speeds, errors, recent finish/error events, print_control_results: the last answer of the Device page for each action (pause, resume, stop, set_bed_temp) of a control_print or preheat_chamber command (success, action, request_id to match the id returned by that tool, value, code/message on failure, requested_at/received_at in ms), and last_print_control_result: the newest of them (null if none since MentorSlicer started). Also, when the printer reports them: materialDetect/materialStatus (filament runout sensor enabled / filament present), ai* fields (AI camera detections and their switches), speedMode (25 silent, 50 stable, 100 standard, 125 sport), sleepMode (1 = printer asleep: commands are refused until woken from the Device tab), printStartTime (epoch s) and printFileName. A device with an active error gets an error_explained field (code, models, English and Italian text). Data comes from the Device tab connection: if empty, ask the user to open the Device tab. Read-only.',
    inputSchema: z.object({}),
  }, async () => {
    try { return jsonResult(withErrorExplanations(await requestApp<Record<string, unknown>>('/v1/printer_status'))); }
    catch (error) { return errorResult(error, 'Unable to read the printer status.'); }
  });

  server.registerTool('explain_printer_error', {
    description: 'Explain a Creality printer error code (the err.errcode / err.key of get_printer_status, or the number shown on the printer screen, e.g. 2565 or AC0117) using the error table of the MentorSlicer device page: display code, table level (1-3, meaning undocumented), models it applies to, English text and an Italian translation for K1-series codes. Unknown codes return known=false with advice to read the printer screen. Read-only.',
    inputSchema: z.object({
      code: z.union([z.number().int().min(0).max(99999999), z.string().trim().min(1).max(16).regex(/^[A-Za-z]{0,2}\d{1,8}$/)])
        .describe('Numeric key (e.g. 2565) or display code (e.g. CB2565, AC0117)'),
    }),
  }, async ({ code }) => {
    const text = typeof code === 'string' ? code.toUpperCase() : String(code);
    let explanation = explainPrinterError(text);
    // A display code like CB2565 is looked up by its numeric part when the prefix is not a table key.
    if (!explanation.known && /^[A-Z]{2}\d+$/.test(text)) {
      const numeric = explainPrinterError(undefined, Number(text.slice(2)));
      if (numeric.known && numeric.code === text) explanation = numeric;
    }
    return jsonResult({ success: true, ...explanation });
  });

  server.registerTool('set_fan', {
    description: `Set one printer fan to a percentage, on an idle or printing printer selected in MentorSlicer. ${confirmationNote} On the Creality K1C: "side" is the big side fan (M106 P2, fanAuxiliary, telemetry auxiliaryFanPct), "exhaust" is the rear exhaust fan (M106 P1, fanCase, telemetry caseFanPct), "part" is the nozzle/part cooling fan (M106 P0, telemetry modelFanPct). During a print the slicer G-code and the firmware can override the value at the next fan command. ${k1cExhaustNote} ${verifyNote}`,
    inputSchema: z.object({
      fan: z.enum(['part', 'side', 'exhaust']),
      percent: z.number().int().min(0).max(100).describe('Fan speed in percent (0 = off)'),
      reason: reasonSchema,
    }),
  }, async ({ fan, percent, reason }) => deviceControl({ control: 'fan', fan, percent, ...(reason ? { reason } : {}) }, 'Unable to set the fan.'));

  server.registerTool('adjust_print_temp', {
    description: `Change the nozzle or bed target temperature of the print in progress (printing or paused only). It is a live adjustment for this print: profiles are not changed and the G-code can set another value later (e.g. a layer schedule). MentorSlicer refuses nozzle values outside the filament range and values above the printer maximum; nozzle 0 (heater off) and bed 0 are accepted, with a dialog warning that the running print will fail or the part may detach. ${confirmationNote} ${verifyNote}`,
    inputSchema: z.object({
      target: z.enum(['nozzle', 'bed']),
      value: z.number().int().min(0).max(350).describe('Target temperature in °C (bed usually at most 100-120 °C; K1C bed max 100 °C)'),
      reason: reasonSchema,
    }),
  }, async ({ target, value, reason }) => deviceControl({ control: 'temp', target, value, ...(reason ? { reason } : {}) }, 'Unable to change the temperature.'));

  server.registerTool('set_print_speed', {
    description: `Change the speed mode of the print in progress: 25 = Silenzioso (silent), 50 = Stabile (stable), 100 = Standard, 125 = Ultraveloce (sport). Values are percent of the sliced speeds. Profiles are not changed. ${confirmationNote} ${verifyNote}`,
    inputSchema: z.object({
      mode: z.union([z.literal(25), z.literal(50), z.literal(100), z.literal(125)]),
      reason: reasonSchema,
    }),
  }, async ({ mode, reason }) => deviceControl({ control: 'speed', mode, ...(reason ? { reason } : {}) }, 'Unable to change the print speed.'));

  server.registerTool('set_light', {
    description: `Turn the printer chamber light on or off (useful before camera_snapshot). ${confirmationNote} ${verifyNote}`,
    inputSchema: z.object({ on: z.boolean(), reason: reasonSchema }),
  }, async ({ on, reason }) => deviceControl({ control: 'light', on, ...(reason ? { reason } : {}) }, 'Unable to switch the light.'));

  server.registerTool('camera_snapshot', {
    description: 'Take one still frame from the camera of the printer selected in MentorSlicer and return it as an image, e.g. to look at the first layer, warping or spaghetti. Needs the Device tab open with the camera playing (WebRTC; an MJPEG-only stream cannot be captured); turn the light on with set_light if the chamber is dark. MentorSlicer takes the first visible, playing <video> of the Device tab: with several printers, open the detail view of the right one. The result reports the printer address shown by the page and address_matched; when address_matched is not true the frame may come from another printer than the selected one. Read-only: nothing is stored.',
    inputSchema: z.object({}),
  }, async () => {
    try {
      // The app waits up to 10 s for the frame.
      const shot = await requestApp<{
        success?: boolean; mime?: string; width?: number; height?: number; data_base64?: string; error?: string;
        address?: string; address_matched?: boolean; warning?: string;
      }>('/v1/camera_snapshot', undefined, slowReadTimeoutMs);
      if (shot.success !== true || !shot.data_base64 || !shot.mime?.startsWith('image/'))
        return errorResult(new Error(shot.error ?? 'Nessun fotogramma: apri la scheda Dispositivo di MentorSlicer con la telecamera attiva e riprova.'), '');
      const size = `Fotogramma ${shot.width ?? '?'}x${shot.height ?? '?'} px (${shot.mime})`;
      const shown = typeof shot.address === 'string' && shot.address ? ` mostrato dalla pagina per ${shot.address.slice(0, 100)}` : '';
      // Only a confirmed match may be presented as the selected printer.
      const text = shot.address_matched === true
        ? `${size} dalla stampante selezionata${shown}.`
        : `${size}${shown}. ATTENZIONE: non è confermato che venga dalla stampante selezionata in MentorSlicer; potrebbe essere di un'altra stampante. `
          + (typeof shot.warning === 'string' && shot.warning ? shot.warning.slice(0, 500) : 'Apri la vista di dettaglio della stampante giusta nella scheda Dispositivo e riprova.');
      return {
        content: [
          { type: 'image' as const, data: shot.data_base64, mimeType: shot.mime },
          { type: 'text' as const, text },
        ],
      };
    } catch (error) { return errorResult(error, 'Impossibile ottenere un fotogramma dalla telecamera.'); }
  });
}
