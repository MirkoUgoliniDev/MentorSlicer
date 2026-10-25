import { readFile } from 'node:fs/promises';
import { homedir } from 'node:os';
import { join } from 'node:path';

// Client of the authenticated loopback API hosted by MentorSlicer. The token is read from the
// descriptor on every call and never returned to the MCP client or logged.

type EndpointDescriptor = {
  base_url: string;
  token: string;
  api_version: number;
  pid?: number;
};

export const notRunning = 'MentorSlicer non è aperto, oppure Print Advisor è disattivato (Integrazione AI → Print Advisor MCP, poi Abilita).';
const invalidDescriptor = 'Descrittore endpoint.json non valido: disattiva e riattiva Print Advisor in MentorSlicer (Integrazione AI → Print Advisor MCP).';

// True when a process with this id exists (EPERM still means it exists).
function processAlive(pid: number): boolean {
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    return (error as { code?: string }).code === 'EPERM';
  }
}

// The descriptor of the default data folder (the one MentorSlicer uses without --datadir).
// MENTORSLICER_ADVISOR_DESCRIPTOR overrides it, e.g. for a build started with --datadir.
export function descriptorPath(): string {
  const configured = process.env.MENTORSLICER_ADVISOR_DESCRIPTOR;
  if (configured) return configured;
  const home = homedir();
  const userData = process.platform === 'win32' ? (process.env.APPDATA ?? join(home, 'AppData', 'Roaming'))
    : process.platform === 'darwin' ? join(home, 'Library', 'Application Support')
    : (process.env.XDG_CONFIG_HOME ?? join(home, '.config'));
  return join(userData, 'MentorSlicer', 'cache', 'print-advisor', 'endpoint.json');
}

async function readDescriptor(): Promise<EndpointDescriptor> {
  const file = descriptorPath();
  let text: string;
  try {
    text = await readFile(file, 'utf8');
  } catch {
    throw new Error(notRunning);
  }
  let parsed: EndpointDescriptor;
  try {
    parsed = JSON.parse(text) as EndpointDescriptor;
  } catch {
    // Corrupted or foreign file (the app replaces it atomically).
    throw new Error(invalidDescriptor);
  }
  if (!parsed || typeof parsed !== 'object' || typeof parsed.base_url !== 'string' || typeof parsed.token !== 'string')
    throw new Error(invalidDescriptor);
  // A descriptor left behind by an app that did not shut down cleanly.
  if (typeof parsed.pid === 'number' && !processAlive(parsed.pid))
    throw new Error(notRunning + ' (descrittore di una sessione chiusa)');
  let endpoint: URL;
  try {
    endpoint = new URL(parsed.base_url);
  } catch {
    throw new Error(invalidDescriptor);
  }
  if (endpoint.protocol !== 'http:' || endpoint.hostname !== '127.0.0.1' || !endpoint.port || endpoint.pathname !== '/'
      || endpoint.username || endpoint.password || !parsed.token || parsed.api_version !== 1)
    throw new Error(invalidDescriptor);
  return parsed;
}

// Timeouts. Mutating routes wait for the in-app confirmation: MentorSlicer keeps a dialog open
// for up to 600 s, so the sidecar waits a little longer. Plain reads fail fast.
export const readTimeoutMs = 5000;
export const slowReadTimeoutMs = 15000;
export const mutationTimeoutMs = 610000;
// Read-only routes that run on the MentorSlicer GUI thread and can wait behind a long task
// there (a slice, a long computation): same wait as the mutations.
export const guiReadTimeoutMs = 610000;

// POST routes that only read (or only build an in-memory proposal) and are safe to repeat. Every
// other POST changes something, so a timeout must not be retried blindly. Keyed on method + path:
// GET /v1/bed_type (get_bed_type) is a read, POST /v1/bed_type (set_bed_type) a change.
const readOnlyRoutes = new Set([
  'POST /v1/proposals', 'POST /v1/profile_diff', 'POST /v1/preflight', 'POST /v1/overhang_regions',
  'POST /v1/support_volumes', 'POST /v1/print_log',
]);

// HTTP 503: MentorSlicer refused the connection before reading the request (too many open
// connections), so nothing was done and even a change can be sent again.
class AppBusyError extends Error {}

