import { McpServer } from '@modelcontextprotocol/server';
import { serveStdio } from '@modelcontextprotocol/server/stdio';
import * as z from 'zod/v4';
import { delay, errorResult, guiReadTimeoutMs, jsonResult, requestApp, slowReadTimeoutMs } from './appClient.js';
import { registerArchiveTools } from './tools/archive.js';
import { registerDeviceTools } from './tools/device.js';
import { registerMonitorTools } from './tools/monitor.js';
import { registerPlateTools } from './tools/plate.js';
import { registerSupportTools } from './tools/supports.js';

type PrintContext = {
  success: boolean;
  captured_at?: number;
  presets?: Record<string, unknown>;
  edited_config?: Record<string, unknown>;
  profile_identity?: Record<string, unknown>;
  bed?: { plate_type?: string; temp_key?: string; initial_layer_temp_key?: string };
  editable_profile_values?: Record<string, Record<string, unknown>>;
};

type SliceResult = { success: boolean; sliced?: boolean; slicing_in_progress?: boolean } & Record<string, unknown>;

type ParameterSchema = {
  success: boolean;
  options?: Array<Record<string, unknown>>;
};

const defectSchema = z.enum([
  'stringing',
  'warping',
  'poor_bed_adhesion',
  'under_extrusion',
  'over_extrusion',
  'blobs_or_zits',
  'weak_bridging',
  'ghosting',
  'elephant_foot',
  'layer_shift',
  'visible_seam',
  'pillowing',
  'curling_overhangs',
  'layer_cracking',
  'weak_layer_adhesion',
]);

// MentorSlicer decides which keys are writable and returns their values in
// editable_profile_values. This list only picks the values shown by default in
// get_print_context; every other writable key is available via get_profile_values.
const contextSummaryKeys: Record<'filament' | 'process', string[]> = {
  filament: [
    'nozzle_temperature', 'nozzle_temperature_initial_layer',
    'cool_plate_temp', 'cool_plate_temp_initial_layer', 'eng_plate_temp', 'eng_plate_temp_initial_layer',
    'hot_plate_temp', 'hot_plate_temp_initial_layer', 'textured_plate_temp', 'textured_plate_temp_initial_layer',
    'chamber_temperature', 'filament_flow_ratio', 'filament_max_volumetric_speed', 'pressure_advance',
    'fan_min_speed', 'fan_max_speed', 'close_fan_the_first_x_layers', 'overhang_fan_speed',
    'slow_down_layer_time', 'slow_down_min_speed', 'filament_retraction_length', 'filament_retraction_speed',
    'additional_cooling_fan_speed', 'activate_air_filtration', 'during_print_exhaust_fan_speed', 'heat_soak_time', 'heat_soak_bed_temp',
  ],
  process: [
    'layer_height', 'initial_layer_print_height', 'wall_loops', 'top_shell_layers', 'bottom_shell_layers',
    'sparse_infill_density', 'sparse_infill_pattern', 'outer_wall_speed', 'inner_wall_speed', 'sparse_infill_speed',
    'initial_layer_speed', 'bridge_speed', 'travel_speed', 'default_acceleration', 'enable_support', 'support_type',
    'brim_type', 'brim_width', 'brim_layers', 'brim_object_gap', 'skirt_height', 'draft_shield', 'raft_layers',
    'seam_position', 'ironing_type',
  ],
};

