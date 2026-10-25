# Piano: aggiornare a una nuova versione di Creality Print senza perdere le modifiche

Obiettivo: quando Creality pubblica una nuova versione, riportarci sopra il Print Advisor e le altre modifiche in poche ore, senza sorprese.

## Situazione attuale (2026-10-19)

- **Base**: il nostro branch `feature/mcp-advisor` parte da `59ae8cbb`, cioè dal tag ufficiale **`v7.3.0`** (`release-260930`).
- **File nostri**: circa 8.400 righe, che in un aggiornamento quasi non creano conflitti:
  - `src/slic3r/GUI/simple/printAdvisor/*`;
  - `print_manage/App/PrintAdvisorMonitor.*`;
  - `tools/print-advisor-mcp/*`;
  - i documenti.
- **33 file di Creality modificati** (circa 1.000 righe): sono loro che possono andare in conflitto. In ordine di rischio:
  - **alto**:
    - le pagine web compilate `BZCDzYbb.js` e `BDAlyC64.js`, rigenerate a ogni versione con nomi diversi;
    - `GCode.cpp`, `PrintConfig.*` e `Tab.cpp`, cioè il motore e le schede;
  - **medio**: `WebSocketProxySession.cpp`, `DeviceTelemetryCache.*`, `PrinterMgrView.*`, `GUI_App.*`, `MainFrame.cpp`, `SendToPrinter.cpp`, `RemotePrinterManager.*`;
  - **basso**: correzioni di build (`CMakeLists`, `AppConfig.cpp`, `admesh`, `MeshBoolean`, `PresetBundle`…).

## Fase 1: preparazione, una volta sola

### 1.1 Repository e branch
- **Fork su GitHub**: `origin` diventa il fork, e il repository di Creality diventa il remote `upstream`.
- **Branch**:
  - `upstream-master`: copia esatta del `master` di Creality, mai modificata;
  - `feature/mcp-advisor`: il nostro lavoro.
- **`git config rerere.enabled true`**: Git ricorda come abbiamo risolto un conflitto e lo riapplica da solo la volta dopo.

### 1.2 Storia pulita, a temi
Ricomporre i commit del branch, oggi circa 20 e misti, in commit tematici, uno per area:
1. correzioni di build per Windows;
2. motore: heat soak e brim su più layer (con la futura distanza del brim);
3. telemetria e collegamento alla stampante (`DeviceTelemetryCache`, `SendToDevice`);
4. nucleo del Print Advisor (server, rotte, conferme);
5. monitor (preriscaldo, log, guardia, raffreddamento);
6. supporti guidati e telecamera;
7. patch web, come script (vedi 1.4);
8. sidecar MCP;
9. documentazione e strumenti (`.claude`, `.vscode`, script di build).

Così un conflitto resta chiuso nel suo tema, e un'area si può anche lasciare indietro senza toccare le altre. Il branch attuale va conservato con un tag `advisor-v7.3.0-final`.

### 1.3 Ridurre i punti di contatto con il codice Creality
Regola: nei file di Creality solo **agganci di 1–3 righe**; la logica sta nei nostri file.
- **`GUI_App` e `MainFrame`**: avvio e arresto del server e voce di menu diventano chiamate a un `PrintAdvisorHooks::on_app_start/on_shutdown/build_menu()` nei file nostri.
- **`WebSocketProxySession.cpp`**:
  - spostare `SendToDevice` e il registro delle sessioni in un nuovo file `WebSocketProxyAdvisor.cpp`;
  - in `Session` restano solo i due agganci, sessione aperta e sessione chiusa.
- **`DeviceTelemetryCache`**: vive nella cartella `print_manage/App`, ma è codice nostro. Va spostata in `printAdvisor/`, con un solo aggancio in `OnSessionEvent`.
- **`PrinterMgrView`**: un solo aggancio in `OnScriptMessage` (`advisor_handle_page_message(cmd, json)`) e una funzione per mandare messaggi alla pagina.
- **CMake**: i nostri file in `printAdvisor/PrintAdvisor.cmake`, inclusi con **una** riga da `src/slic3r/CMakeLists.txt`.
- **Motore**: nei file di Creality ogni blocco nostro va racchiuso fra `// PRINT-ADVISOR BEGIN <tema>` e `// PRINT-ADVISOR END`, così si ritrova con una ricerca.
- **Elenco dei punti di contatto**: nuovo documento `doc/MCP/upstream-touch-points.md`.
  - Contenuto: per ogni file di Creality, l'aggancio e lo scopo, più le **API di Creality che usiamo**, per esempio `Plater::changed_object`, `PresetCollection::load_preset`, `ObjectList::delete_from_model_and_list`, `SlicerBridge`, `RemotePrinterManager::hasPendingUpload`.
  - Uso: in un aggiornamento si controlla questa lista invece di tutto il codice.

### 1.4 Patch web automatiche
Le pagine `resources/web/**/assets/*.js` sono codice minificato, che Creality rigenera a ogni versione. Oggi le modifichiamo a mano.
- **Lo script**: nuovo `tools/web-patches/apply-web-patches.mjs`, che contiene le 9 patch descritte in `web-bundle-patches.md`. Per ognuna:
  - **trova il file giusto per contenuto**, non per nome (per esempio il bundle che contiene `registerHandler("print_control"`);
  - **trova il punto con un'espressione regolare** sulle stringhe stabili (testi, nomi dei comandi, chiavi JSON), non sui nomi minificati;
  - **inserisce il codice** con un segnaposto per i nomi delle variabili ricavati dal contesto;
  - è **idempotente**: riconosce il marcatore `/*PA:<id>*/` e non applica due volte.
