import type { McpServer } from '@modelcontextprotocol/server';
import * as z from 'zod/v4';
import { confirmationNote, errorResult, jsonResult, mutationTimeoutMs, requestApp, slowReadTimeoutMs } from '../appClient.js';

type Json = Record<string, unknown>;

// Archive names come from list_archived_prints; the app also refuses anything outside its archive folder.
const archiveNameSchema = z.string().min(1).max(200).regex(/^[^/\\:]+$/).refine((name) => !name.includes('..'), 'invalid archive name')
  .describe('Archive folder name exactly as returned by list_archived_prints');

// Long strings and lists in metadata.json are cut so a big archive cannot flood the context.
const clip = (value: unknown): unknown => {
  if (typeof value === 'string') return value.length > 300 ? `${value.slice(0, 300)}…` : value;
  if (Array.isArray(value)) return value.slice(0, 16).map(clip);
  if (value && typeof value === 'object')
    return Object.fromEntries(Object.entries(value as Json).slice(0, 16).map(([key, item]) => [key, clip(item)]));
  return value;
};

// Only the fields an assistant needs from each metadata.json.
const metadataKeys = ['created_ms', 'verdict', 'file', 'log', 'address', 'printer_model', 'started_ms', 'ended_ms',
  'profiles_at_print_start', 'profiles_now'];
const artifactKeys = ['project', 'gcode', 'print_log', 'photo'];

function compactMetadata(metadata: unknown): Json | null {
  if (!metadata || typeof metadata !== 'object' || Array.isArray(metadata)) return null;
  const source = metadata as Json;
  const result: Json = {};
  for (const key of metadataKeys) if (key in source) result[key] = clip(source[key]);
  for (const key of artifactKeys) {
    const item = source[key];
    if (item && typeof item === 'object' && !Array.isArray(item)) {
      const artifact = item as Json;
      const kept: Json = {};
      for (const field of ['status', 'file', 'reason', 'note', 'source'])
        if (artifact[field] !== undefined) kept[field] = artifact[field];
      result[key] = clip(kept);
    }
  }
  return result;
}

function compactArchiveList(list: Json): Json {
  if (!Array.isArray(list.archives)) return list;
  return {
    ...list,
    archives: (list.archives as Json[]).map((entry) => ({
      name: entry.name, path: entry.path, has_project: entry.has_project,
      files: Array.isArray(entry.files) ? (entry.files as unknown[]).slice(0, 16) : [],
      metadata: compactMetadata(entry.metadata),
    })),
    pending_verdict: list.pending_verdict === undefined ? null : clip(list.pending_verdict),
  };
}

// Archive of successful prints, built by MentorSlicer when the user answers "Good, archive" in the
// end-of-print verdict dialog (shown after a completed print once the bed cooled below 40 C).
export function registerArchiveTools(server: McpServer) {
  server.registerTool('list_archived_prints', {
    description: 'Archived successful prints (<data_dir>/print-advisor/archive/<date>_<name>/), newest first, with the essential fields of their metadata.json (long texts are shortened): printed file, printer, profiles at print start and at archive time, verdict, and for the project (3MF), G-code, print log and camera photo whether each was saved or skipped and why. Also returns pending_verdict when MentorSlicer still has to ask the user how the last print turned out. Read-only.',
    inputSchema: z.object({}),
  }, async () => {
    try { return jsonResult(compactArchiveList(await requestApp<Json>('/v1/archive', undefined, slowReadTimeoutMs))); }
    catch (error) { return errorResult(error, 'Unable to read the print archive.'); }
  });

  server.registerTool('reopen_archived_project', {
    description: `Open the project.3mf of an archived print in MentorSlicer, replacing the open project (MentorSlicer asks to save it first). ${confirmationNote} Nothing is sent to the printer and no print is started. MentorSlicer loads a temporary copy (<data_dir>/print-advisor/tmp/<archive>.3mf) and makes the project untitled, so Save asks for a new name and the archived copy is never overwritten. The printer, filament and process profiles embedded in the project become the active selection (the dialog says so); the result reports presets_before and presets_after.`,
    inputSchema: z.object({
      name: archiveNameSchema,
      reason: z.string().max(500).optional().describe('Short reason shown in the MentorSlicer confirmation dialog'),
    }),
  }, async ({ name, reason }) => {
    try { return jsonResult(await requestApp<Json>('/v1/archive/open', { name, ...(reason ? { reason } : {}) }, mutationTimeoutMs)); }
    catch (error) { return errorResult(error, 'Unable to open the archived project.'); }
  });
}