const defectGuide: Record<z.infer<typeof defectSchema>, {
  questions: string[];
  causes: string[];
  checks: string[];
  candidates: Array<{ key: string; direction: string; reason: string; unit?: string }>;
}> = {
  stringing: {
    questions: ['Il filamento è stato essiccato e conservato chiuso?', 'Il difetto compare tra torri ravvicinate o su tutto il pezzo?', 'Che materiale e tipo di estrusore usi?'],
    causes: ['Umidità nel filamento', 'Temperatura di estrusione elevata per quel materiale', 'Movimenti di retrazione non ottimizzati'],
    checks: ['Asciuga o sostituisci il filamento prima di cambiare più parametri.', 'Controlla ugello e temperatura effettiva.', 'Fai una prova con una sola modifica per volta.',
      'Con ASA o ABS i primi due esperimenti sono: asciugare il filamento (ASA è igroscopico: tipicamente 80 °C per 4-6 h) e stampare il test di retrazione (start_calibration retraction, fine al massimo 10 mm) per leggere la lunghezza migliore con compute_calibration_result.'],
    candidates: [
      { key: 'nozzle_temperature', direction: 'valutare una riduzione graduale entro l’intervallo raccomandato per il materiale', reason: 'Una temperatura più bassa può ridurre il trasudamento, se il filamento è asciutto e l’estrusione resta regolare.', unit: '°C' },
      { key: 'filament_retraction_length', direction: 'verificare prima di modificare; con ASA/ABS misurarla con il test di retrazione (start_calibration retraction, entro 10 mm)', reason: 'La retrazione dipende molto da estrusore, percorso e materiale. È un override del filamento: se il valore è null usa quello della stampante e va prima abilitato nell’app.' },
    ],
  },
  warping: {
    questions: ['Il sollevamento è agli angoli o lungo tutto il bordo?', 'Che superficie e temperatura del piatto usi?', 'Il pezzo è esposto a correnti d’aria?'],
    causes: ['Ritiro termico del materiale', 'Adesione iniziale insufficiente', 'Raffreddamento o correnti d’aria non adatti'],
    checks: ['Pulisci il piatto e controlla il primo layer.', 'Riduci le correnti d’aria attorno alla stampante.', 'Valuta brim o brim ears prima di cambiare molti parametri.'],
    candidates: [
      { key: 'hot_plate_temp', direction: 'valutare un piccolo aumento entro l’intervallo raccomandato per il materiale', reason: 'Può migliorare l’adesione iniziale. La chiave dipende dal piatto in uso: cool_plate_temp, eng_plate_temp, hot_plate_temp o textured_plate_temp.', unit: '°C' },
      { key: 'close_fan_the_first_x_layers', direction: 'valutare più layer iniziali senza ventola per materiali che ritirano (ASA, ABS)', reason: 'Riduce il raffreddamento della base, che è la zona più soggetta a sollevarsi.' },
      { key: 'heat_soak_time', direction: 'su stampanti chiuse senza riscaldatore della camera, valutare 10-20 minuti di heat soak (configure_heat_soak)', reason: 'Il piatto caldo con ventola posteriore spenta scalda la camera prima della stampa e riduce il ritiro di ASA/ABS.', unit: 'min' },
      { key: 'brim_type', direction: 'abilitare o ampliare il brim', reason: 'Aumenta l’area di contatto senza alterare il profilo del materiale.' },
    ],
  },
  poor_bed_adhesion: {
    questions: ['Il primo layer è schiacciato in modo uniforme?', 'Il piatto è pulito e quale superficie usi?', 'Il distacco avviene subito o dopo alcuni layer?'],
    causes: ['Primo layer non uniforme o Z-offset non corretto', 'Piatto sporco o superficie non adatta', 'Temperatura o velocità del primo layer non adeguate'],
    checks: ['Verifica il primo layer prima di cambiare il flow.', 'Pulisci il piatto secondo le indicazioni della superficie.', 'Controlla che il materiale sia compatibile con la superficie.'],
    candidates: [
      { key: 'hot_plate_temp_initial_layer', direction: 'valutare un piccolo aumento entro l’intervallo raccomandato per il materiale', reason: 'Può aiutare l’adesione iniziale. La chiave dipende dal piatto in uso: cool_, eng_, hot_ o textured_plate_temp_initial_layer.', unit: '°C' },
      { key: 'initial_layer_speed', direction: 'ridurre leggermente', reason: 'Un primo layer più lento può migliorare la deposizione e il contatto.' },
    ],
  },
  under_extrusion: {
    questions: ['Il difetto è continuo o compare a tratti?', 'Hai controllato bobina, trascinamento ed eventuale intasamento?', 'Quali materiale, ugello e portata volumetrica usi?'],
    causes: ['Ugello parzialmente ostruito o alimentazione irregolare', 'Portata richiesta superiore alla capacità del sistema', 'Filamento o percorso di alimentazione problematici'],
    checks: ['Controlla percorso del filamento e stato dell’ugello.', 'Confronta la portata richiesta con i limiti della stampante e del materiale.', 'Non aumentare subito il flow ratio: prima escludi problemi meccanici.'],
    candidates: [{ key: 'filament_flow_ratio', direction: 'calibrare solo dopo aver escluso cause meccaniche', reason: 'La correzione del flow è specifica del filamento e va misurata.' }],
  },
  over_extrusion: {
    questions: ['Il materiale in eccesso compare su tutte le pareti o solo sugli angoli?', 'Il flow è già stato calibrato per questo filamento?', 'La temperatura è dentro l’intervallo raccomandato dal produttore?'],
    causes: ['Flow ratio troppo alto', 'Temperatura elevata o pressione non compensata', 'Geometria o cucitura che concentrano materiale'],
    checks: ['Controlla il flow ratio del filamento.', 'Ispeziona un tratto rettilineo per distinguere sovraestrusione da accumulo alla cucitura.', 'Modifica un solo parametro e misura il risultato.'],
    candidates: [{ key: 'filament_flow_ratio', direction: 'calibrare con una prova misurabile', reason: 'Ridurre il flow solo se la misura conferma sovraestrusione.' }],
  },
  blobs_or_zits: {
    questions: ['I blob sono concentrati sulla cucitura?', 'Che temperatura e materiale stai usando?', 'Sono presenti residui o umidità sul filamento?'],
    causes: ['Accumulo di pressione durante la cucitura', 'Temperatura o retrazione non ottimizzate', 'Umidità o residui sull’ugello'],
    checks: ['Individua la posizione della cucitura.', 'Controlla asciugatura del materiale e pulizia dell’ugello.', 'Valuta temperatura e impostazioni di wipe/retrazione una alla volta.'],
    candidates: [{ key: 'nozzle_temperature', direction: 'valutare una riduzione graduale entro l’intervallo raccomandato se il materiale è asciutto', reason: 'Può ridurre il trasudamento; verificare che non aumentino sottoestrusione e opacità.', unit: '°C' }],
  },
  weak_bridging: {
    questions: ['Quanto è lungo il ponte e che materiale usi?', 'Il difetto è cedimento al centro o scarsa adesione alle estremità?', 'Quali velocità e ventola sono impostate per i ponti?'],
    causes: ['Raffreddamento insufficiente', 'Velocità o portata di ponte non adatte', 'Distanza del ponte o geometria sfavorevoli'],
    checks: ['Confronta un ponte corto e uno lungo.', 'Controlla il raffreddamento consentito dal materiale.', 'Prova un test di bridging prima di modificare il profilo completo.'],
    candidates: [
      { key: 'fan_max_speed', direction: 'valutare un piccolo aumento se il materiale lo consente', reason: 'Più raffreddamento può sostenere il ponte, ma alcuni materiali si deformano se raffreddati troppo.' },
      { key: 'bridge_speed', direction: 'verificare con un test dedicato', reason: 'La velocità efficace dipende da materiale, ugello e lunghezza del ponte.' },
    ],
  },
  ghosting: {
    questions: ['Le onde compaiono dopo gli spigoli o i fori, e si attenuano allontanandosi?', 'Sono più evidenti su un asse (X o Y)?', 'La calibrazione delle vibrazioni (input shaping) della stampante è recente?'],
    causes: ['Accelerazioni elevate rispetto alla rigidità della macchina', 'Cinghie lente o rotelle con gioco', 'Input shaping non calibrato'],
    checks: ['Controlla la tensione delle cinghie.', 'Esegui la calibrazione automatica delle vibrazioni della stampante.', 'Riduci l’accelerazione prima della velocità.'],
    candidates: [
      { key: 'default_acceleration', direction: 'ridurre del 20-30%', reason: 'Accelerazioni più basse riducono le oscillazioni dopo i cambi di direzione.', unit: 'mm/s²' },
      { key: 'outer_wall_speed', direction: 'ridurre la velocità della parete esterna', reason: 'La parete esterna è quella che mostra il difetto.', unit: 'mm/s' },
    ],
  },
  elephant_foot: {
    questions: ['La base è più larga solo nei primi 1-3 layer?', 'Il primo layer è molto schiacciato?', 'Quale temperatura del piatto usi?'],
    causes: ['Primo layer troppo schiacciato', 'Piatto molto caldo che ammorbidisce la base', 'Peso del pezzo sui layer iniziali ancora morbidi'],
    checks: ['Guarda il primo layer: linee molto piatte indicano schiacciamento eccessivo.', 'Misura la base con il calibro rispetto al resto del pezzo.'],
    candidates: [
      { key: 'elefant_foot_compensation', direction: 'impostare 0.1-0.2 mm', reason: 'Restringe i primi layer per compensare l’allargamento.', unit: 'mm' },
      { key: 'hot_plate_temp', direction: 'ridurre di 5 °C se il materiale lo consente', reason: 'Un piatto meno caldo ammorbidisce meno la base.', unit: '°C' },
    ],
  },
  layer_shift: {
    questions: ['Lo spostamento avviene a un’altezza precisa o casuale?', 'Hai sentito urti o salti dei motori?', 'Ci sono parti arricciate che la testina può urtare?'],
    causes: ['Urto della testina contro parti arricciate o sbavature', 'Cinghie lente o pulegge allentate', 'Accelerazioni o velocità di spostamento eccessive'],
    checks: ['Controlla cinghie e pulegge.', 'Verifica se il pezzo ha sbalzi o angoli arricciati.', 'Controlla che nulla ostacoli i movimenti.'],
    candidates: [
      { key: 'travel_speed', direction: 'ridurre', reason: 'Spostamenti più lenti riducono la perdita di passi.', unit: 'mm/s' },
      { key: 'default_acceleration', direction: 'ridurre', reason: 'Meno forza sui motori nei cambi di direzione.', unit: 'mm/s²' },
    ],
  },
  visible_seam: {
    questions: ['La cucitura forma una linea verticale netta o puntini sparsi?', 'Ci sono buchi o rigonfiamenti nel punto di giunzione?', 'Preferisci nasconderla in uno spigolo o sul retro?'],
    causes: ['Posizione della cucitura su una faccia visibile', 'Pressure advance non calibrato', 'Retrazione o temperatura che creano gocce o buchi'],
    checks: ['Calibra il pressure advance prima di cambiare altro.', 'Scegli dove posizionare la cucitura.'],
    candidates: [
      { key: 'seam_position', direction: 'provare "back" o "aligned" su uno spigolo', reason: 'Sposta la cucitura dove si vede meno.' },
      { key: 'pressure_advance', direction: 'calibrare con il test dedicato', reason: 'Riduce rigonfiamenti e buchi all’inizio e alla fine dei perimetri.' },
    ],
  },
  pillowing: {
    questions: ['I buchi o le bolle sono solo sulla superficie superiore?', 'Quanti layer superiori hai?', 'Che ventola usi per il materiale?'],
    causes: ['Pochi layer superiori sopra un riempimento rado', 'Raffreddamento insufficiente della superficie superiore'],
    checks: ['Conta i layer superiori del profilo processo.', 'Valuta un riempimento più denso solo se servono molti layer.'],
    candidates: [
      { key: 'top_shell_layers', direction: 'aumentare di 1-2', reason: 'Più layer chiudono meglio i buchi sopra il riempimento.' },
      { key: 'fan_max_speed', direction: 'aumentare se il materiale lo consente (non per ASA/ABS)', reason: 'Raffreddamento migliore delle linee sopra il vuoto.', unit: '%' },
    ],
  },
  curling_overhangs: {
    questions: ['A che angolo iniziano ad arricciarsi gli sbalzi?', 'Che materiale e che ventola usi?', 'La testina urta le parti arricciate?'],
    causes: ['Raffreddamento insufficiente sugli sbalzi', 'Velocità alta sugli sbalzi', 'Angolo dello sbalzo oltre il limite del materiale'],
    checks: ['Valuta un orientamento che riduca gli sbalzi.', 'Considera i supporti oltre 50-60°.'],
    candidates: [
      { key: 'overhang_fan_speed', direction: 'aumentare se il materiale lo consente', reason: 'Raffredda gli sbalzi anche quando la ventola generale è bassa.', unit: '%' },
      { key: 'enable_overhang_bridge_fan', direction: 'attivare', reason: 'Forza la ventola su sbalzi e ponti.' },
    ],
  },
  layer_cracking: {
    questions: ['Le crepe sono orizzontali tra i layer e più frequenti in alto?', 'La stampante è chiusa? La ventola posteriore gira durante la stampa?', 'Che materiale usi (ASA, ABS, PC)?'],
    causes: ['Camera fredda: il materiale ritira e i layer si separano', 'Ventola pezzo o posteriore troppo alte', 'Temperatura di estrusione bassa'],
    checks: ['Tieni chiuso il coperchio e leggi la temperatura della camera (get_printer_status).', 'Spegni la ventola posteriore durante la stampa.'],
    candidates: [
      { key: 'heat_soak_time', direction: 'aggiungere 10-15 minuti di preriscaldamento', reason: 'Una camera calda riduce il ritiro tra i layer.', unit: 'min' },
      { key: 'during_print_exhaust_fan_speed', direction: 'impostare a 0 con activate_air_filtration attivo', reason: 'La ventola posteriore svuota la camera dal calore.', unit: '%' },
      { key: 'fan_max_speed', direction: 'ridurre a 10-20%', reason: 'Meno raffreddamento, migliore fusione tra i layer.', unit: '%' },
    ],
  },
  weak_layer_adhesion: {
    questions: ['Il pezzo si spezza facilmente lungo i layer?', 'La superficie è opaca o lucida?', 'Il filamento è asciutto?'],
    causes: ['Temperatura di estrusione bassa', 'Raffreddamento eccessivo', 'Sottoestrusione o filamento umido'],
    checks: ['Asciuga il filamento.', 'Verifica il flusso con il test di calibrazione.'],
    candidates: [
      { key: 'nozzle_temperature', direction: 'aumentare di 5-10 °C entro l’intervallo del materiale', reason: 'Una fusione migliore tra i layer aumenta la resistenza.', unit: '°C' },
      { key: 'fan_max_speed', direction: 'ridurre', reason: 'Meno raffreddamento migliora la saldatura tra i layer.', unit: '%' },
    ],
  },
};

