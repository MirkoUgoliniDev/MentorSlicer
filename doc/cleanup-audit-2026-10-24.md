# Verifica approfondita del 2026-10-24: problemi e codice superfluo

Scansione in sola lettura con 6 agenti (interfaccia, Dispositivo e pagine web, log/rete/privacy, motore e Utils, risorse e build, codice del Print Advisor) su `main` al commit `b6b4721`. **Nessun file di codice è stato modificato.** I punti segnati "verificato" li ho ricontrollati io sul codice; gli altri si basano sulle ricerche (`git grep`) degli agenti, riportate come prova.

Le sigle (S1, B1, G1, W1, E1, R1, P1, L1, N1, D1) servono per scegliere: basta indicare quali fare.

## In breve

- **Due problemi di sicurezza veri** nel server locale delle pagine (porta 13666): lettura di file del PC e "ponte" verso la rete di casa. Vanno sistemati per primi.
- **Nessuna connessione automatica** verso Creality, Bambu o Google: le pulizie precedenti tengono.
- **Le stampe sono a posto**: nessun problema trovato tocca il G-code. Il difetto della cache dei profili (B1) rallenta solo l'avvio.
- **Circa 6.000 righe di codice morto** sicure da togliere (più ~1.600 di blocchi commentati, opzionali), **~8 MB di risorse** inutili, e **1,9 GB** di profili WebView2 accumulati nella cartella temporanea.
- **Log**: un quarto delle righe è segnato "errore" ma non lo è (messaggi di debug di Creality).

---

## 1. Sicurezza (da fare per prime)

