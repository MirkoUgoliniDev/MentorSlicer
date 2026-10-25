# Handoff: Print Advisor MCP per Creality Print

## 1. Obiettivo

Integrare in Creality Print un modulo circoscritto che aiuti Claude Code o Codex a consigliare parametri di stampa in base a:

- stampante e profilo attivi;
- filamento e profilo di processo;
- difetto osservato, descritto dall'utente (in futuro anche con immagini).

L'assistente deve spiegare il ragionamento, proporre pochi cambi mirati, indicare confidenza e modo di verificare il risultato. Il prodotto completo deve poter analizzare e aggiornare in modo mirato le impostazioni di un profilo di filamento o di processo specifico (per esempio temperature, raffreddamento, retrazione o velocità, se il parametro appartiene a quel profilo e la combinazione stampante/materiale lo supporta). Le modifiche devono essere mostrate, approvate esplicitamente in Creality Print e salvate come preset utente nuovo o copia separata, senza sovrascrivere preset di sistema o modificare profili non selezionati. Ogni modifica deve poter essere annullata.

Lo sviluppo procede per livelli: prima lettura e raccomandazioni senza scritture; poi anteprima e scrittura controllata di una copia del profilo; infine persistenza, selezione facoltativa del profilo aggiornato e rollback. La scrittura mirata di profili è un requisito del rilascio funzionale, non un obiettivo da lasciare indefinitamente fuori scope.

**Fuori ambito per l'MVP:** invio o avvio di stampe, monitoraggio dispositivi, sostituzione del motore di slicing o del sistema profili, provider LLM incorporato nell'app.

## 2. Principi di progetto

1. **Impatto ristretto:** nuovo modulo in una cartella dedicata, un adattatore verso le API interne esistenti, pochi punti d'aggancio al lifecycle e una modifica al build system. Non riscrivere i flussi esistenti.
2. **Riuso:** leggere e applicare configurazione tramite le azioni già presenti dove possibile, senza duplicare l'accesso a `PresetBundle`.
3. **Separazione:** la logica di diagnosi e patch non dipende da MCP, UI o provider LLM. MCP è un adattatore di trasporto.
4. **Consenso e integrità dei profili:** la prima milestone è read-only; il prodotto completo supporta scrittura controllata solo dopo conferma in GUI, allowlist, validazione, copia utente e rollback. Mai sovrascrivere preset di sistema o modificare implicitamente un altro profilo.
5. **Trasparenza:** distinguere regole verificate da euristiche; esporre assunzioni, limiti e confidenza, mai presentare una diagnosi probabilistica come certezza.

## 3. Architettura

```text
Claude Code / Codex (host MCP e modello LLM)
        | MCP su stdio
        v
Adattatore MCP locale (processo avviato dal client)
        | API JSON locale autenticata, loopback
        v
Print Advisor integrato in Creality Print
        | API interne esistenti / thread GUI
        +-- contesto attivo: stampante, filamento, processo, parametri
        +-- catalogo difetti e domande diagnostiche
        +-- generazione/validazione raccomandazioni e patch
        +-- anteprima e consenso in-app
        +-- copia e aggiornamento mirato di preset utente
        +-- snapshot, persistenza e rollback
```

Claude Code o Codex forniscono il modello e il ciclo di tool calling. Creality Print non deve contenere un LLM né dipendere da un particolare fornitore AI. Il processo MCP parla MCP col client e un protocollo locale ristretto con Creality Print: i due protocolli restano separati.

Il servizio applicativo deve ascoltare solo su loopback, usare un token casuale/sessione e terminare con l'app. Se la verifica del repository trova un trasporto locale già presente e adatto, riusarlo. Evitare una dipendenza HTTP nuova senza aver controllato Boost e le librerie già incluse.

### Stato dell'implementazione

È stato realizzato un primo vertical slice in `src/slic3r/GUI/simple/printAdvisor/` e `tools/print-advisor-mcp/`:

- l'utente abilita il servizio dal menu **AI Integration → Print Advisor MCP** (pulsante Enable nella finestra); è disattivato per default;
- Creality Print avvia un endpoint HTTP dedicato su IPv4 loopback, con token casuale e descrittore di sessione in `data_dir()/cache/print-advisor/endpoint.json`;
- l'endpoint espone `GET /v1/context`, `GET /v1/schema` e POST locali autenticati per proposta, approvazione e rollback; inoltra le operazioni al thread GUI;
- il sidecar MCP usa l'SDK TypeScript ufficiale su stdio e pubblica tool di lettura/diagnosi più `preview_profile_changes`, `apply_profile_changes` e `rollback_profile_update`;
- la scrittura è limitata a un'allowlist di chiavi nei preset filamento/processo; gli scalari accettano un valore e i vettori numerici richiedono l'intero array, lunghezza invariata e gli stessi slot nulli; il preview non scrive, l'approvazione richiede conferma modale nella GUI, il salvataggio crea una copia utente senza selezionarla;
- il rollback richiede conferma e rifiuta la rimozione se la copia è attiva, protetta, modificata dopo la creazione o usata come preset genitore;
- la proposta cattura i preset e le configurazioni attive e viene rifiutata se il contesto cambia prima dell'approvazione;
- il contesto mantiene i limiti legacy per override per-oggetto, ma include i valori vettoriali completi delle sole chiavi scrivibili; le raccomandazioni non impongono valori numerici universali.

La prima implementazione è scritta, ma deve ancora essere compilata e provata con Claude Code o Codex. Restano da verificare build, round-trip reale dei preset, GUI modale, salvataggio e rollback sui sistemi supportati. Le proposte e gli ID di rollback vivono in memoria e scadono alla chiusura dell'app/endpoint.

## 4. Ricognizione del codice e punti di riuso

Verificare firme, thread-safety, lifecycle e semantica effettivi prima di implementare:

- `README.md` (sezione Command line slicing): CLI per slicing di `.3mf`/modelli e caricamento profili. Utile per prove successive; non basta per leggere la sessione attiva.
- `src/slic3r/GUI/simple/bridge/SlicerBridgeActionsConfig.cpp`: `DoGetPresets`, `DoGetEditedConfig`, `DoApplyConfig` e schema configurazione.
- `src/slic3r/GUI/simple/bridge/SlicerBridgeActionRegistry.cpp`: registrazione delle azioni.
- `src/slic3r/GUI/simple/bridge/SlicerAction.hpp`, `SlicerBridge.hpp`: identificativi e interfacce delle azioni.
- La chat IA interna di Creality (`simple/MCPChatPanel`, `simple/toolcalls/`, `bridge/CxAgentClientBridge`, `bridge/SAgentMqttBridge`) è stata rimossa nella fase F3 del ramo `lean`: non era un server MCP standard per Claude Code/Codex e il Print Advisor non la usava.
- `src/slic3r/CMakeLists.txt`: elenco sorgenti `GUI_SIMPLE`.
- Lifecycle di `GUI_App` e applicazione: individuare il punto corretto per avvio/arresto del servizio.
- `src/slic3r/GUI/simple/bridge/MCP_Chat_Dify_Integration_2447a5bd.md` (rimosso in F3 insieme alla chat): vecchia proposta API localhost/Dify, un documento di design, non un server MCP.

Controllare se `DoApplyConfig` applica subito o modifica solo la configurazione attiva, come salva i preset, come si crea/copia/rinomina un preset utente, come vengono aggiornate selezione e dirty state, se genera gli eventi UI corretti e se offre undo. Determinare in modo esplicito quali chiavi appartengono al preset filamento, al preset processo o al preset stampante: la collocazione dipende dallo schema Creality Print, non dal nome intuitivo del parametro. Non esporre direttamente un'azione interna di mutazione al server prima di aver definito approvazione, destinazione del preset, persistenza atomica e rollback.

## 5. Modulo lato Creality Print

Cartella indicativa: `src/slic3r/GUI/simple/printAdvisor/`.

Componenti suggeriti, con nomi da adattare alle convenzioni del repository:

- `PrintContextProvider`: crea una fotografia coerente dei soli dati attivi necessari alla diagnosi.
- `DefectCatalog`: difetti, sintomi, domande di chiarimento e parametri pertinenti.
- `RecommendationEngine`: produce raccomandazioni strutturate da regole verificabili e contesto; non deve modificare configurazioni.
- `ParameterPatch`: valida chiavi, tipo, unità, intervalli, appartenenza al preset e compatibilità; produce un diff per uno specifico preset target.
- `ProfileUpdateService`: nella fase di scrittura, crea una copia utente del profilo target, applica solo le chiavi approvate e salva tramite le API ufficiali di preset.
- `SnapshotStore`: registra preset sorgente, preset derivato, valori precedenti e nuovi, hash/versione e operazione per ripristino/audit locale.
- `PrintAdvisorService`: API di dominio indipendente dal trasporto MCP e dalla UI.
- `PrintAdvisorLocalEndpoint`: adattatore locale autenticato; dispatch sul thread GUI tramite i meccanismi supportati dall'app.

Possibile modello dati del contesto:

```json
{
  "context_id": "snapshot-versioned-id",
  "printer": {"preset_id": "...", "model": "..."},
  "filament": {"preset_id": "...", "material": "PLA", "brand": "..."},
  "process": {"preset_id": "..."},
  "parameters": {"nozzle_temperature": 210, "retraction_length": 0.8},
  "units": {"nozzle_temperature": "C", "retraction_length": "mm"}
}
```

Esportare solo parametri pertinenti e unità esplicite. Includere per ogni parametro l'identificativo/categoria di preset che lo possiede, quando è modificabile e i limiti compatibili noti. Evitare percorsi utente, identificativi cloud, credenziali, cronologia non necessaria e intero dump del profilo quando non serve.

## 6. Interfaccia MCP lato Claude Code / Codex

Strumenti implementati o previsti per il prossimo hardening:

1. `get_print_context` — legge contesto e parametri attivi in sola lettura.
2. `diagnose_defect` — riceve difetto e dettagli; restituisce domande mancanti e cause possibili.
3. `recommend_parameter_changes` — produce pochi suggerimenti strutturati con parametro, valore attuale, valore/intervallo proposto, motivazione, confidenza, presupposti e metodo di verifica.
4. `preview_profile_changes` — implementato: valida e mostra un diff legato ai preset attivi; non scrive.
5. `apply_profile_changes` — implementato: richiede conferma nella GUI e crea una copia utente senza selezionarla.
6. `rollback_profile_update` — implementato: richiede conferma e rimuove solo la copia generata, se è ancora inattiva e invariata.
7. La selezione della copia resta separata e al momento manuale tramite la UI esistente di Creality Print.

I nomi e gli schemi finali vanno convalidati con SDK e versioni MCP selezionate durante l'implementazione. L'adattatore deve restituire errori leggibili (contesto cambiato, preset non disponibile, parametro non ammesso, app chiusa, timeout) e non bloccare le richieste sincrone in attesa della GUI.

### Setup dei client

Consegnare una guida con prerequisiti, installazione del sidecar, comando di avvio, percorso/configurazione MCP per Claude Code e Codex, verifica connessione e risoluzione problemi. Verificare la configurazione supportata dalle versioni correnti dei client al momento del rilascio; non copiare esempi non verificati. La configurazione non deve contenere segreti statici: il token locale si negozia per sessione o viene conservato con le protezioni di sistema.

Il server MCP descrive gli strumenti, ma non forza da solo le abitudini del modello. Includere istruzioni d'uso esemplificative: chiedere chiarimenti quando servono, non inventare valori non forniti, spiegare l'incertezza, proporre modifiche graduali e non invocare strumenti mutativi senza consenso esplicito. Per ogni scrittura il modello deve nominare il profilo specifico da derivare e mostrare le modifiche prima di chiedere approvazione; non deve chiedere genericamente di "aggiornare il profilo" senza identificare target e valori.

## 7. Logica diagnostica MVP

Iniziare con un catalogo limitato:

- stringing;
- warping;
- scarsa adesione al piatto;
- sottoestrusione/sovraestrusione;
- blob e zits;
- bridging debole.

Per ogni difetto documentare: sintomi distintivi, cause alternative, parametri pertinenti, informazioni da chiedere (ad esempio materiale asciutto, temperatura ambiente, ventola, superficie e posizione del difetto), prove a basso rischio e limiti. Cambiare pochi parametri per iterazione. Produrre intervalli iniziali prudenti legati a stampante, ugello e materiale, non valori universali.

Foto e visione sono fase successiva: prevedere un campo opzionale per riferimenti immagine, ma non trasferire o conservare immagini nel MVP. Verificare compatibilità e comportamento privacy dei client prima di attivare input multimodale.

## 8. Flusso di aggiornamento mirato del profilo

1. Leggere il contesto e restituire `context_id`/versione.
2. Risolvere ogni modifica verso il preset proprietario corretto: profilo filamento/materiale oppure profilo processo. Un parametro può essere in una categoria diversa da quella che il suo nome suggerisce; usare lo schema reale del progetto.
3. Creare una proposta riferita a un ID/versione di preset e a un contesto; mostrare in Creality Print nome e tipo del profilo sorgente, nome proposto per la copia, tabella valore prima/dopo, unità, motivazione e avvertenze.
4. L'utente approva o rifiuta esplicitamente nella GUI. L'approvazione del tool da parte del client MCP o la frase interpretata dal modello non sostituiscono il consenso in-app.
5. Verificare contesto e preset invariati, preview non scaduta, target modificabile, chiavi allowlistate e valori entro limiti. Rifiutare patch stale, miste o ambigue.
6. Salvare snapshot; creare una copia utente senza toccare il preset di origine; applicare soltanto i campi approvati alla copia usando le API canoniche di preset e persistenza.
7. Rileggere il preset salvato per verificare i valori, aggiornare lo stato UI e restituire nome/ID, parametri cambiati e rollback id.
8. Se richiesto separatamente, selezionare la copia. Non selezionarla automaticamente al solo salvataggio.
9. Consentire rollback: ripristinare la configurazione precedente e rimuovere o marcare come annullata la copia derivata secondo il comportamento previsto dalle API. Non cancellare profili che nel frattempo sono stati modificati dall'utente.