// Typical starting values and pitfalls per material. These are general guidance: the filament
// maker's data sheet and the printer limits (machine_limits) always take precedence.
const materialGuide: Record<string, Record<string, unknown>> = {
  PLA: { nozzle_c: '190-230', bed_c: '50-65', chamber: 'aperta o fresca; coperchio sollevato su stampanti chiuse', part_fan: '80-100%', drying: '45-55 °C per 4-6 h',
    pitfalls: ['Heat creep e intasamenti con camera calda o heat soak', 'Ammorbidimento della base con piatto oltre 65 °C'] },
  PETG: { nozzle_c: '225-255', bed_c: '70-85', chamber: 'normale', part_fan: '30-60%', drying: '65 °C per 4-6 h',
    pitfalls: ['Fili e gocce: ridurre temperatura e asciugare', 'Adesione eccessiva su PEI liscio: usare colla come distaccante'] },
  ASA: { nozzle_c: '240-270', bed_c: '90-110 (rispettare il massimo della stampante)', chamber: 'calda, 40-60 °C: coperchio chiuso, heat soak, ventola posteriore spenta', part_fan: '0% nei primi 3-5 layer, poi 10-30%', drying: '80 °C per 4-6 h (igroscopico: asciugare prima di ogni stampa importante)',
    stringing: 'Primi esperimenti: asciugare il filamento, poi il test di retrazione (start_calibration retraction, fine al massimo 10 mm) e salvare la lunghezza letta con compute_calibration_result; la temperatura si abbassa solo dopo, entro l’intervallo del filamento.',
    pitfalls: ['Warping e distacco: camera calda, brim largo, colla su PEI liscio (decisiva sui pezzi grandi)', 'Crepe tra i layer con camera fredda', 'Fili e gocce: filamento umido o retrazione non calibrata', 'Emette stirene: arieggiare la stanza'] },
  ABS: { nozzle_c: '240-260', bed_c: '95-110 (rispettare il massimo della stampante)', chamber: 'calda, 45-60 °C', part_fan: '0-20%', drying: '80 °C per 4-6 h',
    stringing: 'Come l’ASA: asciugare il filamento e fare il test di retrazione (start_calibration retraction, entro 10 mm) prima di toccare la temperatura.',
    pitfalls: ['Warping e crepe come l’ASA, ancora più sensibile', 'Fumi: arieggiare la stanza'] },
  TPU: { nozzle_c: '210-240', bed_c: '40-60', chamber: 'normale', part_fan: '50-100%', drying: '50-55 °C per 6-8 h', speed: 'lenta: 15-40 mm/s, velocità volumetrica 3-5 mm³/s',
    pitfalls: ['Fili: retrazione corta e lenta', 'Inceppamenti con estrusori non diretti o velocità alte'] },
  PA: { nozzle_c: '250-290', bed_c: '70-100', chamber: 'calda', part_fan: '0-30%', drying: '80 °C per 8-12 h, critica',
    pitfalls: ['Molto igroscopico: stampare da box asciutto', 'Warping forte: PEI con colla'] },
  PC: { nozzle_c: '260-300', bed_c: '100-120 (spesso oltre il massimo di stampanti come la K1C)', chamber: 'molto calda, 50 °C e oltre', part_fan: '0-20%', drying: '80-120 °C per 6-8 h',
    pitfalls: ['Warping molto forte', 'Richiede piatto e camera che molte stampanti non raggiungono'] },
  CF_GF: { note: 'Filamenti caricati con fibra di carbonio o vetro (es. PLA-CF, PETG-CF, PA-CF)', nozzle: 'ugello in acciaio temprato, diametro 0.4 o meglio 0.6', part_fan: 'come il materiale base, spesso più basso',
    pitfalls: ['Abrasivi: consumano rapidamente l’ugello in ottone', 'Intasamenti con ugelli piccoli', 'Asciugare come il materiale base'] },
};

