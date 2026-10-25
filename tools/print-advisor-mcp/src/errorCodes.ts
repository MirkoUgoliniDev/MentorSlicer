import { errorCodeTable, type ErrorRow } from './errorCodeTable.js';

// Italian texts for the codes that apply to the K1 family (K1, K1C, K1 Max). Written for this
// project; the other codes keep only the English text of the MentorSlicer device page.
const italian: Record<string, string> = {
  1: 'Anomalia del driver dei motori',
  2: 'Errore interno',
  3: 'Anomalia di comunicazione',
  4: 'Il riscaldamento non procede come previsto',
  6: 'Anomalia dell’estrusore',
  7: 'Coordinate del file di stampa anomale',
  9: 'Anomalia del sensore per l’ottimizzazione delle vibrazioni',
  10: 'Anomalia di una ventola',
  100: 'Rilevato un errore sconosciuto durante la stampa',
  101: 'Rilevato un problema di qualità di stampa: stampa in pausa',
  103: 'Errore sconosciuto',
  104: 'Rilevato un oggetto estraneo sul piatto: stampa in pausa',
  110: 'Errore di collegamento della scheda madre',
  111: 'Collegamento della testina anomalo',
  112: 'Collegamento del piatto riscaldato anomalo',
  115: 'Rilevata una stampa non terminata: vuoi riprenderla?',
  116: 'Filamento esaurito: stampa in pausa, ricarica il filamento',
  117: 'Rilevato un sollevamento (warping) del modello: stampa in pausa',
  201: 'Temperatura della camera anomala',
  204: 'Anomalia di una ventola',
  205: 'Anomalia di rete',
  206: 'L’AI ha rilevato problemi di qualità di stampa',
  207: 'Anomalia del sensore Z (z-Touch)',
  208: 'File anomalo',
  209: 'Temperatura del piatto riscaldato anomala',
  500: 'L’AI ha rilevato problemi di qualità di stampa',
  501: 'TF0501: velocità della ventola del dissipatore (heat break) troppo bassa',
  502: 'Anomalia della ventola della scheda madre',
  503: 'Rilevato un problema di qualità nel primo layer',
  504: 'Il file corrente non supporta il rilevamento del flusso',
  505: 'Anomalia del LiDAR AI',
  506: 'Calibrazione del LiDAR AI non riuscita',
  507: 'Allineamento del LiDAR AI non riuscito',
  508: 'Lo spazio di archiviazione occupato ha raggiunto {0}: libera spazio appena possibile',
  509: 'Temperatura della camera troppo alta: telecamera spenta, si riaccende da sola quando la temperatura torna normale',
  510: 'Avvio della telecamera non riuscito: prova a riavviare',
  523: 'Rilevato un sollevamento (warping) del modello',
  800: 'Errore di movimento di un asse',
  801: 'Preparazione della macchina',
  2000: 'Errore di sistema: prova a riavviare',
  2001: 'Errore di sistema: prova a riavviare',
  2060: 'Errore di sistema: prova a riavviare',
  2093: 'Errore di sistema: prova a riavviare',
  2111: 'La temperatura dell’ugello è sotto la minima di estrusione',
  2509: 'Impossibile leggere la temperatura dell’ugello: il termistore potrebbe essere scollegato',
  2510: 'Termistore del piatto aperto (interrotto)',
  2511: 'Il termistore della camera potrebbe essere interrotto',
  2512: 'Temperatura della scheda madre anomala',
  2515: 'Temperatura dell’ugello oltre il limite: il termistore potrebbe essere in corto',
  2516: 'Termistore del piatto in corto circuito',
  2517: 'Il termistore della camera potrebbe essere in corto circuito',
  2536: 'Errore sconosciuto',
  2537: 'Errore sconosciuto',
  2564: 'L’ugello non si scalda come previsto',
  2565: 'Il piatto non si scalda come previsto',
  2566: 'Errore sconosciuto',
  2567: 'Errore sconosciuto',
  2568: 'Errore sconosciuto',
  2571: 'Il file di stampa è danneggiato',
  2573: 'Anomalia nell’homing dell’asse X',
  2577: 'Anomalia nell’homing dell’asse Y',
  2581: 'Anomalia nell’homing dell’asse Z',
  2585: 'Coordinate di stampa dell’asse X fuori dai limiti',
  2586: 'Coordinate di stampa dell’asse Y fuori dai limiti',
  2587: 'Coordinate di stampa dell’asse Z fuori dai limiti',
  2588: 'Z offset oltre il limite',
  3000: 'Errore di sistema: prova a riavviare',
  3001: 'Errore di sistema: prova a riavviare',
  3002: 'Errore di sistema: prova a riavviare',
  20000: 'Rilevato il file dell’ultimo arresto imprevisto',
  20010: 'Filamento esaurito: stampa in pausa, ricarica il filamento',
  30000: 'Nessun filamento rilevato: vuoi continuare la stampa?',
  30010: 'Nessun filamento nel supporto bobina corrente',
};

const byKey = new Map<string, ErrorRow>(errorCodeTable.map((row) => [String(row[0]).toUpperCase(), row]));

export type ErrorExplanation = {
  known: boolean;
  lookup_key: string;
  code: string | null;
  level: number | null; // level of the device page table (1-3; meaning not documented)
  printer_models: readonly string[];
  en: string;
  it: string;
  wiki: string | null;
  advice: string;
};

// Same lookup as the device page: a textual errcode present in the table wins, otherwise the numeric key.
export function explainPrinterError(errcode: unknown, key?: unknown): ErrorExplanation {
  const text = typeof errcode === 'string' ? errcode.trim().toUpperCase() : '';
  const candidates = [text, key !== undefined && key !== null ? String(key) : '', typeof errcode === 'number' ? String(errcode) : '']
    .filter((value) => value && value !== '0');
  const lookup = candidates.find((value) => byKey.has(value.toUpperCase())) ?? candidates[0] ?? '0';
  const row = byKey.get(lookup.toUpperCase());
  if (!row) {
    return {
      known: false, lookup_key: lookup, code: null, level: null, printer_models: [],
      en: 'Unknown error', it: 'Errore sconosciuto', wiki: null,
      advice: 'Codice sconosciuto per MentorSlicer: leggi il messaggio sullo schermo della stampante e cerca il codice sul wiki Creality.',
    };
  }
  const [rowKey, code, level, apply, en] = row;
  const it = italian[String(rowKey)] ?? en;
  const display = code || String(rowKey);
  return {
    known: true, lookup_key: String(rowKey), code: display, level, printer_models: apply, en, it,
    wiki: `https://wiki.creality.com/en/printers-general-documents/${display}`,
    advice: apply.length && !apply.some((model) => model.startsWith('K1'))
      ? 'Questo codice non è documentato per la serie K1: verifica il messaggio sullo schermo della stampante.'
      : 'Controlla anche il messaggio sullo schermo della stampante; il link wiki è quello aperto da MentorSlicer (può non esistere per ogni codice).',
  };
}