// GET when body is undefined, POST otherwise. A POST without an explicit timeout gets the mutation
// timeout (it may open a confirmation dialog). A 503 is retried a few times with a short back-off.
export async function requestApp<T>(path: string, body?: Record<string, unknown>, timeoutMs?: number): Promise<T> {
  const backoffMs = [300, 800, 1500];
  for (let attempt = 0; ; attempt++) {
    try {
      return await requestAppOnce<T>(path, body, timeoutMs);
    } catch (error) {
      if (!(error instanceof AppBusyError) || attempt >= backoffMs.length) throw error;
      await delay(backoffMs[attempt]);
    }
  }
}

async function requestAppOnce<T>(path: string, body?: Record<string, unknown>, timeoutMs?: number): Promise<T> {
  const descriptor = await readDescriptor();
  const url = new URL(path, descriptor.base_url);
  const isWrite = body !== undefined;
  const method = isWrite ? 'POST' : 'GET';
  const isMutation = isWrite && !readOnlyRoutes.has(`${method} ${path}`);
  const timeout = timeoutMs ?? (isWrite ? mutationTimeoutMs : readTimeoutMs);
  let response: Response;
  try {
    response = await fetch(url, {
      method,
      headers: {
        authorization: `Bearer ${descriptor.token}`,
        accept: 'application/json',
        ...(isWrite ? { 'content-type': 'application/json' } : {}),
      },
      ...(isWrite ? { body: JSON.stringify(body) } : {}),
      // The loopback API never redirects: following one could carry the token elsewhere.
      redirect: 'error',
      signal: AbortSignal.timeout(timeout),
    });
  } catch (error) {
    if (error instanceof Error && error.name === 'TimeoutError')
      throw new Error(isMutation
        ? `MentorSlicer non ha risposto entro ${Math.round(timeout / 1000)} s (${path}). Non ripetere la richiesta in automatico: verifica prima se è stata applicata (get_change_history, get_printer_status o lo strumento di lettura corrispondente).`
        : `MentorSlicer non ha risposto entro ${Math.round(timeout / 1000)} s (${path}): probabilmente è occupato (una modifica in corso, un caricamento o uno slicing). È una lettura: si può riprovare tra poco.`);
    throw new Error(notRunning);
  }
  if (!response.ok) {
    const detail = (await response.text()).slice(0, 2000);
    // The app answers {success:false, error} on refusals: surface only the message.
    let message = detail;
    try {
      const parsed = JSON.parse(detail) as { error?: unknown };
      if (typeof parsed.error === 'string') message = parsed.error;
    } catch { /* not JSON */ }
    // 409: another request or a dialog is open in MentorSlicer. Nothing was done; never retry a
    // change automatically, ask the user to close the open window first.
    // 503: MentorSlicer is serving its maximum number of connections. Nothing was done.
    if (response.status === 503)
      throw new AppBusyError(`MentorSlicer è occupato da altre richieste (HTTP 503): nessuna modifica eseguita. Riprova tra qualche secondo.`);
    if (response.status === 409)
      throw new Error(`MentorSlicer è occupato (HTTP 409): ${message || "un'altra richiesta è in corso"}. Nessuna modifica eseguita: non riprovare da solo; chiedi all'utente di chiudere la finestra aperta in MentorSlicer, poi riprova solo se l'utente lo chiede.`);
    throw new Error(`MentorSlicer local API returned HTTP ${response.status}: ${message || 'Is Print Advisor enabled in the app?'}`);
  }
  return await response.json() as T;
}

// Compact JSON (no indentation): some results are large and indentation only costs tokens.
export const jsonResult = (result: Record<string, unknown>) =>
  ({ content: [{ type: 'text' as const, text: JSON.stringify(result) }], isError: result.success === false });

export const errorResult = (error: unknown, fallback: string) =>
  ({ isError: true, content: [{ type: 'text' as const, text: error instanceof Error ? error.message : fallback }] });

export const delay = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

// Shared safety sentence for every tool that changes something.
export const confirmationNote = 'Every change shows a confirmation dialog in MentorSlicer and is sent only if the user accepts it there; the Print Advisor never starts a print.';