function recommendationPayload(defect: z.infer<typeof defectSchema>, context: PrintContext, options: Array<Record<string, unknown>>) {
  const guide = defectGuide[defect];
  const config = context.edited_config ?? {};
  // Bed temperature candidates follow the plate type selected on the current plate.
  const bedKey = (key: string) => {
    if (key.endsWith('plate_temp_initial_layer') && context.bed?.initial_layer_temp_key) return context.bed.initial_layer_temp_key;
    if (key.endsWith('plate_temp') && context.bed?.temp_key) return context.bed.temp_key;
    return key;
  };
  const proposals = guide.candidates.map((entry) => ({ ...entry, key: bedKey(entry.key) })).map((candidate) => {
    const option = options.find((entry) => entry.key === candidate.key);
    const owner = typeof option?.param_class === 'string' ? option.param_class : undefined;
    const ownerConfig = owner && typeof config[owner] === 'object' ? config[owner] as Record<string, unknown> : {};
    const current = context.editable_profile_values?.[owner ?? '']?.[candidate.key] ?? ownerConfig[candidate.key] ?? null;
    const writeAllowed = (owner === 'filament' || owner === 'process') && candidate.key in (context.editable_profile_values?.[owner] ?? {});
    const proposal: Record<string, unknown> = {
      parameter: candidate.key,
      current_value: current ?? null,
      direction: candidate.direction,
      rationale: candidate.reason,
      unit: candidate.unit ?? option?.unit ?? null,
      editable_profile_class: owner ?? null,
      write_allowed: writeAllowed,
      is_vector: option?.is_vector ?? null,
      requires_in_app_preview_and_confirmation: true,
      schema_limits: option ? { min: option.min ?? null, max: option.max ?? null } : null,
      note: writeAllowed
        ? 'Non viene proposto un numero universale: verifica i limiti del produttore e prova una sola variazione alla volta. Per parametri vettoriali la preview richiede un valore per ogni componente.'
        : 'MentorSlicer non espone questa chiave come scrivibile nel profilo selezionato.',
    };
    return proposal;
  });
  return {
    defect,
    assumptions: ['La diagnosi usa solo i sintomi forniti e il contesto leggibile dall’app.', 'edited_config contiene solo un sottoinsieme fisso di chiavi dei profili ed esclude le impostazioni per oggetto: per quelle usare analyze_model, per tutti i valori scrivibili editable_profile_values o get_profile_values.'],
    confidence: 'low_to_medium',
    follow_up_questions: guide.questions,
    possible_causes: guide.causes,
    low_risk_checks: guide.checks,
    parameter_candidates: proposals,
    write_enabled: true,
    write_requires_preview_and_explicit_in_app_approval: true,
    context_captured_at: context.captured_at ?? null,
  };
}

const server = new McpServer({ name: 'mentorslicer-advisor', version: '0.2.0' }, {
  instructions: 'Print Advisor for MentorSlicer. Start with get_print_context and get_parameter_schema. Recommendations are diagnostic experiments, not universal settings. To change a supported setting, call preview_profile_changes, then apply_profile_changes with its proposal_id; MentorSlicer requires a separate in-app approval and saves a new user copy. rollback_profile_update can remove an unchanged inactive copy after another in-app confirmation. Any numeric, percent, boolean or enum setting of the active filament or process profile can be changed; free text (G-code, names, notes), material identity and printer profiles are read-only. Use get_profile_values to read current values. The copy keeps the source parent profile, so values it does not change follow the parent. Printer tools (set_fan, adjust_print_temp, set_print_speed, set_light, preheat_chamber, set_exhaust_guard, slow_cooldown, control_print) and support volumes also need a confirmation in MentorSlicer; verify printer commands with get_printer_status. Exceptions to one dialog per command: set_exhaust_guard and slow_cooldown send repeated printer commands after ONE confirmation, and a preheat switches the bed off by itself (no dialog) when the printer stays idle. If a call fails with HTTP 409, another request or dialog is open in MentorSlicer: nothing was done; ask the user to close it, never retry a change automatically. Read-only tools, preview_profile_changes included, keep working while a confirmation dialog is open; while a change is being applied outside its dialog they wait and may time out. HTTP 503 (too many connections) is retried automatically by the sidecar; nothing was done. On the K1C the firmware ignores exhaust-off commands while the chamber is above about 35-37 C, so set_exhaust_guard only logs the re-enables there. The Print Advisor never starts a print.',
});

server.registerTool('get_print_context', {
  description: 'Read the active MentorSlicer profiles (printer, filament, process and their parents), the machine limits and capabilities (max speeds/accelerations, nozzle, auxiliary fan, chamber), the maximum bed/nozzle/chamber temperatures the printer can reach (machine_limits, read from the LAN printer or a table of known models; proposals above them are refused), the plate type of the current plate with its bed temperature keys (bed.temp_key / bed.initial_layer_temp_key), the plate custom G-code, a short summary of the selected profiles (edited_config: profile values only, never the overrides of the object selected in the 3D view; analyze_model lists those per object), and the current values of the most diagnostic writable settings. Use get_profile_values for any other setting.',
  inputSchema: z.object({}),
}, async () => {
  try {
    const context = await requestApp<PrintContext>('/v1/context');
    const editable = context.editable_profile_values ?? {};
    const summary: Record<string, unknown> = {};
    for (const profileClass of ['filament', 'process'] as const) {
      const values = editable[profileClass] ?? {};
      summary[profileClass] = Object.fromEntries(contextSummaryKeys[profileClass].filter((key) => key in values).map((key) => [key, values[key]]));
      summary[`${profileClass}_writable_key_count`] = Object.keys(values).length;
    }
    return jsonResult({ ...context, editable_profile_values: summary });
  } catch (error) {
    return errorResult(error, 'Unable to read MentorSlicer context.');
  }
});

