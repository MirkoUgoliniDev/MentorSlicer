import type { McpServer } from '@modelcontextprotocol/server';
import * as z from 'zod/v4';
import { confirmationNote, errorResult, guiReadTimeoutMs, jsonResult, requestApp } from '../appClient.js';

type Json = Record<string, unknown>;

// Plate type names of the local API (bed_type_to_gcode_string in the slicer), the same strings
// get_bed_type returns. get_print_context bed.plate_type uses the same names except for the
// Customized plate, which it reports as "unknown". "default" is only valid for a plate (follow the project).
const bedTypeSchema = z.enum(['cool_plate', 'eng_plate', 'hot_plate', 'textured_plate', 'customized_plate', 'epoxy_resin_plate', 'default'])
  .describe('hot_plate = Smooth PEI / High Temp Plate, textured_plate = Textured PEI Plate; cool_plate/eng_plate are not offered for Creality printers, epoxy_resin_plate only for them; default (plate scope only) removes the plate override');

export function registerPlateTools(server: McpServer) {
  server.registerTool('get_bed_type', {
    description: 'Read the plate type (curr_bed_type, a project setting, not a profile): the project default shown in the sidebar, the override of each plate (Plate Settings), the effective type per plate, the plate types this printer offers, and for each one the bed temperatures of the active filament (its *_plate_temp and *_plate_temp_initial_layer keys). Read-only.',
    inputSchema: z.object({}),
  }, async () => {
    try { return jsonResult(await requestApp<Json>('/v1/bed_type', undefined, guiReadTimeoutMs)); }
    catch (error) { return errorResult(error, 'Unable to read the plate type.'); }
  });

  server.registerTool('set_bed_type', {
    description: `Change the plate type: scope "project" sets the project default (sidebar combo box, used by every plate without an override), scope "plate" sets an override on one plate (plate_number, default the current plate; bed_type "default" removes it). Omitted scope: project when the project has one plate and no plate_number is given, otherwise plate. MentorSlicer shows before/after and the bed temperatures the active filament will use on the new type, through the same code path as the GUI (sidebar and 3D view refresh). Refused while slicing or exporting; the affected plates must be sliced again. ${confirmationNote}`,
    inputSchema: z.object({
      bed_type: bedTypeSchema,
      plate_number: z.number().int().min(1).max(64).optional().describe('1-based plate number (plate_number of get_print_context); omitted: the current plate'),
      scope: z.enum(['project', 'plate']).optional(),
      reason: z.string().max(500).optional().describe('Short reason shown in the confirmation dialog'),
    }),
  }, async (args) => {
    try { return jsonResult(await requestApp<Json>('/v1/bed_type', args)); }
    catch (error) { return errorResult(error, 'Unable to change the plate type.'); }
  });
}
