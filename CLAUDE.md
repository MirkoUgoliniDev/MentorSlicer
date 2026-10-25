# CLAUDE.md

## Pulsanti nella barra di stato di VS Code

I pulsanti di build/avvio nella barra inferiore sono task definiti in `.vscode/tasks.json`.
Li visualizza l'estensione **Tasks** (`actboy168.tasks`), che legge il campo `options.statusbar` di ogni task.

Pulsanti attuali:

| Pulsante | Task | Cosa fa |
|---|---|---|
| `$(tools) Build EXE` | Build EXE MentorSlicer | `build_exe_only.bat`: solo `MentorSlicer.exe` in `build\src\Release`, senza install (task di build predefinito, Ctrl+Shift+B) |
| `$(package) Build Install` | Build Install MentorSlicer | `build_release_vs2022.bat`: build completa con target `install` in `build\OrcaSlicer` |
| `$(play) Avvia MentorSlicer` | Avvia MentorSlicer | lancia `build\src\Release\MentorSlicer.exe` |
| `$(rocket) Pubblica stabile` | Pubblica stabile MentorSlicer | `tools\publish-stable.ps1`: controlla prima exe, `mentorslicer-mcp.exe`, DLL, `mesa` e le vere `resources`; se un client MCP tiene aperto `mentorslicer-mcp.exe` nella copia stabile, lo rinomina in `.old-<data>` (cancellato alla pubblicazione successiva); chiude la copia stabile con `tools\close-build-instance.ps1`; copia la build provata in `C:\MentorSlicer` (robocopy `/R:2 /W:2`, senza l'eventuale `CrealityPrint.exe` rimasto da una build precedente a F8); scrive `stable-version.txt` come INCOMPLETA prima della copia e completa solo alla fine; crea il collegamento sul desktop "MentorSlicer" e toglie quello vecchio "Creality Print Advisor" |

La **copia stabile** è quella che l'utente usa per stampare: le build di sviluppo non la toccano finché non si preme "Pubblica stabile" dopo averle provate. Lo script chiude prima la copia stabile se è aperta (chiusura normale, forzata solo dopo 20 s) e si ferma per gli stessi motivi del task di chiusura (preriscaldo o raffreddamento lento in corso, stato non leggibile). Le due copie condividono la cartella dati (`%APPDATA%\MentorSlicer`): profili e impostazioni sono comuni, e il Print Advisor MCP resta acceso in una sola istanza alla volta: il descrittore `endpoint.json` è unico, e una seconda istanza non avvia il server (mostra un messaggio, toglie la spunta dal menu ma non cambia l'impostazione condivisa). L'utente ha disinstallato la Creality Print 7.1 ufficiale il 2026-10-20.

La vecchia copia stabile `C:\CrealityPrint-Advisor` (`CrealityPrint.exe`, cartella dati `%APPDATA%\Creality\Creality Print\7.3 Alpha`) resta di riserva fino a quando l'utente non la cancella, senza collegamento sul desktop. Dopo l'importazione di F8 le due cartelle dati **divergono**: i profili salvati in una non compaiono nell'altra. Se nella vecchia copia è attivo il Print Advisor MCP, MentorSlicer non avvia il suo server (messaggio "Print Advisor MCP è attivo nella vecchia Creality Print"). Il supporto alla vecchia copia (nome `CrealityPrint.exe` in `close-build-instance.ps1` e `publish-stable.ps1`, sonda del vecchio `endpoint.json` in `PrintAdvisorServer::check_no_other_instance`, righe `allow:` di `tools/verify/removed-F8.txt`) va tolto quando `C:\CrealityPrint-Advisor` viene cancellata.

Prima di compilare, entrambi i pulsanti di build eseguono il task nascosto **Chiudi MentorSlicer di build** (`dependsOn`). Il task esegue `tools\close-build-instance.ps1`: chiude normalmente i processi `MentorSlicer.exe` (e `CrealityPrint.exe` delle build precedenti a F8) avviati da `build*` nel repository, aspetta fino a 20 s e solo dopo li termina forzatamente (altrimenti il linker fallisce con `LNK1104` sulla DLL bloccata). Se l'istanza è quella del Print Advisor e ha un preriscaldo attivo o in attesa di spegnimento, un raffreddamento lento attivo o con la temperatura finale ancora da inviare (GET `/v1/monitor_status`), si ferma con un messaggio e la build non parte; si ferma anche se lo stato o `endpoint.json` non sono leggibili (cerca il descrittore in `%APPDATA%\MentorSlicer` e, per una build precedente a F8, nella vecchia cartella di Creality Print) o se c'è un `MentorSlicer`/`CrealityPrint` avviato come amministratore. La guardia dell'estrazione dà solo un avviso. `-Force` salta questi controlli (i task non lo passano). Non tocca una copia avviata da un'altra cartella. Se MentorSlicer chiede di salvare il progetto e nessuno risponde entro 20 s, il progetto non salvato va perso.

### Aggiungere un pulsante

Aggiungere un task a `.vscode/tasks.json` con `options.statusbar`:

```json
{
  "label": "Nome task",
  "type": "shell",
  "command": "call \"C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\Common7\\Tools\\VsDevCmd.bat\" -arch=x64 && call mio_script.bat",
  "options": {
    "cwd": "${workspaceFolder}",
    "shell": { "executable": "cmd.exe", "args": ["/d", "/c"] },
    "statusbar": { "label": "$(icona) Testo", "tooltip": "Descrizione" }
  },
  "presentation": { "panel": "dedicated", "reveal": "always" },
  "problemMatcher": ["$msCompile"]
}
```

Regole:
- Gli script di build `.bat` vanno eseguiti con `cmd.exe /d /c` dopo `VsDevCmd.bat -arch=x64`, così l'ambiente MSVC è caricato.
- Per lanciare un eseguibile si usa `"type": "process"` con `command` uguale al percorso dell'exe e `cwd` uguale alla sua cartella.
- Le icone usano la sintassi codicon `$(nome)`, per esempio `tools`, `package`, `play`, `debug`, `beaker`.
- `"statusbar": { "hide": true }` nasconde un task dalla barra.
- Dopo una modifica, se i pulsanti non si aggiornano, eseguire **Developer: Reload Window**.