server.registerTool('get_parameter_schema', {
  description: 'Read parameter metadata and optionally filter by owning profile class and keyword. The schema limits are metadata, not printer/material compatibility guarantees.',
  inputSchema: z.object({
    profile_class: z.enum(['process', 'filament', 'printer']).optional(),
    search: z.string().max(80).optional(),
    limit: z.number().int().min(1).max(150).default(100),
  }),
}, async ({ profile_class, search, limit }) => {
  try {
    const result = await requestApp<ParameterSchema>('/v1/schema');
    const needle = search?.trim().toLowerCase();
    const options = (result.options ?? []).filter((option) => {
      if (profile_class && option.param_class !== profile_class) return false;
      if (!needle) return true;
      return `${option.key ?? ''} ${option.label ?? ''} ${option.category ?? ''}`.toLowerCase().includes(needle);
    }).slice(0, limit);
    return jsonResult({ success: result.success, count: options.length, options });
  } catch (error) {
    return errorResult(error, 'Unable to read parameter metadata.');
  }
});

server.registerTool('get_profile_values', {
  description: 'Read current values of writable settings of the active filament or process profile, filtered by exact keys or by a keyword matched against key, label and category. Each entry includes label, unit and schema limits.',
  inputSchema: z.object({
    profile_class: z.enum(['filament', 'process']),
    keys: z.array(z.string().min(1).max(120)).max(60).optional(),
    search: z.string().max(80).optional(),
    limit: z.number().int().min(1).max(150).default(60),
  }),
}, async ({ profile_class, keys, search, limit }) => {
  try {
    const [context, schema] = await Promise.all([
      requestApp<PrintContext>('/v1/context'),
      requestApp<ParameterSchema>('/v1/schema'),
    ]);
    const values = context.editable_profile_values?.[profile_class] ?? {};
    const metadata = new Map((schema.options ?? []).map((option) => [option.key, option]));
    const needle = search?.trim().toLowerCase();
    const selected = Object.keys(values).filter((key) => {
      if (keys?.length) return keys.includes(key);
      if (!needle) return true;
      const option = metadata.get(key);
      return `${key} ${option?.label ?? ''} ${option?.category ?? ''}`.toLowerCase().includes(needle);
    }).slice(0, limit);
    const entries = selected.map((key) => {
      const option = metadata.get(key);
      return { key, value: values[key], label: option?.label ?? null, unit: option?.unit ?? null, min: option?.min ?? null, max: option?.max ?? null, type: option?.type ?? null };
    });
    const missing = keys?.filter((key) => !(key in values)) ?? [];
    return jsonResult({ profile_class, profile: context.profile_identity?.[profile_class] ?? null, count: entries.length, values: entries, not_writable_or_absent: missing });
  } catch (error) {
    return errorResult(error, 'Unable to read profile values.');
  }
});

server.registerTool('diagnose_defect', {
  description: 'Return targeted questions, plausible causes, and low-risk checks for a reported print defect. This tool does not change settings.',
  inputSchema: z.object({ defect: defectSchema, observed_symptoms: z.string().max(1000).optional() }),
}, async ({ defect, observed_symptoms }) => {
  const guide = defectGuide[defect];
  return jsonResult({ defect, observed_symptoms: observed_symptoms ?? null, follow_up_questions: guide.questions, possible_causes: guide.causes, low_risk_checks: guide.checks, confidence: 'low_to_medium', write_enabled: false });
});

server.registerTool('recommend_parameter_changes', {
  description: 'Combine a defect report with the current MentorSlicer context to suggest cautious parameter experiments. It only recommends; it never applies or saves settings.',
  inputSchema: z.object({ defect: defectSchema, observed_symptoms: z.string().max(1000).optional() }),
}, async ({ defect, observed_symptoms }) => {
  try {
    const [context, schema] = await Promise.all([
      requestApp<PrintContext>('/v1/context'),
      requestApp<ParameterSchema>('/v1/schema'),
    ]);
    const advice = recommendationPayload(defect, context, schema.options ?? []);
    return jsonResult({ ...advice, observed_symptoms: observed_symptoms ?? null });
  } catch (error) {
    return errorResult(error, 'Unable to produce a context-aware recommendation.');
  }
});

server.registerTool('preview_profile_changes', {
  description: 'Validate up to 40 changes against the active filament or process profile and return a preview. Any writable setting can be used (see get_profile_values). For vector settings, provide the complete array matching the source profile length and preserve null slots; booleans may be true/false. This creates only an in-memory proposal; it does not save or select a profile.',
  inputSchema: z.object({
    profile_class: z.enum(['filament', 'process']),
    changes: z.array(z.object({
      key: z.string().min(1).max(120),
      value: z.union([
        z.string().max(256), z.number(), z.boolean(),
        z.array(z.union([z.string().max(64), z.number(), z.boolean(), z.null()])).min(1).max(32),
      ]),
    })).min(1).max(40),
    reason: z.string().max(500).optional(),
    profile_name: z.string().min(3).max(80).optional().describe('Readable name for the new copy, e.g. "ASA K1C - prova 2"; omitted: "<source> (Advisor <time>)"'),
  }),
}, async ({ profile_class, changes, reason, profile_name }) => {
  try {
    const proposal = await requestApp<Record<string, unknown>>('/v1/proposals', { profile_class, changes, ...(reason ? { reason } : {}), ...(profile_name ? { profile_name } : {}) });
    return jsonResult(proposal);
  } catch (error) {
    return errorResult(error, 'Unable to preview profile changes.');
  }
});

server.registerTool('apply_profile_changes', {
  description: 'Request application of a previously previewed proposal. MentorSlicer shows the exact diff and requires an explicit in-app confirmation. A confirmed proposal creates and saves a separate user profile copy that inherits from the same parent as a manual "Save as" (the system profile when the source is one); the original and active selection are left untouched.',
  inputSchema: z.object({ proposal_id: z.string().min(8).max(128) }),
}, async ({ proposal_id }) => {
  try {
    const result = await requestApp<Record<string, unknown>>('/v1/approve', { proposal_id });
    return jsonResult(result);
  } catch (error) {
    return errorResult(error, 'Unable to apply the profile copy.');
  }
});

server.registerTool('rename_profile_copy', {
  description: 'Rename a filament or process profile copy created by Print Advisor (its name contains "(Advisor " or the change history shows Print Advisor created or renamed it). The copy keeps its parent and values; a selected copy can be renamed and stays selected under the new name (also on the extruders that use it). MentorSlicer asks for confirmation. Refused for other profiles, copies inherited by another profile, copies with unsaved changes, or when the new name already exists.',
  inputSchema: z.object({
    profile_class: z.enum(['filament', 'process']),
    current_name: z.string().min(1).max(300),
    new_name: z.string().min(3).max(80),
  }),
}, async ({ profile_class, current_name, new_name }) => {
  try {
    const result = await requestApp<Record<string, unknown>>('/v1/rename', { profile_class, current_name, new_name });
    return jsonResult(result);
  } catch (error) {
    return errorResult(error, 'Unable to rename the profile copy.');
  }
});