| Sigla | Dove | Problema | Stato | Correzione proposta |
|---|---|---|---|---|
| **S1** | `src/slic3r/GUI/HttpServer.cpp:154, 843-883` | **Lettura di qualsiasi file del PC.** L'indirizzo richiesto viene decodificato (`%2F` → `/`) e aggiunto a `resources/web` senza rifiutare `..`; il file viene restituito con `Access-Control-Allow-Origin: *`. Con MentorSlicer aperto, una pagina web qualsiasi nel browser potrebbe leggere per esempio `%APPDATA%\MentorSlicer\cache\print-advisor\endpoint.json` (con il token del Print Advisor). I browser recenti limitano in parte le chiamate da siti pubblici a `127.0.0.1`, ma non tutti e non sempre. | verificato sul codice; non provato dal vivo (app chiusa) | Normalizzare il percorso e rifiutarlo se esce da `resources/web` (o dalle radici ammesse); togliere `Access-Control-Allow-Origin: *` dai file statici. Prova dal vivo con `curl --path-as-is` su `win.ini` prima e dopo. |
| **S2** | `HttpServer.cpp:161-163, 779-830` | **Route `/proxy` aperta.** Apre una connessione verso **qualsiasi** IPv4 indicato in `host=` sulla porta 81 e restituisce la risposta, con `Access-Control-Allow-Origin: *`; nessun controllo su `\r\n` nel `path`. Serve solo alle vecchie stampanti Creality WiFi (`protocal.csp`), non alla K1C. | verificato dall'agente | Limitarla agli indirizzi privati della LAN come già fa `/device_image`, rifiutare CR/LF, togliere `*`. Oppure toglierla del tutto se usi solo stampanti K. |
| **S3** | `package/macx.cmake:25` | **Password scritta nel codice**, in formato delle password per app di Apple, usata per la firma macOS. È di Creality ed era già nel loro sorgente pubblico. | verificato | Toglierla (parametro da variabile d'ambiente). Resta nella storia Git: sparisce con la riscrittura della storia già prevista. |
| **S4** | `src/slic3r/Jobs/OAuthJob.cpp:21` | Se la risposta del login OAuth (Obico/SimplyPrint) non è JSON valido, la scrive **intera** nel log, token compreso. | dall'agente | Scrivere solo lunghezza e codice HTTP. |
| **S5** (da decidere) | `print_manage/Routes/SysRoutes.cpp:23-29` | `get_system_id` dà alle pagine un'impronta del PC (SHA del MachineGuid di Windows). Serviva alle intestazioni cloud; oggi è usata anche come id del flusso video WebRTC. | dall'agente, **incerto** | Sostituirla con un id casuale per sessione e provare la telecamera. |

## 2. Difetti di funzionamento

| Sigla | Dove | Problema | Effetto | Correzione |
|---|---|---|---|---|
| **B1** | `src/libslic3r/PresetConfigCache.cpp:317-319` | La lettura dalla cache dei profili non imposta il separatore decimale "C" (lo fa invece il caricamento normale, `Config.cpp:867`). Con Windows in italiano `sscanf` legge `99999,99999` come un solo numero e fallisce. | **verificato.** 94 profili stampante Creality vengono riletti dal JSON a ogni avvio: valori corretti, **G-code invariato**, ma la cache da 10 MB viene riscritta sempre, l'avvio è più lento e il log ha 94 avvisi per avvio (30% di una sessione corta). | Una riga: `CNumericLocalesSetter` in `PresetConfigCache::load`. Tocca `libslic3r`, ma non cambia l'output. |
| **B2** | `PrinterMgrView.cpp:2486`, `PrintAdvisorInternal.cpp:335` | Pausa/ripresa/stop e avvio del preriscaldo passano dalla pagina Dispositivo "senza attendere risposta": la pagina rimanda `print_control_result` con `success:false` in caso di errore, ma nessun codice C++ lo ascolta. | **verificato.** `control_print` e `preheat_chamber` possono dire "fatto" anche se la pagina ha fallito. | Gestire `print_control_result` e riportarlo al client MCP (o almeno nel log e nello storico). |
| **B3** | `simple/bridge/SlicerBridgeActionsProcess.cpp:356-419` | `/v1/slice` sceglie il piatto dalla selezione 3D e può **cambiare il piatto visibile** senza conferma; la descrizione del tool dice "il piatto corrente". | dall'agente | Usare sempre il piatto corrente. Insieme a P1. |
| **B4** | `simple/bridge/SlicerBridgeActionsConfig.cpp:93-139` | `/v1/context` prende i valori prima dalle **impostazioni dell'oggetto selezionato** e poi dal profilo: il risultato cambia secondo cosa è selezionato nella vista 3D; inoltre restituisce tutte le opzioni, non il "sottoinsieme" promesso (più token). | dall'agente | Leggere solo il profilo (o etichettare a parte le impostazioni dell'oggetto) e limitare le chiavi. |
| **B5** | `Widgets/WebView.cpp:382-435, 895-913` | Profili WebView2 in `%TEMP%\slicer_webview_userdata`: se il profilo è "occupato" (chiusura forzata, seconda istanza) ne crea uno nuovo e **non cancella mai i vecchi**. | **1,9 GB** oggi: 41 profili (31 dal 18 al 23 ottobre), più 75 cartelle `slicer_webview_userdata_<pid>` di agosto. Il nome è lo stesso usato da Creality Print. | All'avvio cancellare i profili non attivi; nome della cartella proprio (`MentorSlicer_webview`). Le cartelle attuali si possono cancellare a mano con l'app chiusa. |
| **B6** | `SendToPrinter.cpp:446-452` | Nella finestra di invio i link (wiki CFS, negozio) vengono **aperti due volte** nel browser. | dall'agente | Togliere una delle due aperture. |
| **B7** | `GUI_Factories.cpp:2655`, `GUI_ObjectList.cpp:5701` | L'evento `EVT_GLCANVAS_PLATE_NAME_CHANGE` viene inviato ma nessuno lo ascolta: rinominare un piatto potrebbe non aggiornare ciò che doveva. | dall'agente | Verificare cosa doveva aggiornare; collegarlo o toglierlo. |
| **B8** | `resources/web/deviceMgr` (bundle) | La pagina Dispositivo mostra sempre "**Accedi** per collegare il dispositivo…": il token è sempre vuoto. Il clic porta alla Home e non succede nulla. | dall'agente | Patch web 26: nascondere il riquadro. |
| **B9** | `PrintAdvisorServer.cpp:1540, 1795, 2664, 2725` | Il Print Advisor considera ancora "sincronizzata sul cloud" una copia con `setting_id` e rifiuta rinomina, eliminazione e annullamento, con un messaggio sul cloud che non esiste più. I profili importati in F8 possono avere ancora un `setting_id`. CLAUDE.md dice invece che è "sempre false". | dall'agente, **incerto** se capita sui tuoi profili | Togliere il controllo del cloud e aggiornare testi del sidecar e README. |
| **B10** | `BuildLinux.sh:207,209`, `BuildLinux_clang.sh:184,186`, `build_release_macos.sh:155,273`, `Dockerfile:95`, `CMakeLists.txt:989` | Le build Linux/macOS/Docker usano ancora il target e i nomi `CrealityPrint` (dopo F8 è `MentorSlicer`): fallirebbero. | dall'agente | Aggiornare i nomi (non si compilano, ma restano coerenti). |
| **B11** | `resources/images/process/light/` | 23 immagini dei suggerimenti di `ProcessConfig.json` esistono solo per il tema scuro: col tema chiaro il suggerimento con immagine resta vuoto. | dall'agente | Creare le varianti chiare o usare la scura anche nel tema chiaro. |
| **B12** | `AppConfig.cpp:507-508` | Refuso `show_build_edgets` (chiave che nessuno legge). | dall'agente | Sparisce con E13. |
| **B13** (latente) | `DataDirectoryMigration.cpp` | L'importazione della cartella di Creality non salta `ota/`: eventuali profili lì verrebbero applicati come aggiornamenti forzati. Oggi la cartella è vuota. | dall'agente | Sparisce con E3. |

## 3. Log

Impostazione del log corretta (livello `warning`, file da 100 MB, al massimo 30). Nei log reali: circa 280 righe all'ora, ma **il 25% è segnato `[error]` senza essere un errore**. Nessun token, intestazione HTTP o messaggio WebSocket nei log.

| Sigla | Dove | Messaggio | Quante righe | Azione |
|---|---|---|---|---|
| L1 | `Tab.cpp:4605, 4609` | `[TabPrintModel::set_model_config]` con puntatori, a warning | 20% della sessione lunga | debug |
| L2 | `GUI_Preview.cpp:331, 358, 655, 833` | `load_print_as_fff start/end` a **error** | 12% | debug |
| L3 | `PrintApply.cpp`, `Plater.cpp` (17 punti) | `[SLICE_PREFLIGHT_TIMING]` a warning | 411 righe | debug |
| L4 | `3DScene.cpp:246` e altri 14 | `[LOD_DIAG]` ogni 2 s durante il disegno | 386 righe | debug |
| L5 | `PresetConfigCache.cpp:327` | 94 avvisi per avvio | 30% di una sessione corta | si risolve con B1 |
| L6 | `GCode.cpp:3261-3627`, `Print.cpp`, `PrintObject.cpp`, `BackgroundSlicingProcess.cpp` | "Exporting G-code…", `[PERF_TIMING]`, memoria, a **error** | ~120 righe + memoria 146 | info/debug |
| L7 | `PrintApply.cpp:2036`, `PrinterMgrView.cpp:1537,1573` | percorso completo del modello e IP/MAC della stampante a **error** | 98 righe | debug (finirebbero in un log condiviso) |
| L8 | `Plater.cpp` (16), `bbs_3mf.cpp`, `TransmittanceDialog.cpp`, `WipeTowerDialog.cpp` | `MF_DEBUG`, "BEFORE/AFTER SetBitmap" | ~100 righe | togliere |
| L9 | `HttpServer.cpp:845` e `std::cout` 148/172/178 | ogni file servito alle pagine a warning | – | debug / togliere |
| L10 | `SendToPrinter.cpp:359-361` `cxy_print_log` | JSON completo di ogni fase dell'invio a warning (tracciamento Creality) | – | togliere |
| L11 | `SlicerBridge.cpp:27, 42` | 2 righe info con tutti i parametri per ogni chiamata (4 per ogni `get_print_context`) | – | una sola, a debug |
| L12 | `PrinterMgrView.cpp:406-440`, `SendToPrinter.cpp:1487` | "SendAPIKey start" a ogni avvio da una funzione che non fa nulla | – | sparisce con W9 |
| L13 | `OpenGLManager.cpp:431` | "Unable to init GLAD OpenGL loader" a error 3 volte per avvio, sembra innocuo | – | verificare, poi warning/debug |

## 4. Codice morto da togliere (rischio basso, nessun effetto sul G-code)

### Interfaccia (`src/slic3r/GUI`)

| Sigla | Cosa | Righe |
|---|---|---|
| G1 | `FileUploader.*` (`HttpFileUploader`, caricatore HTTP mai usato) + membro in `Klipper4408Interface.hpp:22` | 526 |
| G2 | Preferenze: 8 costruttori di pagine/voci mai chiamati, compresa la pagina "Sincronizzazione utente/preset" del cloud Creality | ~320 |
| G3 | `Plater`/`Sidebar`/`FilamentPanel`: 15 funzioni senza chiamanti (export headless "Creality", `export_cxprj`, sincronizzazione box filamenti, `cancel_background_slicing` "da AI bridge"…) | ~270 |
| G4 | Stub del login: `is_login()`, `get_user()`, `UserInfo`, `m_user`, `m_create_preset_blocked` (oggi nessuno li chiama: verificato) | ~25 |
| G5 | `select_language()`, `discard_all_current_preset_changes()`, `get_local_device_dir()` (cartelle per account) | ~115 |
| G6 | `download_file()` (scaricatore HTTP con socket a mano) e `generate_image()` | ~150 |
| G7 | `get_vertion_type()` (Alpha/Beta per il cloud), `get_file_md5()` | ~55 |
| G8, G15, G16 | Blocchi interamente commentati: vecchio menu di configurazione, gestore delle eccezioni di debug, menu del visualizzatore G-code | ~310 |
| G9 | Resti di pubblicazione/account/negozio modelli nella barra in alto e nella finestra principale | ~45 |
| G10 | Notifica "Nuova versione disponibile" (apriva thingiverse) e 8 tipi di notifica mai usati | ~20 |
| G11 | Evento `EVT_ON_MAPPING_DEVICE_FILAMENT` mai inviato e i due gestori | ~15 |
| G12 | Macro `CUSTOM_CXCLOUD`/`CUSTOMIZED` (sempre vere/mai definite): togliere gli `#if` | ~25 |
| G13 | Blocchi `_DEBUG1` con l'indirizzo LAN dello sviluppatore Creality | ~20 |
| G14 | Variabile `imported_from_add_file` scritta 10 volte e mai letta ("la usava la chat IA") | ~12 |
| G18–G20 | Classe vuota `UserNotification`, `bind_phy_printer_by_ip_or_name`, evento `EVT_WEBVIEW_RECREATED` | ~26 |
| G22 | Etichetta "Regione di login" nelle Preferenze: il valore serve ancora alle pagine, va solo rinominata "Regione" | 1 |

### Dispositivo, invio e pagine web

| Sigla | Cosa | Dimensione |
|---|---|---|
| W1 | SDK **Alibaba OSS** (upload cloud) ancora dentro il bundle della Home, mai usato | ~730 KB (patch web 25) |
| W2 | Immagini "Edizione AI / Professionale" della guida iniziale, irraggiungibili dopo la patch 21 | 2,5 MB |
| W3, W4 | Vecchie pagine della procedura guidata (stile Bambu) e il C++ collegato (`save_region`, `ShowUserGuide`) | ~235 KB + 60 righe |
| W5 | Catena "corrispondenza colori" della finestra di invio, mai attivata | ~60 righe |
| W6, W7 | Gestori vuoti tenuti solo per "inghiottire" comandi di account, cloud, MQTT, analisi | ~20 righe |
| W8–W12 | Ramo irraggiungibile `common_openurl`, iniezione `X-API-Key` mai attiva, blocchi `_DEBUG1` (server di sviluppo), funzioni e membri senza uso | ~150 righe |
| W14 | Route `/creality_presets` (serve file dalla cartella temporanea), nessuno la usa | 3 righe |
| W16 | Gestori della Home che non fanno nulla (`send_http_request`, `update_preset_params`) | ~40 righe |
| W20–W23 | Immagini delle pagine non usate | ~0,9 MB |
| N4, N5 | Codice di tracciamento morto nella Home (stub di sicurezza, patch 14e); link `target=_blank` aperti **dentro** la Home invece che nel browser | piccoli |

Nota: nei bundle restano comandi e indirizzi cloud (MakeNow, account, MQTT, "workshop" dell'agente IA di Creality), ma sono tutti dietro il login o disattivati dalle patch: si lasciano (W18, W19, W24).

### Motore e utilità (`src/libslic3r`, `src/slic3r/Utils`)

| Sigla | Cosa | Righe |
|---|---|---|
| **E1** | **Visualizzatore di sviluppo Creality** (`Diagnostics/Plot.cpp`, `GeometrySender.h`) compilato nella DLL di Release con 13 funzioni esportate e l'indirizzo `192.168.10.47:45454` scritto nel codice (verificato). Nessuno lo chiama. | ~1.370 |
| E3 | `PresetUpdater`: resti degli aggiornamenti online (cartella `ota`, `prune_tmps`, `Index::load_db`, URL GitHub di Orca) | ~150 |
| E4, E5, E6 | Ramo `Is_Cloud_Slicer` (intestazione G-code invariata), stub `export_layer_time_analytics`, ciclo di debug inutile in `ToolOrdering` | ~40 |
| E8, E9, E10 | Sincronizzazione dei preset col cloud Bambu (`need_sync`, `set_sync_func`, `remove_user_presets_directory`, `get_iot_type_string`) | ~80 |
| E11, E12 | `GlobalConfig` (scritto, mai letto), `compare_creality_versions` | ~60 |
| E13, E14 | Chiavi di `MentorSlicer.conf` e opzioni della riga di comando mai lette | ~20 |
| E16 | Metodi `Http` mai usati (aggiunte cloud Bambu) | ~90 |
| E17 | `src/libslic3r/buildinfo.h` generato e tracciato per errore (data di giugno, `F:/C3DSlicer`): copre quello vero | config build |
| E18 | Log `info` ripetuti (ZAA, MixedSublayer) | ~10 |

### Codice nostro (Print Advisor e sidecar)

| Sigla | Cosa | Righe |
|---|---|---|
| P1 | `start_slice`: ~300 righe mai eseguite, comprese quelle che cambierebbero profilo **senza conferma** se un giorno ricevessero parametri | -300 |
| P3, P5, P6 | Parametri mai usati, campo `overhang_ratio`, commenti sulla chat IA di Creality | ~20 |
| P8–P10 | README e test con il vecchio nome `CrealityPrint` e testi sul cloud | ~10 |
| P11 | Opzione `GUI_SIMPLE`: non è più un vero interruttore (sempre ON) e ha un testo vecchio | ~35 |
| P13 | Sidecar: 12 tool non usano gli helper comuni, così 5 non segnalano l'errore quando l'app risponde `success:false` | -40 |
| P14 | Smoke test: aggiungere i tool di sola lettura non provati (`get_parameter_schema`, `get_bed_type`, `get_slice_result`, `list_archived_prints`, `compute_calibration_result`, `diagnose_defect`) | +15 |
| P15, P16 | Timeout fisso della foto invece della costante; messaggio sbagliato sulle proposte in sospeso | 2 |

### Risorse, build e script

| Sigla | Cosa | Dimensione |
|---|---|---|
| R1, R2 | 26 modelli STL e 27 texture del piatto Creality che il codice non sceglie mai (usa solo F022, K2, K2 Pro, K2 Plus, K1) | 4,4 MB |
| R3, R13–R17 | Immagini e icone non usate (varianti chiare, icone MentorSlicer extra, `param_cooling`, …) | ~0,5 MB |
| R5, R6 | Dipendenza Breakpad e opzioni CMake inesistenti ancora passate dagli script Linux/macOS | 188 KB |
| R7–R11 | Installer WebView2 di Creality con percorsi `C:\work\C3DSlicer`, script Mac non usati, `FindGTK3`, `blacklist.json` Bambu, script Python dei profili | ~15 KB |
| R12 | 2 profili filamento non elencati (mai caricati), uno con nome sbagliato | 5 KB |
| R20, R21 | Voci vecchie in `.gitignore`; catalogo `CrealityPrint.pot` da rigenerare dopo la pulizia | – |

## 5. Da decidere tu

1. **S2 `/proxy`**: restringerla alla LAN o toglierla del tutto? (Serve solo a stampanti Creality WiFi più vecchie della serie K.)
2. **S5 impronta del PC**: sostituirla con un id casuale (da provare con la telecamera)?
3. **E2**: codice di debug di Creality dentro i supporti ad albero (scrive in `C:/temp`, mai attivo, ~330 righe). Togliendolo i supporti restano uguali, si verifica col G-code. Procedo?
4. **E15** `Serial.cpp` (~600 righe, mai usato) ed **E19** cache di slicing per la riga di comando (~1.100 righe, funzione "cloud slicer" Bambu): codice di Orca, la regola era di toccarlo solo se nostro o cloud. E19 è cloud, E15 no.
5. **E20 SimplyPrint**: host di stampa cloud con OAuth. Avevi deciso di tenere gli host esterni: confermi?
6. **R4**: 3 cataloghi di traduzione mai compilati (`Community`, `DeviceList`, `SendPage`, 2,7 MB in tutte le lingue). Non sono lingue, ma "domini" inutilizzati. Toglierli?
7. **R19** `build_release_vs2022_progress.bat`: lo usi ancora?
8. **G17**: ~220 blocchi di codice commentato o `#if 0` di Orca/Bambu nell'interfaccia (~1.600 righe verificate, fino a ~6.700 stimate). Togliere, o lasciare il codice di Orca com'è?
9. **P12**: codice Metal (macOS) per l'orientamento automatico, ~2.000 righe. Avevi deciso di tenere i file per Linux/macOS: confermi?
10. **P11 `GUI_SIMPLE`**: togliere l'opzione o solo rinominarla?
11. **R18** `standard_color_recipes.json` (262 KB): forse letto dalla libreria chiusa dei colori misti. Proposta: tenerlo.

### Decisioni dell'utente (2026-10-24)

1. S2 `/proxy`: **restringere alla LAN**.
2. S5: **id casuale per sessione**, poi prova della telecamera.
3. E2: **sì**, togliere il debug dai supporti (G-code identico).
4. E15 e E19: **entrambi via**.
5. E20 SimplyPrint: **via**.
6. R4 cataloghi `Community`/`DeviceList`/`SendPage`: **restano**.
7. R19 `build_release_vs2022_progress.bat`: **via** (con `scripts/build_with_progress.ps1`).
8. G17: **solo i blocchi verificati**.
9. P12 Metal: **resta**.
10. P11 `GUI_SIMPLE`: **via** (il Print Advisor si compila sempre).
11. R18 `standard_color_recipes.json`: **resta**.
12. Tutto il resto delle sezioni 1–4 in un blocco con una build; **pulizia fuori dal repository sì** (tranne `deps/DL_CACHE`).

## 6. Fuori dal repository (solo da sapere, nulla cancellato)

| Cosa | Dimensione | Nota |
|---|---|---|
| `%TEMP%\slicer_webview_userdata*` | 1,9 GB | vedi B5; si cancella a mano con l'app chiusa |
| `%LOCALAPPDATA%\Creality\EBWebView` | 204 MB | della Creality Print ufficiale, disinstallata |
| `%LOCALAPPDATA%\crealityprint_squirrel` | 19 MB | vecchia cache degli aggiornamenti Creality |
| `tests/regression/out/` | 520 MB | G-code delle prove, rigenerabili |
| `deps/DL_CACHE/` | 627 MB | archivi delle dipendenze: servono solo per ricompilarle |
| `deps/build/dep_BREAKPAD-prefix` e progetti vuoti `dep_MQTT*`, `dep_OSS`, `dep_OpenCSG` | ~50 MB | resti di dipendenze tolte |
| `tools/web-patches/.backup` | 16 MB | copie di sicurezza delle patch web |
| `<data_dir>/printers/`, `ota/`, `cloud_download_data/`, `privacyInfo.json`, `user_info.json` | piccoli | importati o creati senza che nessuno li legga (D2–D5): togliere dall'importazione e dall'avvio |

## 7. Cosa risulta pulito

- **Rete**: le chiamate C++ rimaste vanno solo alla stampante in LAN e agli host configurati da te; niente intestazioni globali, MAC o id del PC nelle richieste HTTP; i link a wiki e siti si aprono solo al clic; i server locali ascoltano solo su `127.0.0.1`.
- **Pagine web**: nessuno script remoto; Firebase, Google Analytics, sonda baidu e `checkInternet` neutralizzati; nessuna stringa dell'IA di Creality nelle pagine Dispositivo e Invio.
- **IA di Creality**: nessun resto di `cxagent`, `SAgent`, chat MCP, MakeNow, ModelMall nel codice C++ (solo commenti, P6).
- **Print Advisor**: tutte le route hanno il loro tool e viceversa (50 tool); il token non finisce mai nei log; i ricontrolli dopo le finestre di conferma ci sono; timeout e limiti coincidono con CLAUDE.md; 26/26 patch web si applicano; TypeScript senza variabili inutilizzate.
- **Motore**: nessun resto cloud nel motore oltre ai tag dei formati 3MF/G-code (restano); le 23 opzioni non referenziate di `PrintConfig` finiscono nell'intestazione del G-code e **non si toccano**.
- **Risorse**: shader, modelli di calibrazione, font e `list.txt` delle traduzioni tutti in uso.

## 8. Come procederei

Un unico giro con **una sola build** (come sempre), in quest'ordine, un commit per blocco:

1. **Sicurezza**: S1, S2, S3, S4 (+ S5 se lo approvi), con prova dal vivo di S1/S2.
2. **Difetti**: B1–B9, B11 (B10 solo testo negli script).
3. **Log**: L1–L13.
4. **Codice morto**: G, W, E, P, R approvati, più le decisioni della sezione 5.
5. **Pulizia fuori dal repository** (B5 e la tabella della sezione 6), solo con il tuo via.

Verifiche: build, G-code identico sulle 4 fixture e `check-gcode.mjs`, `apply-web-patches --check`, `npm run typecheck` + smoke MCP (ampliato con P14), avvio manuale (Prepara, Anteprima, Dispositivo con K1C, invio, Preferenze, chiusura), 5 minuti di `net-watch`, controllo del log (meno righe `[error]`), poi la revisione a più passaggi.