- **Le modalità**:
  - `--check` dice quali patch si applicano e quali no, senza scrivere;
  - `--apply` scrive;
  - `--revert` ripristina l'originale da un backup.
- **Nel repository**: gli originali di Creality restano nel branch `upstream-master`; il nostro branch contiene i bundle già patchati, più lo script.
- **Al momento della build**: `build_exe_only.bat` esegue lo script in `--check` e avvisa se una patch manca.

### 1.5 Traduzioni separate
- Le nostre stringhe italiane vanno in `localization/i18n/it/PrintAdvisor_it.po`.
- In fase di build uno script (`msgcat`, o un piccolo `.mjs`) le unisce al `.po` di Creality prima di `msgfmt`.
- Così il loro `.po`, che cambia spesso, non va in conflitto.

### 1.6 Test automatici di non regressione
1. **Test di compilazione**: build più avvio dell'app in modalità di prova; lo script esistente resta.
2. **Test del server** `tools/print-advisor-mcp/scripts/smoke.mjs`:
   - avvia il sidecar, verifica i 46 tool e chiama tutte le rotte in sola lettura;
   - controlla che le chiavi segrete non escano dal contesto;
   - con stampante assente, controlla che le risposte siano gli errori previsti.
3. **Test del G-code**, in `tests/print-advisor/`:
   - un progetto `.3mf` di riferimento (cubo ASA con soak e brim su 3 layer) viene preparato in modalità riga di comando, se disponibile, oppure tramite l'API (`/v1/slice`);
   - si confrontano le righe chiave con un risultato salvato: blocco del soak, ordine rispetto a `START_PRINT`, brim ripetuto una sola volta per layer, nessun `M190`/`M140` in più.
4. **Test delle patch web**: `apply-web-patches.mjs --check` più `node --check` su ogni bundle.
5. **Lista di prove con la stampante** (manuale, 10 minuti), in `doc/MCP/printer-test-checklist.md`:
   - sola lettura: telemetria, foto;
   - un solo comando con conferma: luce spenta e riaccesa;
   - preriscaldo di 2 minuti con spegnimento.

### 1.7 Automazione con Claude
- Una skill `.claude/skills/upstream-update/` con la procedura della fase 2. Basta dire "aggiorna a Creality 7.4" e Claude esegue i passi, fermandosi a ogni conflitto e a ogni prova sulla stampante.

## Fase 2: procedura a ogni nuova versione di Creality

1. **Recuperare la nuova versione**: `git fetch upstream --tags`, poi leggere il changelog e la differenza fra il vecchio e il nuovo tag nei file dell'elenco dei punti di contatto.
2. **Creare il branch di lavoro**: `git switch -c update/v7.x upstream/v7.x`, poi `git merge feature/mcp-advisor`. Il merge si preferisce al rebase perché i conflitti si risolvono una volta sola, e rerere li ricorda.
3. **Risolvere i conflitti** seguendo l'elenco dei punti di contatto:
   - per le API di Creality cambiate si adatta il nostro aggancio, mai la loro logica;
   - per i bundle web si prendono **i loro** file nuovi, poi si passa al punto 4.
4. **Applicare le patch web**: `apply-web-patches.mjs --check`, poi `--apply`. Se un punto d'aggancio non si trova più, si aggiorna l'espressione nello script e il documento delle patch.
5. **Dipendenze**: se il nuovo tag ha cambiato `deps/`, `build_release_vs2022.bat deps`, che è lungo; altrimenti `build_exe_only.bat`.
6. **Test automatici** 1.6.1–1.6.4. Se un test fallisce, si corregge prima di andare avanti.
7. **Prove con la stampante** dalla lista 1.6.5.
8. **Chiusura**:
   - merge nel nostro branch e tag `advisor-v7.x-1`;
   - push sul fork;
   - aggiornare `CLAUDE.md` con le novità di Creality che ci riguardano.

**Tempo stimato**:
- 1–3 ore per una versione minore;
- mezza giornata se Creality ha cambiato il motore o la gestione della stampante;
- in più la build delle dipendenze, se serve.

## Fase 3: manutenzione continua
- A ogni nostra modifica a un file di Creality si aggiorna l'elenco dei punti di contatto.
- Le nuove patch web si scrivono **subito** come voci dello script, mai a mano.
- Ogni nuova opzione del motore va nello stesso commit tematico, con il suo blocco `PRINT-ADVISOR BEGIN/END`.
- Ogni tanto `apply-web-patches.mjs --check` va provato anche sulla versione installata dal sito di Creality, per accorgersi presto dei cambiamenti.

## Rischi noti
- **Creality pubblica su GitHub in ritardo** o con salti: a volte bisogna aggiornare da più versioni insieme.
- **Pagine web riscritte da zero** (nuovo framework): alcune patch vanno ripensate. Lo script lo segnala subito.
- **Cambi nel protocollo LAN della K1C** (firmware): telemetria e comandi vanno riverificati con la stampante, non bastano i test automatici.
- **Opzioni nuove con lo stesso nome** introdotte da Creality o da OrcaSlicer: si rinominano le nostre (prefisso `advisor_` per le prossime).