server.registerTool('rollback_profile_update', {
  description: 'Request rollback of a profile copy created by this server session. MentorSlicer requires in-app confirmation and refuses if the copy is active, modified, protected, or used as a parent profile.',
  inputSchema: z.object({ operation_id: z.string().min(8).max(128) }),
}, async ({ operation_id }) => {
  try {
    const result = await requestApp<Record<string, unknown>>('/v1/rollback', { operation_id });
    return jsonResult(result);
  } catch (error) {
    return errorResult(error, 'Unable to roll back the profile copy.');
  }
});

server.registerTool('configure_heat_soak', {
  description: 'Prepare a filament profile change that warms the chamber of an enclosed printer before printing (ASA, ABS, PC): before the printer start G-code, the bed is heated and held for the given minutes with the nozzle cold (no oozing) and the exhaust fan off. Optionally also sets the exhaust (back) fan speed during the print, to keep the heat in. Returns a preview proposal: call apply_profile_changes with its proposal_id to save it as a profile copy after in-app approval. minutes = 0 disables the heat soak.',
  inputSchema: z.object({
    minutes: z.number().int().min(0).max(60),
    bed_temp: z.number().int().min(0).max(120).optional().describe('Bed temperature during the soak; omitted or 0 uses the initial layer bed temperature of the selected plate type'),
    exhaust_fan_during_print: z.number().int().min(0).max(100).optional().describe('Back/exhaust fan speed in percent while printing'),
    reason: z.string().max(500).optional(),
    profile_name: z.string().min(3).max(80).optional(),
  }),
}, async ({ minutes, bed_temp, exhaust_fan_during_print, reason, profile_name }) => {
  try {
    const context = await requestApp<PrintContext>('/v1/context');
    const filament = context.editable_profile_values?.filament ?? {};
    // Filament settings are per-extruder vectors: write the same value in every non-null slot.
    const fill = (key: string, value: number) => {
      const current = filament[key];
      if (!Array.isArray(current))
        throw new Error(`MentorSlicer does not expose ${key}; rebuild the app with heat soak support.`);
      return { key, value: current.map((slot) => (slot === null ? null : String(value))) };
    };
    const changes = [fill('heat_soak_time', minutes), fill('heat_soak_bed_temp', bed_temp ?? 0)];
    if (exhaust_fan_during_print !== undefined)
      changes.push(fill('during_print_exhaust_fan_speed', exhaust_fan_during_print));
    const proposal = await requestApp<Record<string, unknown>>('/v1/proposals', {
      profile_class: 'filament', changes,
      reason: reason ?? `Heat soak ${minutes} min prima della stampa`,
      ...(profile_name ? { profile_name } : {}),
    });
    return jsonResult(proposal);
  } catch (error) {
    return errorResult(error, 'Unable to prepare the heat soak change.');
  }
});

server.registerTool('get_slice_result', {
  description: 'Read the current plate slice result without slicing: estimated time, layers, filament used (mm, g), time per feature, slicer warnings and whether the toolpath leaves the plate. sliced=false means the plate must be sliced again (for example after a profile or layer schedule change).',
  inputSchema: z.object({}),
}, async () => {
  try {
    const result = await requestApp<SliceResult>('/v1/slice_result');
    return jsonResult(result);
  } catch (error) {
    return errorResult(error, 'Unable to read the slice result.');
  }
});

server.registerTool('slice_and_measure', {
  description: 'Slice the current plate with the active settings and return the result (estimated time, layers, filament used, time per feature, slicer warnings). Use it before and after a change to compare. A plate already sliced with the current settings is not sliced again: its result is returned directly. An empty plate (or one with no printable object inside the plate area) is refused with code NO_OBJECTS. It does not export or send G-code. Slicing a new profile copy requires selecting it in MentorSlicer first.',
  inputSchema: z.object({ timeout_seconds: z.number().int().min(10).max(600).default(240) }),
}, async ({ timeout_seconds }) => {
  try {
    const started = await requestApp<Record<string, unknown>>('/v1/slice', {}, slowReadTimeoutMs);
    if (started.success === false)
      return jsonResult(started);
    const deadline = Date.now() + timeout_seconds * 1000;
    // already_sliced: the plate result is still valid, so it is read at once.
    if (started.already_sliced !== true)
      await delay(1500);
    while (Date.now() < deadline) {
      const result = await requestApp<SliceResult>('/v1/slice_result');
      if (result.sliced && !result.slicing_in_progress)
        return jsonResult(result);
      await delay(2000);
    }
    return { isError: true, content: [{ type: 'text', text: `Slicing did not finish within ${timeout_seconds} s; call get_slice_result later.` }] };
  } catch (error) {
    return errorResult(error, 'Unable to slice the plate.');
  }
});

server.registerTool('set_layer_schedule', {
  description: 'Program bed, nozzle and chamber temperatures and the case/chamber fan to change from a given layer onward on the current plate (e.g. bed 105 °C at layer 1 from the profile, 95 °C from layer 5, 85 °C from layer 30). It uses the plate custom G-code shown on the preview layer slider, not the profiles; MentorSlicer shows the schedule and requires in-app confirmation. Each call replaces the previous Print Advisor schedule; an empty list removes it. Layer 1 uses the profile initial-layer values. Part and auxiliary fans are recomputed by the cooling logic every layer: set them through the filament profile (fan_min_speed, fan_max_speed, full_fan_speed_layer, close_fan_the_first_x_layers, overhang_fan_speed, additional_cooling_fan_speed). chamber_temp is refused on printers without chamber temperature control (machine.support_chamber_temp_control = 0, e.g. K1C): use configure_heat_soak instead. Layers are computed from initial_layer_print_height and layer_height, so variable layer height shifts them. The plate must be sliced again afterwards.',
  inputSchema: z.object({
    entries: z.array(z.object({
      layer: z.number().int().min(2).max(10000),
      bed_temp: z.number().min(0).max(120).optional(),
      nozzle_temp: z.number().min(150).max(350).optional(),
      chamber_temp: z.number().min(0).max(80).optional(),
      case_fan: z.number().min(0).max(100).optional().describe('Case/chamber exhaust fan speed in percent'),
    })).max(30),
    reason: z.string().max(500).optional(),
  }),
}, async ({ entries, reason }) => {
  try {
    const result = await requestApp<Record<string, unknown>>('/v1/layer_schedule', { entries, ...(reason ? { reason } : {}) });
    return jsonResult(result);
  } catch (error) {
    return errorResult(error, 'Unable to program the layer schedule.');
  }
});

