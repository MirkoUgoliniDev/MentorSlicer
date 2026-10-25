import type { McpServer } from '@modelcontextprotocol/server';
import * as z from 'zod/v4';
import { confirmationNote, errorResult, guiReadTimeoutMs, jsonResult, requestApp, slowReadTimeoutMs } from '../appClient.js';

type Json = Record<string, unknown>;

const objectIndex = z.number().int().min(0).max(999).describe('Object index on the current plate, as returned by analyze_model');
const vec3 = (min: number, max: number, what: string) =>
  z.tuple([z.number().min(min).max(max), z.number().min(min).max(max), z.number().min(min).max(max)]).describe(what);

// Expert workflow, part of the get_overhang_regions description (not repeated in its results).
const supportWorkflow = [
  '1. Analyze: read the clusters (kind, area_mm2, max/mean angle from vertical, span_mm, height_above_bed, below_clearance_mm and below=object|bed) and the current support_settings.',
  '2. Consider re-orienting first (arrange_plate orient, or ask the user to rotate): fewer supports beat good supports.',
  '3. Decide per cluster: bridges (bridge_like) up to about 10-15 mm span print fine without support in PLA, shorter (about 5-10 mm) in ASA/ABS/PETG; overhangs below 50-55 degrees from vertical usually need nothing; tiny areas (a few mm2) or small isolated points can often be skipped; ceilings (wide flat areas) need support; high clearance (tall support columns) is costly, prefer reorientation; cosmetic surfaces facing down will be marked by supports: ask the user which faces matter.',
  '4. Propose a plan with one reason per cluster to the user and wait for agreement.',
  '5. Apply: add_support_volume (enforcer boxes/cylinders covering the cluster bbox, blockers where supports must not go) and the support settings (enable_support, support_type normal(manual)/tree(manual), support_threshold_angle) via set_object_settings or the process profile; Z distance and interface layers also via set_object_settings.',
  '6. Verify: slice_and_measure, compare time, filament and the support weight in the time-per-feature data, check warnings; undo with remove_support_volume or Ctrl+Z if worse.',
].join('\n');

async function post(path: string, body: Json, fallback: string, timeoutMs?: number) {
  try { return jsonResult(await requestApp<Json>(path, body, timeoutMs)); }
  catch (error) { return errorResult(error, fallback); }
}

export function registerSupportTools(server: McpServer) {
  server.registerTool('get_overhang_regions', {
    description: `Find the regions of one object that may need support, grouped into clusters (at most 60, largest first): id, area_mm2, centroid, probe_point, bbox_min/bbox_max, z_min/z_max, height_above_bed, max/mean angle from vertical, kind (overhang, bridge_like, ceiling), span_mm, face_count, below_clearance_mm and below (object or bed), plus the angle convention, the current support settings and totals. Coordinates are world mm (bed at z = 0). threshold_deg is the face slope from horizontal like support_threshold_angle. Read-only. Expert workflow:\n${supportWorkflow}`,
    inputSchema: z.object({
      object_index: objectIndex,
      threshold_deg: z.number().min(10).max(89).default(45).describe('Overhang threshold, slope from horizontal in degrees'),
      min_area_mm2: z.number().min(0).max(100000).default(5).describe('Ignore clusters smaller than this area (mm²)'),
    }),
  }, async ({ object_index, threshold_deg, min_area_mm2 }) => {
    try {
      return jsonResult(await requestApp<Json>('/v1/overhang_regions', { object_index, threshold_deg, min_area_mm2 }, guiReadTimeoutMs));
    } catch (error) { return errorResult(error, 'Unable to analyze the overhangs.'); }
  });

  server.registerTool('list_support_volumes', {
    description: 'List the support enforcer and blocker volumes of one object (name, type, shape, position, size); those added by Print Advisor are named "Advisor enforcer…"/"Advisor blocker…". Read-only.',
    inputSchema: z.object({ object_index: objectIndex }),
  }, async ({ object_index }) => post('/v1/support_volumes', { object_index }, 'Unable to list the support volumes.', slowReadTimeoutMs));

  server.registerTool('add_support_volume', {
    description: `Add a support enforcer or blocker volume (box or cylinder) to one object, e.g. covering the bbox of a cluster from get_overhang_regions. Enforcers only act when supports are enabled for the object (enable_support=1, or enforce_support_layers > 0); with support_type normal(manual) or tree(manual) ONLY enforced areas get support, with the auto types enforcers add to the automatic ones. Blockers prevent support inside the volume. Change Z distance, interface layers or support type with set_object_settings. Undo with remove_support_volume or Ctrl+Z in MentorSlicer; slice again afterwards. ${confirmationNote}`,
    inputSchema: z.object({
      object_index: objectIndex,
      type: z.enum(['enforcer', 'blocker']),
      shape: z.enum(['box', 'cylinder']),
      center: vec3(-1000, 1000, 'Center [x, y, z] in world mm (bed at z = 0)'),
      size: vec3(0.5, 300,'Size [x, y, z] in mm; a cylinder needs [diameter, diameter, height] with x equal to y'),
      reason: z.string().min(1).max(500).describe('Why this volume is needed, shown in the confirmation dialog'),
    }),
  }, async (args) => {
    if (args.shape === 'cylinder' && Math.abs(args.size[0] - args.size[1]) > 1e-6)
      return jsonResult({ success: false, error: 'A cylinder needs size [diameter, diameter, height] with size[0] equal to size[1]' });
    return post('/v1/support_volume', args, 'Unable to add the support volume.');
  });

  server.registerTool('remove_support_volume', {
    description: `Remove a support volume added by Print Advisor (name starting with "Advisor enforcer" or "Advisor blocker", from list_support_volumes); volumes created by the user are refused. ${confirmationNote}`,
    inputSchema: z.object({
      object_index: objectIndex,
      volume_name: z.string().min(1).max(200).regex(/^Advisor (enforcer|blocker)/, 'Only volumes named "Advisor enforcer…" or "Advisor blocker…" can be removed'),
    }),
  }, async (args) => post('/v1/remove_support_volume', args, 'Unable to remove the support volume.'));
}