La modifica del preset attivo deve essere una derivazione identificabile, non un edit in-place invisibile. Preset di sistema, preset originali e profili non target restano intatti. Se il progetto/app non consente di clonare o salvare in modo affidabile un tipo di preset, segnalare il limite e non degradare silenziosamente a modifica in-place.

## 9. Sicurezza e affidabilità

- Bind loopback esclusivamente; autenticazione per sessione; rifiuto di origine remota.
- Limitare metodi, dimensioni payload, timeout, frequenza e numero di sessioni.
- Usare allowlist esplicita di parametri modificabili, mappatura canonica parametro→preset proprietario e vincoli specifici per macchina/materiale; rifiutare chiavi arbitrarie, parametri read-only e patch che attraversano preset diversi senza anteprime separate.
- Non consentire al modello di scegliere percorsi locali o invocare comandi shell.
- Nessuna credenziale o token cloud in prompt, risultati MCP o log.
- Validare schema e tipi JSON; gestire app in chiusura, GUI non pronta, progetto/preset cambiato, richieste duplicate e conflitti durante il salvataggio.
- Le chiamate che leggono/scrivono lo stato UI passano sul thread GUI; rete e parsing non bloccano quel thread.
- Il servizio è opt-in o chiaramente visibile, si chiude con l'app e restituisce errori se non disponibile.

## 10. Fasi e deliverable

### Fase 0 — Verifica tecnica

- Mappare lifecycle, thread dispatch, API di preset, schema e applicazione config.
- Scegliere trasporto loopback e SDK MCP compatibili con i target.
- Confermare packaging sidecar su Windows/macOS/Linux.
- Deliverable: nota architetturale breve, inventario API e dipendenze; nessuna modifica funzionale.

### Fase 1 — Vertical slice read-only

- Aggiungere il modulo e un endpoint che restituisce contesto minimo dalla sessione attiva.
- Implementare sidecar MCP e tool `get_print_context`.
- Documentare setup iniziale di Claude Code e Codex.
- Deliverable: un client legge profili reali dell'app senza poterli modificare.

### Fase 2 — Diagnosi e raccomandazioni

- Aggiungere catalogo difetti e tool diagnostici/raccomandazioni.
- Limitare il contesto ai campi necessari, includere unità e provenienza.
- Deliverable: consigli riproducibili e spiegabili, ancora read-only.

### Fase 3 — Anteprima e approvazione (implementazione iniziale, da verificare)

- Implementare anteprima di aggiornamento associata a uno specifico preset filamento o processo e UI in-app per confrontare valori.
- Rendere espliciti profilo sorgente, profilo derivato, categoria target, parametro, valore prima/dopo e motivazione.
- Gestire contesto versionato, identità/hash del preset e scadenza della preview.
- Deliverable: l'utente approva o rifiuta un aggiornamento mirato senza mutazioni accidentali.

### Fase 4 — Applicazione e rollback (implementazione iniziale, da verificare)

- Implementare creazione di preset utente derivato per filamento/processo, allowlist, limiti, snapshot, persistenza, rilettura di verifica e rollback.
- Separare applicazione della copia dalla selezione del preset attivo; la selezione richiede un'azione distinta e conferma esplicita.
- Aggiungere slicing di verifica opzionale usando la copia approvata, senza invio alla macchina.
- Deliverable: modifica persistente, mirata e reversibile di uno specifico profilo, senza alterare l'origine.

### Fase 5 — Qualità e rilascio

- Packaging multipiattaforma, documentazione finale e osservabilità senza dati sensibili.
- Valutare immagini solo dopo verifica tecnica e privacy.

## 11. Criteri di accettazione