server.registerTool('control_print', {
  description: 'Pause, resume or stop the print on the printer selected in MentorSlicer. Each command needs a confirmation in MentorSlicer; stop cannot be undone. The command is sent asynchronously through the Device page: success means it was handed over, not that the printer executed it; the result carries a request_id: confirm with get_printer_status (print_control_results.<action> with the same request_id, then the printer state). Starting a print is not possible. Use only after checking get_printer_status and, for stop, after the user asked for it.',
  inputSchema: z.object({ action: z.enum(['pause', 'resume', 'stop']), reason: z.string().max(500).optional() }),
}, async ({ action, reason }) => {
  try { return jsonResult(await requestApp<Record<string, unknown>>('/v1/print_control', { action, ...(reason ? { reason } : {}) })); }
  catch (error) { return errorResult(error, 'Unable to control the print.'); }
});

server.registerTool('start_calibration', {
  description: 'Generate a MentorSlicer calibration test on a NEW project (the current project is closed; MentorSlicer asks to keep or discard unsaved changes) after confirmation. Tests: temperature (tower from start down to end in 5 C blocks of 10 mm; start and end must be multiples of 5, start at most 350, end at least 170 and at most start - 5, and stay within the filament nozzle range and the printer maximum), flow (pass 1: patches -20..+20; pass 2: -9..0 around the pass-1 result given as flowrate), pressure_advance (tower, start/end/step, one step per whole mm, end at most 2), retraction (tower, one step per mm above 0.6 mm, end at most 10 mm), max_volumetric_speed (mm3/s, one step per mm, end at most 60). Step bounds: pressure_advance step 0.001-0.5, retraction step 0.01-2 mm, max_volumetric_speed start at least 0.5 and step 0.1-10. The tower must fit the printer printable_height (temperature: 10 mm per 5 C block; the other towers: one mm per step plus a base), otherwise the test is refused. Values outside these limits are refused. The generators leave the edited filament/process profiles with unsaved changes, some beyond the Print Advisor limits (e.g. the max volumetric speed test sets filament_max_volumetric_speed very high), so profile changes are refused until the user reselects the profiles discarding them; max_volumetric_speed also switches the selected filament to the preset of extruder 1. The new project replaces the current one (MentorSlicer asks to save it). The result tells how to read the printed test. Recommended order: temperature, flow, pressure advance, retraction, max volumetric speed. After printing, ask targeted questions (which block/patch/height is best, optionally a photo), compute the value with compute_calibration_result, ask the user to reselect the profile discarding the test changes, then save with preview_profile_changes and apply_profile_changes.',
  inputSchema: z.object({
    test: z.enum(['temperature', 'flow', 'pressure_advance', 'retraction', 'max_volumetric_speed']),
    start: z.number().optional(), end: z.number().optional(), step: z.number().optional(),
    pass: z.number().int().min(1).max(2).optional(), flowrate: z.number().min(0.8).max(1.2).optional(),
  }),
}, async (args) => {
  try { return jsonResult(await requestApp<Record<string, unknown>>('/v1/calibration', args)); }
  catch (error) { return errorResult(error, 'Unable to start the calibration test.'); }
});

server.registerTool('compute_calibration_result', {
  description: 'Turn the answer about a printed calibration test into the value to save, with the profile key it belongs to. temperature: block_index (0 = bottom block); flow: patch_number (the number printed on the best patch, negative for m) and base_flow_ratio (the flow ratio of the profile used for the test, times the pass-1 result for pass 2); pressure_advance / retraction / max_volumetric_speed: height_mm where the result is best. Formulas: pressure_advance = start + step * floor(height_mm); retraction = start + step * floor(height_mm - 0.6); max_volumetric_speed = (start + step * height_mm) * 0.9.',
  inputSchema: z.object({
    test: z.enum(['temperature', 'flow', 'pressure_advance', 'retraction', 'max_volumetric_speed']),
    start: z.number().optional(), step: z.number().optional(),
    block_index: z.number().int().min(0).optional(), patch_number: z.number().optional(),
    base_flow_ratio: z.number().min(0.5).max(1.5).optional(), height_mm: z.number().min(0).optional(),
  }),
}, async ({ test, start, step, block_index, patch_number, base_flow_ratio, height_mm }) => {
  const need = (value: number | undefined, name: string) => {
    if (value === undefined) throw new Error(`${name} is required for the ${test} test`);
    return value;
  };
  try {
    const round = (value: number, digits: number) => Math.round(value * 10 ** digits) / 10 ** digits;
    let value: number; let key: string; let note = '';
    switch (test) {
      case 'temperature':
        value = need(start, 'start') - 5 * need(block_index, 'block_index'); key = 'nozzle_temperature';
        note = 'Also set nozzle_temperature_initial_layer, usually equal or 5 C higher.'; break;
      case 'flow':
        value = round(need(base_flow_ratio, 'base_flow_ratio') * (1 + need(patch_number, 'patch_number') / 100), 3); key = 'filament_flow_ratio'; break;
      case 'pressure_advance':
        // The tower changes value once per whole millimetre: GCode.cpp sets start + int(print_z) * step.
        value = round(need(start, 'start') + need(step, 'step') * Math.floor(need(height_mm, 'height_mm')), 4); key = 'pressure_advance';
        note = 'pressure_advance = start + step * floor(height_mm), as the tower G-code (one value per whole mm). enable_pressure_advance must be on in the filament profile.'; break;
      case 'retraction':
        value = round(need(start, 'start') + need(step, 'step') * Math.max(0, Math.floor(need(height_mm, 'height_mm') - 0.6)), 2); key = 'filament_retraction_length';
        note = 'filament_retraction_length is a filament override: if it is null in the profile, enable it in MentorSlicer first (or set retraction in the printer profile manually).'; break;
      default:
        value = round((need(start, 'start') + need(step, 'step') * need(height_mm, 'height_mm')) * 0.9, 1); key = 'filament_max_volumetric_speed';
        note = 'Includes a 10% safety margin below the height where the wall degraded.';
    }
    return jsonResult({ success: true, test, profile_key: key, value, note, profile_class: 'filament' });
  } catch (error) { return errorResult(error, 'Unable to compute the calibration result.'); }
});

// What to do about the advisory ASA/ABS findings of /v1/preflight (code -> advice shown with the result).
const preflightAdvice: Record<string, string> = {
  glue_on_smooth_pei_large_part: 'Large ASA/ABS part on a smooth PEI plate: ask the user to apply glue stick on the plate before sending (on the K1C a 130 mm ASA part detached at 57% without glue and printed well with it). Alternatives: a textured PEI plate (set_bed_type textured_plate, then slice again) or a wider brim on 2-3 layers.',
  chamber_cold_preheat: 'The live chamber reading is below 30 °C and the printer is idle: run preheat_chamber BEFORE sending the print (data.suggested_preheat is the K1C estimate, lid closed, exhaust fan off) and make sure the filament profile has a heat soak of at least data.suggested_soak_minutes minutes (configure_heat_soak): 30 minutes below 20 °C, 15 minutes between 20 and 30 °C, the same figures as the cold_room/cool_room findings. In a cold room the bed alone brings the K1C chamber to about 30 °C in 20-25 minutes and no further.',
  chamber_unknown: 'No live chamber reading (not reported when printer_data_stale already says the telemetry is old): ask the user to open the Device tab of MentorSlicer, then read get_printer_status; do not guess the chamber temperature. Below 30 °C suggest preheat_chamber before sending.',
};