## Build: cose da sapere

- **Librerie di terze parti** (Aiuto → Librerie di terze parti, `ThirdPartyLibraries.cpp`): per ogni libreria a cosa serve, versione in uso, ultima versione e stato (aggiornata, da aggiornare, lasciata apposta con il motivo, da ricontrollare). I dati di riferimento sono scritti nel codice (`reference_latest`, `THIRD_PARTY_REFERENCE_DATE`): vanno aggiornati, insieme a `version` e alla politica, a ogni aggiornamento o revisione delle librerie. Il pulsante **Verifica aggiornamenti** interroga GitHub, GitLab e NuGet solo quando lo si preme (HTTPS con verifica dei certificati di Windows, circa 33 richieste; GitHub ne concede 60 l'ora senza account) e salva il risultato in `<data_dir>/cache/third-party-libraries.json`.
- `build_exe_only.bat` compila solo `MentorSlicer_app_gui` e il catalogo italiano `.mo`, poi crea `mentorslicer-mcp.exe` (sidecar MCP impacchettato, vedi Print Advisor). Riusa la cache CMake e la rigenera da solo quando cambia un `CMakeLists.txt`; rilancia la configurazione se la cache è precedente al cambio di nome (`CMAKE_PROJECT_NAME` diverso da `MentorSlicer`). Le dipendenze devono essere già compilate (`build_release_vs2022.bat deps`).
- Una modifica a `src/libslic3r/PrintConfig.hpp` (nuove opzioni) ricompila quasi tutto il motore e l'interfaccia, e richiede molto tempo. Le altre modifiche richiedono pochi minuti.
- Con MentorSlicer aperto da `build\src\Release`, il link fallisce con `LNK1104` su `MentorSlicer_Slicer.dll`. I pulsanti di build chiudono prima l'app. Dopo la prima build di F8 in `build\src\Release` restano i vecchi `CrealityPrint.exe`, `CrealityPrint_Slicer.dll` e `.pdb`: si possono cancellare.
- Strawberry Perl nel PATH rompe la build MSVC. Gli script tolgono `C:\Strawberry\c\bin` dal PATH e `C:\Strawberry\c\include`/`lib` da INCLUDE e LIB (il `perl\bin` resta nel PATH); vedi `doc/MCP/compilation-fix-report.md`.
- **Boost senza iconv/bzip2/lzma** (dal 2026-10-25, ramo `UPDATE_LIBRARY`): su Windows `deps/Boost/Boost.cmake` passa `BOOST_LOCALE_ENABLE_ICONV=OFF` e `BOOST_IOSTREAMS_ENABLE_BZIP2/LZMA/ZSTD=OFF`, quindi i config CMake di Boost non chiedono più `Iconv`/`BZip2`/`LibLZMA` e `MentorSlicer_Slicer.dll` non importa `iconv-2.dll`. Prima Boost era stato compilato con gli header di Strawberry e servivano `iconv.lib`, `bz2.lib`, `lzma.lib` (dal vecchio pacchetto FFmpeg) e gli header copiati da `C:\Strawberry\c\include`: restano nel prefisso ma non sono più necessari. Dopo una configurazione fallita la `build\CMakeCache.txt` resta e `build_exe_only.bat` salta la configurazione (`MSB1009`): togliere la cache prima di rilanciare.
- Nella shell degli agenti `NoDefaultCurrentDirectoryInExePath=1` impedisce a `cmd` di trovare i `.bat` nella cartella corrente: usare percorsi assoluti oppure azzerare la variabile.
- I file sorgente usano CRLF. Le patch via script devono rispettare il fine riga esistente.
- **Numero di versione**: `version.inc` ripiega su `7.0.0.0` se CMake non riceve `CREALITYPRINT_VERSION` (la CI di Creality lo passa). I due script di build (`build_exe_only.bat`, `build_release_vs2022.bat`) passano `-DCREALITYPRINT_VERSION=7.3.0.0` (variabile `CP_VERSION` in testa agli script); `build_exe_only.bat` rilancia la configurazione se il valore nella cache CMake è diverso. `PROJECT_VERSION_EXTRA` non va toccato: resta "Alpha" (da `cmake/BuildInfoUtil.cmake`); dopo F8 influisce sulla cartella dati solo con un `--datadir` esplicito (`<dir>\MentorSlicer\7.3 Alpha`).
- **Nome MentorSlicer (F8)**: `version.inc` (`SLIC3R_APP_NAME`, `SLIC3R_APP_KEY`, `SLIC3R_APP_USE_FORDER`, `PROCESS_NAME` = `MentorSlicer`) dà `MentorSlicer.exe`, `MentorSlicer_Slicer.dll`, la configurazione `MentorSlicer.conf` e `Company="MentorSlicer"` nei 3MF. Restano apposta: l'intestazione G-code `; generated by Creality_Print`, l'`Application` `Creality_Print` dei 3MF, l'user agent HTTP `CrealityPrint/<ver>` (`Http.cpp`) e il riconoscimento di `; generated by CrealityPrint` (`Config.cpp`), l'user agent web `Creality-Slicer`, il tipo di host `crealityprint`, vendor e profili "Creality", il dominio gettext `CrealityPrint` (`SLIC3R_I18N_DOMAIN` in `libslic3r.h`: i cataloghi restano `CrealityPrint.mo`/`CrealityPrint_it.po`) e i nomi dei file sorgente. Le icone si chiamano `resources/images/MentorSlicer*`. Le stringhe di Creality che nominavano l'app ora dicono MentorSlicer: il loro italiano sta in `PrintAdvisor_it.po` (sezione F8), perché i msgid sono nuovi.
- **Cartella dati e importazione (F8)**: la cartella dati è `%APPDATA%\MentorSlicer` (`wxStandardPaths::GetUserDataDir()`, senza sottocartella di versione). Al primo avvio `GUI_App::init_app_config` chiama `DataMigration::import_legacy_directory` (`src/libslic3r/DataDirectoryMigration.cpp`), che copia **una volta** `%APPDATA%\Creality\Creality Print\7.3 Alpha` e non la modifica mai:
  - salta `log/`, `cache/print-advisor/` (il vecchio `endpoint.json`), `cache/system_presets/`, `print-advisor/tmp/`, `ota/`, `printers/`, `user_info.json`, `privacyInfo.json`, `applock.data`, `*.lock`, `*.tmp`; rinomina `Creality.conf`(`.bak`) in `MentorSlicer.conf`(`.bak`); verifica ogni copia con MD5;
  - copia in `%APPDATA%\.MentorSlicer.import-<n>` e poi rinomina la cartella in `%APPDATA%\MentorSlicer`; il segno di fine è `migration/imported-from-creality.json` (sorgente, data, file copiati, saltati e rinominati);
  - non importa se la destinazione ha già il segno, `MentorSlicer.conf` o `migration/manifest.json`; si rifiuta se `CrealityPrint.exe` è aperto (attende 5 s) o se la destinazione contiene altro oltre a `log`, `cache`, `applock.data`;
  - in caso di errore toglie la propria cartella di appoggio, scrive `%APPDATA%\.MentorSlicer-import-last-error.txt`, mostra una finestra di Windows (`GUI_Init.cpp`) e l'app non parte; al prossimo avvio riprova da capo. Anche gli errori di `DataMigration::initialize` ora sono mostrati;
  - `init_download_path` sposta `download_path` dentro `%APPDATA%\MentorSlicer\cloud_download_data` se puntava alla vecchia cartella;
  - per **reimportare**: chiudere MentorSlicer e cancellare (o rinominare) `%APPDATA%\MentorSlicer`; al prossimo avvio la copia viene rifatta dalla cartella di Creality Print. Su un'installazione senza dati di Creality parte l'inizializzazione normale (`DataMigration::initialize`, solo se manca `migration/manifest.json`).
- **Aggiornamenti ufficiali**: il controllo delle versioni di Creality Print e' stato tolto dal fork (fase F1, voce 2): niente controllo all'avvio, niente **Aiuto -> Controlla aggiornamenti**, niente download o installazione del pacchetto ufficiale (`AppUpdater`, `AppUpdateDialogs`, `UpdateVersionDialog`, `update_state.json`, `tools/updater` e il gancio `advisor_show_fork_update_notice` di `PrintAdvisorUpdateNotice.*` sono stati eliminati). Dal 2026-10-24 `PresetUpdater` installa solo i profili delle risorse all'avvio (`check_installed_vendor_profiles`); non esistono piu' `sync`, `config_update`, la cartella `ota/` ne' la finestra o la notifica di aggiornamento dei profili. Gli aggiornamenti si fanno ricompilando il fork.

## Print Advisor (MCP)

Assistente di stampa usato da Claude Code / Codex via MCP. L'LLM è nel client; MentorSlicer contiene solo il servizio di dominio. Il server MCP si chiama `mentorslicer-advisor` (tool `mcp__mentorslicer-advisor__*`). Dal 2026-10-25 il sidecar è distribuito come `mentorslicer-mcp.exe` accanto a `MentorSlicer.exe` (Node.js impacchettato: l'utente non installa Node né npm) e si registra una volta per tutte le cartelle con `claude mcp add mentorslicer-advisor --scope user --transport stdio -- "<cartella di MentorSlicer>\mentorslicer-mcp.exe"` (la finestra Print Advisor MCP… mostra il comando con il percorso dell'exe in uso; niente percorsi fissi nel codice). Il descrittore lo trova da solo in `%APPDATA%\MentorSlicer\cache\print-advisor\endpoint.json`; la variabile `MENTORSLICER_ADVISOR_DESCRIPTOR` serve solo per una copia avviata con `--datadir`. Le vecchie registrazioni (`node … tsx … src/index.ts` nello scope `local` del progetto, o `creality-print-advisor`) vanno tolte con `claude mcp remove mentorslicer-advisor -s local` da quella cartella (entrambe le grafie, `C:/...` e `c:/...`), altrimenti nascondono quella `user`. Documenti di riferimento:

- `doc/MCP/handoff.md`: requisiti e vincoli originali;
- `tools/print-advisor-mcp/README.md`: tool, installazione e configurazione dei client;
- `doc/MCP/web-bundle-patches.md`: patch alle pagine web compilate, da rifare a mano dopo ogni aggiornamento di Creality Print.

### Architettura

- **Sidecar MCP** `tools/print-advisor-mcp/src/index.ts` (Node 20+, stdio). Solo trasporto, schemi dei tool e testi guida (difetti, schede materiali, formule di calibrazione). La logica di dominio e le validazioni stanno nel C++.
  - **Eseguibile** `mentorslicer-mcp.exe` (Node.js single executable application, circa 90 MB, non firmato): `scripts/build-exe.mjs` impacchetta il sidecar con esbuild e lo inietta in una copia del `node.exe` della build (postject); `tools\build-mcp-exe.bat`, chiamato da `build_exe_only.bat` e `build_release_vs2022.bat`, fa `npm ci` la prima volta e lo copia in `build\src\Release` (il target `install` lo prende da lì). Si rifà solo se cambiano `src/`, `package-lock.json` o lo script. Node.js serve **solo sulla macchina di build**: senza, la build continua con un avviso e la finestra Print Advisor MCP… mostra l'installazione con Node. Un exe aperto da un client MCP non si può sovrascrivere: viene rinominato in `.old-<ora>` e cancellato dopo. Smoke test dell'exe: `node scripts/smoke.mjs --exe dist/mentorslicer-mcp.exe`.
- **API locale** `src/slic3r/GUI/simple/printAdvisor/PrintAdvisorServer.*`:
  - HTTP solo su `127.0.0.1` con token di sessione;
  - descrittore in `<data_dir>/cache/print-advisor/endpoint.json`, cancellato alla chiusura;
  - ogni richiesta viene eseguita sul thread GUI con `CallAfter`;
  - attivazione da **Integrazione AI → Print Advisor MCP…** (`print_advisor_mcp_enabled` in AppConfig, spento di default): la voce apre la finestra modale `simple/printAdvisor/AdvisorInfoDialog.*` (cos'è, cosa permette, installazione con il comando `claude mcp add` già compilato e il pulsante Copia, stato Attivo/Spento con il descrittore) con il pulsante **Abilita/Disattiva** (`advisor_set_enabled`).
- **Telemetria stampante** `src/slic3r/GUI/print_manage/App/DeviceTelemetryCache.*`, alimentata da `WebSocketProxySession::OnSessionEvent`:
  - ultimi valori LAN di ogni stampante (stato, temperature, massimi, ventole, avanzamento, errori);
  - eventi di fine stampa ed errore, con notifica Windows quando il Print Advisor è attivo;
  - i dati arrivano solo se in MentorSlicer è aperta la scheda **Dispositivo**.
- **Comandi alla stampante**:
  - `device_control` (ventole, temperature, velocità, luce), guardia dell'estrazione, raffreddamento lento e spegnimento automatico del preriscaldo: il C++ scrive direttamente sul WebSocket della stampante con `WebSocketProxy::SendToDevice`;
  - `pause`/`resume`/`stop` e l'avvio del preriscaldo (`set_bed_temp`): passano dalla pagina web del device manager con `PrinterMgrView::request_print_control`; La pagina risponde con `print_control_result` (gestito in `advisor_handle_page_message`, `PrintAdvisorPageBridge.cpp`): ogni comando ha un `request_id` restituito dal tool, e l'esito finisce in `print_control_results` (uno per azione) di `get_printer_status`, nello storico e, se fallisce, in una notifica (il comando resta asincrono).
  - in entrambi i casi serve la scheda **Dispositivo** aperta.
  - il pulsante **Preriscalda/Stop** nella card "Temp camera" della pagina Dispositivo (patch web 17, `advisor_preheat_from_page`) usa lo stesso codice di `preheat_chamber`: stessa finestra di conferma, limiti e spegnimento automatico; piatto al massimo della stampante (tetto 100 °C), `max_minutes` 60; richiede il Print Advisor attivo. Un campo facoltativo `°C` accanto al pulsante dà l'obiettivo della camera (20 °C – massimo della camera, sopra la lettura attuale; vuoto = fino al plateau); il preriscaldo finisce con "Camera a temperatura" quando lo raggiunge e la card mostra `Preriscaldo mm:ss → 35°`.
- **Richieste concorrenti**: il server serve ogni connessione su un proprio thread (massimo 8, oltre risponde HTTP 503). Le modifiche rispondono HTTP 409 se è già in corso un'altra richiesta GUI o è aperta una finestra modale o la finestra di invio alla stampante; le letture (compresa `preview_profile_changes`) funzionano anche con una finestra di conferma aperta; mentre una modifica è in corso fuori dalla sua finestra (per esempio il caricamento di un progetto) vengono rimandate a intervalli di 50 ms, così non leggono uno stato a metà. Se il client si disconnette o va in timeout la richiesta viene annullata: la finestra eventualmente aperta non applica più nulla. Il sidecar ripete da solo (fino a 3 volte) una richiesta rifiutata con 503, perché il server non l'ha letta; non ripete mai una modifica rifiutata con 409 o andata in timeout. Timeout del sidecar: 610 s per le modifiche (compreso `record_print_verdict`) e per le letture sul thread GUI (preflight, analisi modello, confronto profili, sbalzi, `get_bed_type`); 15 s per foto, diario, volumi di supporto, archivio e avvio dello slicing; 5 s per le altre letture. La classificazione usa metodo + percorso.
- **SlicerBridge** (`simple/bridge/`): servizio interno di Creality ridotto a ciò che usa il Print Advisor: preset, configurazione modificata (`edited_config.print` legge solo il profilo di processo, non le impostazioni dell'oggetto selezionato), `start_slice` (sempre il piatto corrente, senza cambiarlo), schema dei parametri e analisi degli sbalzi. Le azioni tolte (oggetti, filamenti, stampante, diagnostica, `auto_map_filaments`) si recuperano dalla storia precedente (backup locale `..\CrealityPrint-git-old`, tag `pre-cleanup-20261023`). La chat IA interna di Creality (`MCPChatPanel`, `toolcalls/`, bridge CxAgent/SAgent, `sendWorkflow/`), la libreria modelli online, ModelMall/PublishDialog e AICloudService sono state rimosse nella fase F3 del ramo `lean`; il pulsante "Compra filamento" delle pagine web è nascosto dalle patch 12/12b.
- **Niente easy mode** (fase F7 del ramo `lean`): l'interfaccia semplificata (`simple/*Simple*`, `PrintSettingsPanel`, `DeviceListSimple`, `SimpleModelMgr`, `UploadDialog`, il gizmo `GLGizmoSimplePaint`) e tutti i rami `easy_mode()` sono stati cancellati; `GUI_App::easy_mode()` non esiste più e il refresh di avvio si chiama `refresh_after_startup()`. Anche `simple/filamentMapping` è stato tolto (serviva solo ad `auto_map_filaments`). La scheda **Home** resta, ridotta a "Progetti": le patch web **14** (niente Firebase/GA, tracciamento Creality, sonda internet), **22–24** (tolto il SDK Firebase con le chiavi di Creality dalla Home, dalla pagina Dispositivo e dalla finestra di invio) e **15** (via riquadro Login, Parametri online, Guide, Centro personale, Commenti, link al negozio) sono in `tools/web-patches/patches.mjs`; lo stesso bundle `homepage` serve anche alle finestre "Aggiungi stampante/filamento". Cancellate le pagine web morte `guide/swiper`, `include`, `model`, `homepage2`, `orca`.
- **Nessun account** (fase F4 del ramo `lean`): login Creality, sincronizzazione dei profili sul cloud, upload 3mf/G-code al cloud, MQTT cloud e condivisione dei timelapse sono stati tolti insieme agli SDK Alibaba OSS e Paho MQTT. `is_login()`/`get_user()` sono stati tolti il 2026-10-24; `list_profiles` non riporta più `synced_to_cloud`; la pagina Dispositivo e la finestra di invio ricevono dal comando web `get_user` un utente vuoto (patch web 26 nasconde il riquadro "Accedi" della pagina Dispositivo) (patch web 13: la finestra di invio non chiama più il cloud senza token). `AccountDeviceMgr` è stato tolto: il file locale dei dispositivi (`deviceInfo.json`) è di `DM::DeviceMgr`; i comandi della pagina senza gestore (`set_device_relate_to_account`, `cxy_print_log`, …) vengono ignorati con un log a livello trace. I print host esterni (Obico con `PrinterCloudAuthDialog`, OctoPrint, Moonraker…) restano; SimplyPrint (OAuth) è stato tolto il 2026-10-24.

### Regole di sicurezza (non rilassarle)

- Ogni azione che cambia qualcosa chiede conferma nella finestra `AdvisorConfirmDialog`. Dopo la finestra si ricontrolla lo stato e il flag di stop del server.
- **Eccezioni esplicite** (le uniche):
  - guardia dell'estrazione e raffreddamento lento: **una** conferma, poi comandi ripetuti senza altre finestre;
  - preriscaldo: se la stampante resta inattiva, il piatto viene **spento** senza finestra (sicurezza) dopo `max_minutes` (5–120, predefinito 60), 20 min dopo il plateau (meno di 1 °C in 10 min, valutato dal minuto 15) o il target, e all'arresto del server (solo se dati recenti, meno di 60 s, danno la stampante inattiva; altrimenti `stopped_not_sent` e notifica). Se si perde la telemetria il preriscaldo finisce come `telemetry_lost` ma lo spegnimento resta in sospeso fino a 12 h: quando la stampante torna inattiva il piatto viene spento (`switched_off_after_reconnect`). Un caricamento in corso rimanda lo spegnimento al massimo di 15 min. Se la pagina Dispositivo risponde che il comando di spegnimento (`set_bed_temp` 0) è fallito, il monitor riprende il controllo e spegne il piatto direttamente sul WebSocket appena la stampante è inattiva (con notifica); se fallisce l'avvio, il preriscaldo finisce come `command_failed`. Un solo preriscaldo alla volta: uno su un'altra stampante viene rifiutato (409) finché il primo è seguito. Se parte una stampa il monitor si ferma.
  - raffreddamento lento interrotto (arresto del server, sostituzione con un'altra stampante, telemetria persa): invia la temperatura finale `to`, mai più alta dell'ultima inviata; se la stampante non è raggiungibile la invia quando torna inattiva.
- **Guardia dell'estrazione**: si arma solo su una stampa riconosciuta (`printStartTime` + file); si disarma con una nuova stampa o con un buco di telemetria oltre 2 min; si sospende al tetto della camera (`maxBoxTemp - 5`, 55 °C sulla K1C).
- **Raffreddamento lento**: il controllo "non scalda mai il piatto" si ripete dopo la finestra e subito prima di inviare `from`: `from` non può superare la temperatura impostata del piatto (o la temperatura misurata + 1 °C se il piatto è spento); termina a `to` (`to=0` spegne il piatto). Se il server si ferma a metà invia subito `to`: la sicurezza (riscaldatore spento) prevale sulla rampa lenta.
- **Calibrazioni**: torre di temperatura nell'intervallo del filamento e sotto il massimo della stampante; PA ≤ 2; retrazione ≤ 10 mm; velocità volumetrica ≤ 60 mm³/s.
- Il Print Advisor **non avvia mai una stampa**. Pausa, ripresa e stop sono ammessi con conferma.
- **Tipo di piatto** (`set_bed_type`, `GET/POST /v1/bed_type`, `PrintAdvisorPlate.cpp`): impostazione di progetto, non di profilo. Ambito `project` (combo della barra laterale, via `Plater::select_bed_type` + `on_bed_type_change`) oppure `plate` (override del singolo piatto via `PartPlate::set_bed_type`, come la finestra Impostazioni piatto). Solo i tipi offerti dalla barra per quel vendor; conferma con prima → dopo e temperature del filamento per il nuovo tipo; rifiutato durante slicing/esportazione; dopo la finestra si ricontrolla che il tipo non sia cambiato.
- **Preflight per ASA/ABS** (solo avvisi, con `data`): `glue_on_smooth_pei_large_part` (PEI liscio e pezzo > 80 mm: colla stick), `chamber_cold_preheat` (camera < 30 °C da telemetria con stampante inattiva: `preheat_chamber` prima dell'invio e soak più lungo), `chamber_unknown` (nessuna telemetria recente: non si indovina). Per fili con ASA/ABS i testi guida del sidecar propongono per primi asciugatura (80 °C, 4–6 h) e test di retrazione (≤ 10 mm).
- **Profili**:
  - si salva sempre una **copia utente**, mai sopra l'originale e mai un profilo di sistema;
  - la copia eredita dal padre come "Salva come": da un profilo base eredita da quello, altrimenti dal suo `inherits`;
  - il file della copia contiene solo le differenze dal padre;
  - la copia non viene selezionata automaticamente.
- **Chiavi scrivibili** (`advisor_key_writable`):
  - ammesse solo numeriche, percentuali, booleane ed enum di filamento o processo;
  - mai testo libero (G-code, nomi, note, `inherits`), identità del materiale, assegnazione degli estrusori o `print_sequence`, né `material_flow_dependent_temperature`/`material_flow_temp_graph` (aggirano l'intervallo dell'ugello);
  - le stesse regole valgono per `remove` di `set_object_settings`;
  - il profilo stampante è in sola lettura.
- **Limiti** (`check_advisor_safety`):
  - limiti fisici plausibili, più i massimi della stampante (`machine_limits`), presi dalla telemetria LAN o dalla tabella della famiglia K1;
  - la temperatura dell'ugello deve stare nell'intervallo del filamento.
- Vengono rifiutate le proposte su profili con modifiche non salvate: per esempio quelle lasciate da un test di calibrazione.
- **Rinomina ed eliminazione** sono ammesse solo per le copie create dal Print Advisor (suffisso "(Advisor …)" oppure presenti nello storico), e mai se ereditate da altri profili.
  - La rinomina rifiuta anche le copie con modifiche non salvate. Una copia selezionata si può rinominare: la selezione (anche negli estrusori) passa al nuovo nome.
  - L'eliminazione rifiuta le copie selezionate o usate da un estrusore.
- Lo storico persistente si trova in `<data_dir>/print-advisor/history.jsonl`.
- **Esito di fine stampa e archivio** (`simple/printAdvisor/PrintAdvisorVerdict.cpp`):
  - quando una stampa con diario termina `completed` e il piatto scende sotto 40 °C, il monitor chiede "Com'è venuta la stampa di <file>?" (finestra in stile MCP Advisor + notifica). Una sola domanda in sospeso; la finestra aspetta se c'è un'altra finestra modale aperta (riprova ogni minuto);
  - **Bene, archivia**: esito `ok` nel diario (stesso scrittore di `/v1/print_verdict`) e cartella `<data_dir>/print-advisor/archive/<data>_<nome>/` con `project.3mf` (progetto aperto, salvato a parte senza cambiare il percorso del progetto), il G-code del piatto corrente se lo slicing è valido, `print-log.jsonl`, la foto dalla telecamera (solo con la scheda Dispositivo aperta, in un thread separato) e `metadata.json` con ciò che è stato saltato e perché;
  - **Difetti o fallita**: seconda finestra con il motivo (staccata, warping, stringing, sottoestrusione, altro + note) registrato come esito `detach`/`warp`/`stringing`/`under_extrusion`/`other` con nota (gli stessi valori di `record_print_verdict`);
  - se una nuova stampa completata sostituisce la domanda mentre la finestra è aperta, la risposta viene comunque applicata alla stampa chiesta (storico `..._then_replaced`) e subito dopo si chiede la nuova;
  - mentre la finestra è aperta le modifiche (compreso `record_print_verdict`) ricevono HTTP 409; se nel frattempo il client MCP ha già registrato un esito, la finestra non ne scrive un secondo, e `record_print_verdict` rifiuta un log che ha già un esito;
  - la domanda resta in sospeso se la registrazione fallisce; la nota è limitata a 500 byte UTF-8; se la temperatura del piatto non è nota si chiede 30 min dopo la fine;
  - **Più tardi**: resta in sospeso in memoria e in `<data_dir>/print-advisor/pending_verdict.json`, richiesto 20 s dopo il prossimo avvio del server;
  - tool MCP `list_archived_prints` (GET `/v1/archive`) e `reopen_archived_project` (POST `/v1/archive/open`, con conferma; non avvia mai una stampa; carica una copia temporanea in `<data_dir>/print-advisor/tmp/` e rende il progetto senza titolo con `set_project_filename("")`, così "Salva" chiede un nome e la copia archiviata non viene mai sovrascritta). La finestra avvisa che i profili stampante/filamento/processo del progetto archiviato diventano la selezione attiva; il risultato riporta `presets_before`/`presets_after`.

### Opzioni aggiunte al motore

- `heat_soak_time`, `heat_soak_bed_temp` (filamento): `GCode.cpp` le emette **prima** di `machine_start_gcode` (`M106` scarico 0, `M190`, `G4 P`). `heat_soak_bed_temp` = 0 significa la temperatura del piatto al primo layer di ciascun filamento per il tipo di piatto (sulle stampanti multi-ugello segue `bed_temperature_mode`), poi il massimo tra i filamenti con soak; nessun massimo lato stampante, solo l'intervallo 0–120 dell'opzione. Il `filament_start_gcode` viene emesso dopo `START_PRINT` da `set_extruder` (è commentata solo la chiamata doppia in `_do_export`).
- `brim_layers` (processo/oggetto): ripete il brim del primo layer sui layer successivi, riscalando il flusso all'altezza del layer.
- `brim_layers_object_gap` (processo/oggetto, mm, predefinito 0 come `brim_object_gap`): distanza brim-oggetto usata solo sui layer del brim dopo il primo; il layer 1 mantiene `brim_object_gap`. Se è maggiore di `brim_object_gap`, il **bordo** del brim sui layer 2+ resta almeno a quella distanza dalle isole dell'oggetto: le linee vengono tolte o tagliate, mai spostate, quindi la distanza reale cresce a passi di circa una linea. Le isole degli altri oggetti contano solo in questo caso, con la distanza minore tra questo valore e il `brim_object_gap` dell'altro oggetto (0 se non ha brim). Con un valore ≤ `brim_object_gap` il comportamento è quello precedente: copia del layer 1 tagliata solo dove si sovrappone alle isole dell'oggetto stesso.
- Nella scheda filamento le opzioni della camera sono disattivate solo per la famiglia Creality K1 (senza riscaldatore); le K2 le mantengono anche con `support_chamber_temp_control` = 0. `set_layer_schedule` rifiuta `chamber_temp` solo sulla famiglia K1.
- Tutte e quattro sono `comSimple`, visibili nelle schede (`Tab.cpp`, `resources/images/process/ProcessConfig.json`).

### Insidie trovate

- `std::stod` segue il locale della GUI (in italiano la virgola): "0.5" diventava 0. Leggere i numeri con `std::istringstream` + `std::locale::classic()`.
- Anche `sscanf` segue il locale C del processo: chi deserializza opzioni di configurazione fuori da `load_from_json` deve usare `CNumericLocalesSetter` (la cache dei profili `PresetConfigCache` falliva su `99999,99999` a ogni avvio, corretto il 2026-10-24).
- `PresetCollection::delete_preset` non aggiorna l'indice del preset selezionato: usare `delete_preset_keep_selection`. `load_preset` invece lo aggiorna.
- Non iterare `snapshot().items()` su un temporaneo: prima salvare il json in una variabile.
- `wxNotificationMessage` va tenuto vivo dopo `Show()`.
- Le build Alpha di Creality scaricavano un `machineList.json` ridotto (solo K2). Dalla fase F1 il fork non scarica più nulla: `machineList.json` e `materialList.json` si leggono solo da `resources/profiles/Creality/` (la copia in `<data_dir>/system/Creality` viene ignorata), le copertine delle stampanti non vengono scaricate (le pagine ricevono `thumbnail` locali da `localize_printer_list_images` in `PrinterCoverRoute.hpp`, non più `pic-cdn.creality.com`), `update_preset_params` e `send_http_request` dalle pagine web sono no-op, le intestazioni HTTP globali (`Http::set_extra_headers`, MAC address, token) non esistono più, la diagnosi LAN non contatta più baidu.com (patch web 20), e sono spariti sondaggio di soddisfazione, pulsante/menu "User Feedback", finestra "User Experience Program" e i workflow GitHub. Il controllo di compatibilità nella pagina di invio ripiega sul modello stampante.

### Creality K1C (stampante dell'utente)

- **Limiti**: piatto massimo 100 °C (il firmware riduce 105 a 100), ugello 300 °C. Ha il sensore della camera (`boxTemp`, massimo 60) ma nessun riscaldatore della camera.
- **Sensore camera**: secondo la community legge 5–10 °C in più perché sta nel flusso della ventola pezzo.
- **Ventola posteriore di estrazione (`fanCase`, `M106 P1`)**: senza `activate_air_filtration` nel filamento, `START_PRINT` la lascia al 100% e la camera resta a circa 37 °C. Per l'ASA: `activate_air_filtration=1` e `during_print_exhaust_fan_speed=0`.
- **Protocollo LAN**:
  - indirizzo `wss://<ip>:443` (oppure `ws://<ip>:9999`); per leggere lo stato si invia `{"method":"get","params":{"reqPrinterPara":1}}`;
  - il certificato della stampante è scaduto e intestato a un altro IP, ma firmato da `resources/cert/ca.crt`: accettarlo solo se firmato da quella CA.
- **Ventole** (verificato il 2026-10-19 guardando le ventole fisiche; la supposizione del 18 era sbagliata):

  | Ventola fisica | G-code | Campo LAN `set` | Telemetria |
  |---|---|---|---|
  | ugello/pezzo (testina) | `M106 P0` (anche P3, solo da telemetria) | `fan` | `modelFanPct` |
  | estrazione posteriore | `M106 P1` | `fanCase` | `caseFanPct` |
  | laterale grande | `M106 P2` | `fanAuxiliary` | `auxiliaryFanPct` |

  - Il soak (`M106 P1 S0` + `M106 P2 S0`) le spegne già entrambe.
  - Pagina Dispositivo: "Case fan" = "Ventola estrazione", "Side fan" = "Ventola laterale" (etichette corrette il 2026-10-19, vedi `doc/MCP/web-bundle-patches.md`).
  - Comandi diretti: su `ws://<ip>:9999` inviare `{"method":"set","params":{"gcodeCmd":"M106 P2 S255"}}` oppure `{"fanCase":1}`. La lettura subito dopo può essere vecchia: aspettare circa 2 s.
  - La K1C accende la laterale al 100% durante la sua calibrazione pre-stampa: per l'ASA va spenta.
- **Estrazione automatica** (verificato il 2026-10-19 sulla seconda stampa ASA):
  - il firmware della K1C accende la ventola di estrazione quando la camera arriva a circa 35–37 °C;
  - finché la camera resta sopra soglia, **ignora ogni spegnimento esterno**: `fanCase:0`, sia dal Print Advisor sia diretto, e `M106 P1 S0`. Si spegne da sola quando la camera scende;
  - quindi `set_exhaust_guard` sulla K1C **non** ottiene l'effetto voluto: registra soltanto le accensioni;
  - rimedi reali: nastro di alluminio sulla griglia posteriore (da togliere per il PLA), oppure cambiare la soglia nel firmware (serve l'accesso root).
- **ASA su pezzi grandi**: la colla stick sul PEI liscio è decisiva. Il 2026-10-19 la borchia da 130 mm si è staccata al 57% senza colla ed è venuta bene con la colla, pur con la camera a 33–37 °C.
- **Avvio stampa**: il firmware fa la sua preparazione (calibrazione) prima del G-code; `START_PRINT` **non** abbassa il piatto (verificato il 2026-10-19).
- **Stati**: 0 inattiva, 1 in stampa, 2 completata, 3 fallita, 4 interrotta, 5 in pausa, 6 in pausa in corso, 7 in arresto, 8 ripristino.
- **Nome del sensore camera** nel Klipper Creality: non documentato. Per questo il "soak a temperatura" è fatto lato MCP (`preheat_chamber`), non con `TEMPERATURE_WAIT` nel G-code.

### Stato e prossimi passi

- **Fatto e verificato** (sola lettura o con la stampante): copie dei profili con padre, nomi e rinomina, limiti, preflight, confronto profili, calcolo calibrazioni, schede materiali, slicing con misura, heat soak e brim multi-layer nel G-code, telemetria LAN.
- **Verificato con la stampante** (2026-10-19): `device_control` per ventole e luce, foto dalla telecamera, analisi degli sbalzi.
- **Implementato ma da provare con la stampante**:
  - `control_print`, `start_calibration` (con i nuovi limiti);
  - `device_control` per temperature e velocità;
  - preriscaldo: spegnimento automatico del piatto, plateau, `max_minutes`;
  - guardia dell'estrazione, raffreddamento lento, timer nella card della camera;
  - volumi di supporto, `set_object_settings`, `arrange_plate`;
  - `delete_profile_copy`, `export_profile`, annullamento delle copie;
  - notifiche, `set_layer_schedule`, preselezione della stampante nella finestra di invio.
- **Fatto il 2026-10-18 sera**:
  - dialogo di conferma "MCP Advisor" (`simple/printAdvisor/AdvisorConfirmDialog.*`): logo, titolo "MCP Advisor - …", riga "Richiesta da MCP Advisor (Claude)", tabella prima → dopo con nomi tradotti, Annulla predefinito, colori per tema chiaro e scuro;
  - `preheat_chamber` si ferma quando la stampa parte;
  - l'evento di fine stampa riporta il layer reale;
  - il descrittore contiene il `pid`, e il server si ferma già in `MainFrame::shutdown`;
  - il sidecar segnala "Creality Print non è aperto" invece di "fetch failed";
  - il soak spegne anche la ventola ausiliaria (`M106 P2 S0`).
- **Profili ASA**:
  - di riferimento: filamento **"ASA K1C"** e processo **"0.20mm ASA K1C"** ("ASA K1C - prova 1" eliminato il 2026-10-19);
  - per pezzi grandi: **"ASA K1C - pezzi grandi"** (soak 10 min, ventola sbalzi 40%), creato il 2026-10-19 per la borchia da 130 mm.
- **Giro di modifiche del 2026-10-19** (20 punti approvati dall'utente): fatto nel commit `9541a937`, tranne `set_ai_detection` (non implementato). Il controllo del piatto durante `START_PRINT` ha dato esito negativo: il piatto non scende.
- **Revisione del 2026-10-19**: elenco delle correzioni nella memoria di Claude Code (`review-2026-10-19.md`).
- **Proposte non ancora decise**: comandi fissi in stampa per flusso (`M221`), Z-offset e pressure advance; modificatori per fascia di altezza; pittura della cucitura e dei supporti.
- **Preriscaldamento della K1C**:
  - 2026-10-18: con il solo piatto la camera si ferma a 35–37 °C dopo 5–6 minuti, e supera i 40 °C solo durante la stampa;
  - 2026-10-19, stanza fredda (camera a 16 °C all'inizio): piatto a 99 °C al minuto 6, camera a 30 °C al minuto 23 e a 32 °C circa al minuto 35, poi ferma.
- **Fatti**: foto dalla telecamera (`camera_snapshot`) e diario delle stampe (`list_print_logs`, `get_print_log`, `record_print_verdict`, `suggest_recipe`).
- **Rimandati dall'utente**: inventario bobine e asciugatura, costo della stampa, manutenzione e timelapse.
- **Pulizia del 2026-10-23** (storia precedente nel backup locale `..\CrealityPrint-git-old`, tag `pre-cleanup-20261023`; circa 171.000 righe in meno; dettagli in `doc/cleanup-plan-2026-10-20.md`, sezione "Pulizia del 2026-10-23"). G-code identico su 4 fixture, smoke MCP 13/13 sulla build e sul pacchetto installato. Da provare con la stampante: foto della K1C da `localize_printer_list_images`, invio, Diagnosi di una stampante offline (patch 20), campo obiettivo camera.
- **Blocco 6 del 2026-10-23** (snellimento): profili solo **Creality** e **Custom** (`PresetBundle::BBL_BUNDLE` è `"Custom"`; un vendor in `<data_dir>/system` senza `<vendor>.json` in `resources/profiles` viene ignorato); il pacchetto non installa più WebView2, vcredist, Edge setup, STLThumb né `curl.exe` (se manca WebView2 l'app mostra solo un messaggio con il link di Microsoft); la procedura del primo avvio parte direttamente da "collega stampante" (patch web 21: via "modalità software" e "area login"); tolti `BaseException`/`StackWalker` (mai installati) e la voce "Zoom of view" delle Preferenze. Restano per scelta dell'utente: tutte le lingue, il video RTSP (FFmpeg), l'import STEP.
- **Pulizia del 2026-10-24** (tag di partenza `pre-audit-20261024`; verifica con 6 agenti e decisioni in `doc/cleanup-audit-2026-10-24.md`; nomi tolti in `tools/verify/removed-F9.txt`, fase `F9` di `verify-phase.mjs`): server locale 13666 corretto (niente file fuori da `resources/web`, niente `Access-Control-Allow-Origin: *`, `Host` solo `localhost`/`127.0.0.1` sulla porta del server, `/proxy` solo con il token di sessione `pa_t` iniettato nelle pagine e solo verso IPv4 private e `/protocal.csp`, video solo verso IPv4 private, niente nomi di periferica DOS); password di notarizzazione macOS tolta da `package/macx.cmake` (variabile `MENTORSLICER_NOTARY_PASSWORD`); id casuale per sessione al posto dell'impronta del PC (`get_system_id`); cache dei profili corretta (`CNumericLocalesSetter`); esito di pausa/ripresa/stop/preriscaldo letto dalla pagina; profili WebView2 in `%TEMP%\MentorSlicer_webview` con pulizia dei profili abbandonati; tolti il visualizzatore di sviluppo `Diagnostics/Plot` (IP `192.168.10.47` nella DLL), SimplyPrint/OAuth, `Serial`, la cache di slicing "cloud" della CLI, la catena di aggiornamento dei profili, `GUI_SIMPLE` (il Print Advisor si compila sempre), 158 file (risorse comprese) e circa 18.000 righe nette di codice morto; log: messaggi di avanzamento e diagnostica portati a debug; patch web 25 (SDK Alibaba OSS), 26 (riquadro login della pagina Dispositivo), 14e (tracciamento).
- **Provare una build senza disturbare la copia stabile**: avviare `build\src\Release\MentorSlicer.exe --datadir <cartella>`; i dati stanno in `<cartella>\MentorSlicer\7.3 Alpha` (copiarci `%APPDATA%\MentorSlicer` senza `deviceInfo.json`, `user\default\local_device`, `print-advisor`, `cache\print-advisor`, `log`, così la copia non si collega alla stampante) e lo smoke test va lanciato con `MENTORSLICER_ADVISOR_DESCRIPTOR=<cartella>\MentorSlicer\7.3 Alpha\cache\print-advisor\endpoint.json`.
- **Fatto il 2026-10-20** (commit `32f5ea42`): `brim_layers_object_gap`; versione 7.3.0.0 e avviso di aggiornamento del fork (`PrintAdvisorUpdateNotice.*`, poi rimosso in F1 il 2026-10-21 insieme a tutto il controllo aggiornamenti); finestra di esito a fine stampa con archivio e tool `list_archived_prints`/`reopen_archived_project`; preflight ASA/ABS (colla su PEI liscio, camera fredda) e tool `get_bed_type`/`set_bed_type`; avviso di camera fredda nella finestra di invio; script `publish-stable` con pulsante VS Code; consigli su asciugatura e retrazione per i fili. `set_bed_type` è stato verificato nell'app il 2026-10-20 (in entrambe le direzioni, con conferma). Da provare: finestra di esito, archivio, riapertura.
- **Prova ASA** (conclusa il 2026-10-20: la borchia è venuta bene con la colla stick su PEI liscio): la prima stampa si è staccata al 48% (PEI liscio senza colla, ventola case al 100%, camera a 37 °C).
  - La seconda prova usa i profili `ASA K1C - prova 2` e `0.20mm ASA K1C - prova 2`: soak 10 min a 100 °C, ventola case spenta, 5 layer senza ventola pezzo con massimo al 20%, primo layer 0,6 mm a 30 mm/s, brim 10 mm su 3 layer.

### Lavorare con l'utente

- Risposte in italiano, brevi; una domanda alla volta.
- Per scegliere tra più funzioni: raccogliere tutte le risposte, poi implementare in un unico blocco con **una sola build**.
- Dopo ogni build, verificare con un client MCP di prova prima di dichiarare una cosa funzionante.