- Il contesto restituito corrisponde ai preset attivi nella GUI e riporta unità/versione.
- Il tool MCP è utilizzabile almeno dai client target documentati senza provider LLM nell'app.
- La milestone read-only non modifica profili e non invia né avvia stampe.
- Il rilascio funzionale può creare una copia utente di un preset filamento o processo specifico e modificarne solo i parametri approvati.
- Preset di sistema, sorgente e altri profili rimangono invariati; il profilo derivato viene salvato e riletto correttamente.
- La selezione del preset derivato è separata dalla scrittura e richiede richiesta/consenso distinti.
- I consigli citano solo parametri presenti e plausibili per il contesto, dichiarano incertezza e spiegano prove suggerite.
- Una richiesta con parametro, valore o contesto non valido è rifiutata con errore comprensibile.
- Senza consenso in-app non è possibile scrivere; con consenso viene creato/aggiornato solo il preset derivato target, lo snapshot è disponibile e il rollback funziona.
- Una patch diretta a parametri che appartengono a categorie di preset diverse viene separata e mostrata chiaramente oppure rifiutata; non viene reindirizzata implicitamente.
- Il servizio non è raggiungibile dalla rete e non espone segreti nei log o nel payload MCP.
- L'app chiude correttamente listener, worker e connessioni allo shutdown.
- Sono documentati installazione, configurazione e troubleshooting per Claude Code e Codex.

## 12. Test da prevedere durante l'implementazione

Non sono stati eseguiti test come parte della stesura di questo handoff. L'implementatore dovrà aggiungere/eseguire verifiche per:

- serializzazione e riduzione del contesto; unità, preset mancanti e profilo multiplo;
- diagnosi per ciascun difetto e domande quando mancano dati;
- parametri allowlistati, limiti, tipi, valori non finiti e patch vuote;
- proprietà dei parametri tra preset filamento/processo/stampante e rifiuto di target non supportati;
- contesto/preset cambiato o preview scaduta tra lettura e applicazione;
- conferma in-app, clonazione, salvataggio e rilettura del preset derivato;
- prova che preset di sistema e preset fratelli non cambino;
- snapshot e rollback, incluso conflitto se la copia è stata modificata dopo l'applicazione;
- concorrenza tra richieste, timeout e shutdown;
- accesso loopback e rifiuto connessione remota;
- smoke test di connessione dai client Claude Code e Codex sulle versioni supportate;
- regressione del flusso esistente dei preset e della GUI.

## 13. Rischi e mitigazioni

| Rischio | Mitigazione |
|---|---|
| Il bridge restituisce impostazioni diverse da quelle attive o incomplete | Verificare semantica e punto temporale delle API; includere identificativo/versione contesto e test con sessioni reali. |
| Accesso concorrente alla GUI | Dispatch esplicito sul thread UI, timeout e snapshot coerente; nessuna attesa bloccante nel thread GUI. |
| Il modello suggerisce parametri non compatibili | Catalogo limitato, schema, allowlist e limiti; l'app valida ogni patch indipendentemente dall'LLM. |
| La modifica viene scritta nel tipo di preset sbagliato | Risolvere la proprietà dal config schema/PresetBundle e testare il mapping; mostrare sempre categoria target all'utente. |
| La modifica tocca l'originale o un altro preset | Clonare come preset utente, identificare origine e destinazione, testare che gli altri preset non cambino. |
| Applicazione su un progetto/preset nel frattempo cambiato | Legare preview a context id/hash e rifiutare patch stale. |
| Salvataggio preset non atomico o rollback sovrascrive modifiche successive | Verificare API canoniche, usare snapshot/versione e rifiutare rollback in conflitto; mai cancellare modifiche utente successive. |
| Differenze tra trasporti/implementazioni MCP dei client | Sidecar MCP standard; test e guida per versioni supportate; tenere isolato l'adattatore. |
| Diagnosi troppo sicure con pochi dati | Domande di follow-up, confidenza e cause alternative; evitare promesse di correzione garantita. |
| L'impatto cresce fino a modificare workflow esistenti | Gate di revisione: ogni file fuori dal nuovo modulo, CMake e punti d'aggancio richiede giustificazione tecnica. |

## 14. Regola per contenere lo scope

Prima di ogni cambiamento fuori da `printAdvisor/`, dal sidecar MCP, dal build system e dai punti d'aggancio/lifecycle indispensabili, l'implementatore deve documentare perché il punto d'aggancio esistente non è sufficiente. La scrittura di preset utente può richiedere un adattatore limitato al sistema profili già esistente; non rifattorizzare `PresetBundle` se le sue API bastano. Evitare modifiche a `RemotePrinterManager`, `AISendWorkflowService` e UI di invio: analisi, aggiornamento profili e slicing di verifica sono il perimetro, non la stampa remota.