server.registerTool('preflight_check', {
  description: 'Checks before printing: printer temperature limits, filament range, material vs plate type, abrasive filament vs brass nozzle, heat soak with low-temperature materials, back/part fan for ASA/ABS, speeds and accelerations vs machine limits, the G-code of the current slice (temperature commands, heat soak block, dwell time, slicer warnings, toolpath outside the plate), and for ASA/ABS the advisory findings glue_on_smooth_pei_large_part (smooth PEI plate with a part over 80 mm), chamber_cold_preheat (live chamber under 30 °C on an idle printer: preheat_chamber and a heat soak of 30 min below 20 °C or 15 min at 20-30 °C, like cold_room/cool_room) and chamber_unknown (no telemetry, unless printer_data_stale was already reported). Returns ok_to_print, findings with severity error/warning/info (advisory ones also carry data) and a top-level advice map (code -> text) added by the sidecar for the known advisory codes. Read-only.',
  inputSchema: z.object({}),
}, async () => {
  try {
    const result = await requestApp<Record<string, unknown>>('/v1/preflight', {}, guiReadTimeoutMs);
    const findings = Array.isArray(result.findings) ? result.findings as Array<Record<string, unknown>> : [];
    const advice = Object.fromEntries(findings.map((finding) => String(finding.code)).filter((code) => code in preflightAdvice).map((code) => [code, preflightAdvice[code]]));
    return jsonResult(Object.keys(advice).length ? { ...result, advice } : result);
  } catch (error) { return errorResult(error, 'Unable to run the pre-print checks.'); }
});

server.registerTool('analyze_model', {
  description: 'Analyze the objects on the current plate: size, volume, mesh health (manifold, open edges), overhang estimate from the mesh (unsupported and downward area ratios, max overhang angle, bed contact area), per-object overrides, and support hints from the last slice. Use it to suggest orientation, supports, brim or per-object settings. Read-only.',
  inputSchema: z.object({}),
}, async () => {
  try { return jsonResult(await requestApp<Record<string, unknown>>('/v1/model_analysis', undefined, guiReadTimeoutMs)); }
  catch (error) { return errorResult(error, 'Unable to analyze the model.'); }
});

server.registerTool('arrange_plate', {
  description: 'Run MentorSlicer auto-orient or auto-arrange on the current plate, after confirmation in the app (undo with Ctrl+Z). Slice again afterwards.',
  inputSchema: z.object({ action: z.enum(['orient', 'arrange']) }),
}, async ({ action }) => {
  try { return jsonResult(await requestApp<Record<string, unknown>>('/v1/arrange', { action })); }
  catch (error) { return errorResult(error, 'Unable to orient or arrange the plate.'); }
});

server.registerTool('set_object_settings', {
  description: 'Set or remove setting overrides on ONE object of the plate (object_index from analyze_model), like the object settings in the object list: e.g. more walls or a wider brim only for a large part. Only scalar process/object settings that MentorSlicer allows per object; same safety checks as profile changes. Confirmation in the app; profiles are not changed; undo with Ctrl+Z.',
  inputSchema: z.object({
    object_index: z.number().int().min(0),
    changes: z.array(z.object({ key: z.string().min(1).max(120), value: z.union([z.string().max(256), z.number(), z.boolean()]) })).max(20).optional(),
    remove: z.array(z.string().min(1).max(120)).max(20).optional(),
    reason: z.string().max(500).optional(),
  }),
}, async (args) => {
  try { return jsonResult(await requestApp<Record<string, unknown>>('/v1/object_settings', args)); }
  catch (error) { return errorResult(error, 'Unable to change the object settings.'); }
});

server.registerTool('list_profiles', {
  description: 'List user filament and process profiles with parent, whether selected, created by Print Advisor, inherited by other profiles, and compatible with the selected printer.',
  inputSchema: z.object({}),
}, async () => {
  try { return jsonResult(await requestApp<Record<string, unknown>>('/v1/profiles')); }
  catch (error) { return errorResult(error, 'Unable to list the profiles.'); }
});

server.registerTool('compare_profiles', {
  description: 'Show only the settings that differ between two filament or process profiles (system or user), with labels.',
  inputSchema: z.object({ profile_class: z.enum(['filament', 'process']), a: z.string().min(1).max(300), b: z.string().min(1).max(300) }),
}, async (args) => {
  try { return jsonResult(await requestApp<Record<string, unknown>>('/v1/profile_diff', args, guiReadTimeoutMs)); }
  catch (error) { return errorResult(error, 'Unable to compare the profiles.'); }
});

server.registerTool('delete_profile_copy', {
  description: 'Delete an unused profile copy created by Print Advisor (also from earlier sessions, via the change history). Refused if selected, used by an extruder, or inherited by another profile. Confirmation in the app; cannot be undone.',
  inputSchema: z.object({ profile_class: z.enum(['filament', 'process']), name: z.string().min(1).max(300) }),
}, async (args) => {
  try { return jsonResult(await requestApp<Record<string, unknown>>('/v1/delete_profile', args)); }
  catch (error) { return errorResult(error, 'Unable to delete the profile.'); }
});

server.registerTool('export_profile', {
  description: 'Export a filament or process profile with its resolved values to a JSON file; MentorSlicer opens a save dialog where the user chooses the location.',
  inputSchema: z.object({ profile_class: z.enum(['filament', 'process']), name: z.string().min(1).max(300) }),
}, async (args) => {
  try { return jsonResult(await requestApp<Record<string, unknown>>('/v1/export_profile', args)); }
  catch (error) { return errorResult(error, 'Unable to export the profile.'); }
});

server.registerTool('get_change_history', {
  description: 'Persistent log of what Print Advisor changed (profile copies created/renamed/deleted with changes and reasons, object settings, layer schedules, calibrations, print controls), newest last.',
  inputSchema: z.object({}),
}, async () => {
  try { return jsonResult(await requestApp<Record<string, unknown>>('/v1/history')); }
  catch (error) { return errorResult(error, 'Unable to read the change history.'); }
});

server.registerTool('get_material_guide', {
  description: 'Typical starting values and pitfalls for a material: PLA, PETG, ASA, ABS, TPU, PA, PC, CF_GF (fiber-filled). General guidance: the filament data sheet and the printer limits take precedence.',
  inputSchema: z.object({ material: z.enum(['PLA', 'PETG', 'ASA', 'ABS', 'TPU', 'PA', 'PC', 'CF_GF']) }),
}, async ({ material }) => jsonResult({ success: true, material, ...materialGuide[material] }));

registerDeviceTools(server);
registerMonitorTools(server);
registerPlateTools(server);
registerSupportTools(server);
registerArchiveTools(server);

void serveStdio(() => server);
