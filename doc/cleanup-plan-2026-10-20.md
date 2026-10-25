# Piano di snellimento del fork (verso MentorSlicer)

Analisi del 2026-10-20 fatta da 5 agenti in sola lettura sul branch `advisor` (commit `14528e0e`). Nessun file di codice è stato modificato.

## Decisioni già prese

- Il fork **non segue più** le versioni di Creality Print: si cancella davvero (file e CMake), senza interruttori di compilazione.
- L'app **non controlla più** gli aggiornamenti di Creality.
- Nome futuro: **MentorSlicer**. Il cambio di nome viene alla fine, separato dalle rimozioni.
- Si stampa solo in rete locale (K1C), senza account Creality.

## Cosa si tiene (non si tocca)

- **Print Advisor**: `simple/printAdvisor/*`, `PrintAdvisorMonitor`, `DeviceTelemetryCache`, `WebSocketProxyAdvisor`, il sidecar MCP.
- **SlicerBridge** (`simple/bridge/SlicerBridge*`): il Print Advisor lo usa per preset, configurazione, slicing, sbalzi e schema dei parametri.
- **Stampa in rete locale**:
  - scheda Dispositivo (`PrinterMgrView`, `resources/web/deviceMgr`);
  - invio (`print_manage/App/SendToPrinter`, `resources/web/sendToPrinterPage`);
  - `RemotePrinterManager` con `Klipper4408Interface`, il percorso reale della K1C;
  - mDNS, `DeviceTlsPolicy` e `resources/cert/ca.crt`;
  - il server HTTP locale (porta 13666), che serve le pagine e le immagini della stampante.
- **Motore di slicing**, con OpenVDB (supporti ad albero), OCCT (STEP), OpenCV, Assimp e le librerie Creality CR_*.
- **PresetUpdater**, solo per l'installazione locale dei profili: si toglie la parte online.
- **`GUI_SIMPLE` deve restare ON**: senza, il Print Advisor non viene compilato.
- **Calibrazioni**: quelle di Orca (`Plater::calib_*`) usate dal Print Advisor, più `resources/calib` e `resources/creality_models`.

## Cosa è emerso

### Privacy: da sistemare per primo

- **Analisi delle stampe inviate senza consenso.** A ogni invio l'app manda a `api-dev.crealitycloud.cn` l'impronta del file, la stampante, il filamento e tutti i parametri di slicing. Lo fa anche con il consenso privacy a "no": nel log del 2026-10-20 l'invio è partito alle 17:19, subito dopo quello alla K1C. Il codice è in `AnalyticsDataUploadManager`, chiamato da 22 file.
- **Indirizzo MAC del PC** nelle intestazioni (`__CXY_DUID_`) di ogni chiamata a Creality all'avvio: controllo aggiornamenti, lista stampanti, materiali e colori.
- **Intestazioni globali.** `Http::set_extra_headers` è globale: dopo la prima chiamata al cloud, le stesse intestazioni finiscono in tutte le richieste successive, compresi gli invii in rete locale alla stampante. Contengono il MAC e, con login, il token.
- **Log.** Le intestazioni, token compreso, vengono scritte nel log a livello errore.
- **Google Analytics / Firebase** nella pagina Home, chiamati dagli eventi di clic.
- **Server di prova.** La versione "Alpha" parla con i server di test di Creality (`admin-pre`, `api-dev`, `cxagent-dev`). Per questo la lista stampanti scaricata non conteneva la K1C.
- **GitHub Actions.** In `.github/workflows` ci sono job giornalieri (`creality_bot`, `schedule_bot`) e job lanciati a ogni push. Se le Actions sono attive sul tuo fork, girano già.

### Codice morto o inutile

| Area | Dimensione | Stato |
|---|---|---|
| Codice Bambu (DeviceManager, NetworkAgent, AMS, monitor, calibrazione Bambu, multi-macchina, liveview) | ~72.000 righe, 63 file su ~458 dell'interfaccia | già spento: `on_init_network` esce subito e non crea nessun oggetto |
| Chat IA Creality (cxagent, MCPChatPanel, toolcalls, sendWorkflow cloud) | ~18.000 righe | attiva, usa server cloud |
| Modalità AI / easy mode (interfaccia semplificata) | ~7.200 righe, più ~200 punti nel codice | si attiva con il pulsante "Pro" |
| Login, account, sincronizzazione profili, upload cloud, stampa cloud, OSS, MQTT | ~15.000 righe | inattiva senza account, ma compilata |
| Pagine già morte (Monitor, MultiMachine, Project, CalibrationPanel, PrinterWebView) | ~6.100 righe | mai create |
| Controllo aggiornamenti e updater | ~3.000 righe | attivo a ogni avvio |
| Sondaggio di soddisfazione, feedback, finestra privacy | ~2.800 righe | attivo |

### Repository e risorse

| Cosa | Spazio |
|---|---|
| Dimensione attuale tracciata | 595 MB, 15.663 file |
| `clang-tidy/` (eseguibile da 61 MB) | 63 MB |
| Profili di altre marche (restano Creality e Custom) | ~52 MB, ~5.260 file |
| Binari ARM64 (mesa, WebView2, GMP) | ~64 MB |
| Firebase per macOS (`deps/Darwin`) | 14 MB |
| Font coreani mai usati (`NotoSansKR`) | 12 MB |
| 20 lingue oltre italiano e inglese | ~17 MB, più 14 MB di `.mo` non tracciati |
| Pagine web inutili (`guide/swiper`, `include`, `model`, `login`, `homepage2`, `orca`) | ~14 MB |
| CI di Creality, script di altre piattaforme, file spazzatura nella radice | ~2 MB |
| `tests/engine_test` (test Python di Creality) | 35 MB |
| `src/tracy` (si usa solo `public/`) | ~18 MB |
| `doc/` di Creality (si tengono `doc/MCP` e `doc/branding`) | ~11 MB |

Totale stimato: circa **230 MB e 7.000 file** in meno nel repository, e circa **80 MB** in meno nella copia stabile. La cartella `.git` (552 MB) si riduce solo riscrivendo la storia: è opzionale, e lo decidi tu.

## Fasi

Ogni fase si fa su un branch `lean` creato da `advisor`, con un commit per ogni funzione tolta, così ognuna si può annullare da sola. Ogni fase finisce con **una sola build** e con il protocollo di verifica.

### F0. Punto di partenza (nessuna modifica al codice)

- Tag `pre-lean` sul commit attuale.
- Slicing da riga di comando, con l'exe attuale, di 4 progetti di prova:
  - K1C con PLA;
  - ASA per pezzi grandi, con soak e brim a 3 strati;
  - due oggetti con supporti;
  - un piatto di calibrazione.

  Si salva il G-code: da qui in poi ogni fase deve dare **lo stesso G-code**.
- Smoke test MCP e controllo manuale di avvio, salvati come riferimento.

### F1. Privacy e rete (priorità massima, rischio basso)

1. **Analisi delle stampe.** `AnalyticsDataUploadManager` diventa una classe vuota con la stessa interfaccia, che non invia nulla. Poi si tolgono le chiamate file per file. Il G-code perde la riga `creality_task_id_pending`.
2. **Controllo aggiornamenti.** Si tolgono l'avvio automatico, la voce del menu Aiuto, `check_new_version*`, `AppUpdater`, `AppUpdateDialogs`, `PrintAdvisorUpdateNotice` e `tools/updater`. **Restano** `PresetUpdater` (installazione locale dei profili), `UpdateDialogs` e le finestre ancora usate in `ReleaseNote.cpp`.
3. **Liste dal disco.** Lista stampanti, materiali e colori si leggono dal disco: si copiano `materialList.json` e `filaments_color.json` in `resources/profiles/Creality`, e si ripiega su `resources` invece che sul cloud. Questo risolve anche la lista senza la K1C.
4. **Intestazioni.** Si tolgono le intestazioni globali (MAC e token) e la loro scrittura nel log. Si toglie anche la richiesta HTTP verso indirizzi arbitrari che la pagina Home può far partire.
5. **Sondaggio e feedback.** Si tolgono il sondaggio di soddisfazione, il pulsante e la voce "Feedback utente", e la finestra privacy di Creality.
6. **GitHub Actions.** Si toglie `.github/workflows`, cioè i job automatici su GitHub.

- **Codice morto lasciato da F1, da togliere in una fase successiva** (note del revisore del 2026-10-21): blocco irraggiungibile in `PrinterCover::sync_printer_cover`; `ProfileFamilyLoader::reload_machine_list` senza chiamanti e commenti nell'header non piÃ¹ veri; helper `update_package_*`/`UpdateParamPackage` in `GUI_App.cpp`; `update_http_extra_header` (solo per l'agente Bambu, via con F5); `PrinterMgrView` compone il nome utente MQTT cloud senza `__CXY_DUID_` (solo con login cloud, via con F4); `JenkinsPipeline` firma ancora `updater.exe` (F6).

### F2. Interfaccia (le 8 voci già decise, più il pulsante IA)

- **Schede.** Restano solo **Prepara, Anteprima e Dispositivo**. L'enum delle schede viene rinumerato a `{0, 1, 2}`, così indice e valore coincidono, e il compilatore segnala ogni punto da sistemare. Attenzione ai `select_tab(0)` scritti come numero e all'impostazione `default_page`.
- **Elementi da togliere:**
  - "Creazione con IA" e "Modelli Online" (richiesta ribadita dall'utente il 2026-10-21: via le schede **e tutto il codice dietro**, cancellato in F3, non solo nascosto);
  - il pulsante "Pro ⇌", con easy mode forzato a spento;
  - "Feedback utente" e "Carica (3mf) su CrealityCloud";
  - il pulsante fluttuante dell'assistente IA sulla vista 3D.
- **Miglioramenti:**
  - riquadro "Temp camera" più largo;
  - riquadri dei filamenti più larghi, al massimo 3 per riga;
  - preriscaldo a orario.

- **Note del revisore F2 (2026-10-21), da sistemare dopo la prova manuale**: separatore solitario nella barra in alto a sinistra dei pulsanti finestra (`m_feedback_separator_item`, tenuto per `GetTitleDisplayRect`: trasformarlo in uno spacer da 0 px se si vede); icone non piÃ¹ usate `tab_ai_creation*.svg`, `topbar_mode_switch*.svg`; su macOS/Linux la pulizia di `easy_print_mode`/`pro_mode` in AppConfig non gira (solo `_WIN32`), innocuo perchÃ© `easy_mode()` Ã¨ sempre falso. Il riquadro del filamento singolo Ã¨ limitato a 2 larghezze standard (decisione presa da Claude).

### F3. Chat IA e modelli online

- Si tolgono:
  - `MCPChatPanel`, `CxAgentClientBridge`, `SAgentMqttBridge`, `simple/toolcalls/*`;
  - `sendWorkflow` (la parte cloud), `CxAgentEndpointPolicy`;
  - `ModelDetailDialog`, `WebModelLibraryView`, `ModelMall`, `PublishDialog`, `AICloudServiceDialog`;
  - l'eshop.
- In SlicerBridge si tolgono solo le azioni dei modelli online. La funzione `ShouldSuppressSliceWarningForAI` si sposta in `SlicerBridgeState.cpp`.
- **Verifica chiave:** il Print Advisor (contesto, preflight, sbalzi, slicing) deve funzionare come prima.

- **Note del revisore F3 (2026-10-21)**: `SEND_TO_PRINTER` resta registrato nel bridge (risponde sempre `AI_SEND_WORKFLOW_UNAVAILABLE`) e `GetActionListJSON`/`GetAvailableToolsJSON` non hanno piÃ¹ chiamanti: da togliere in F5/F7. Decisioni prese da Claude: `sendWorkflow` cancellata per intero (variante A), link "Compra filamento" nascosto con le patch web 12/12b, delegato di invio del bridge eliminato.

### F4. Account e cloud

- Si tolgono:
  - **login**: `LoginDialog`, `WebUserLoginDialog`, `LoginTip`, la route OAuth `/login` nel server locale, `resources/web/login`;
  - **sincronizzazione dei profili**: `SyncUserPresets`, `CommunicateWithCXCloud`;
  - **upload e stampa cloud**: `Upload3mfToCloud`, `UploadGcodeToCloud`, `UploadFile`, `VideoOssUploader`, `KlipperCXInterface`, `CxCloudPrint*`;
  - MQTT cloud e condivisione dei timelapse.
- **Restano come funzioni vuote** `is_login()` (sempre falso) e `get_user()` (vuoto), perché circa 30 file e la pagina Dispositivo li interrogano. `AccountDeviceMgr` resta per i gruppi di dispositivi, senza sincronizzazione.
- Si tolgono dalla build l'SDK Alibaba OSS e Paho MQTT.
- **Verifica:** lista di controllo completa con la stampante. È la fase più delicata della parte cloud.

- **Fatto il 2026-10-21** (specifica `doc/specs/F4-account-cloud-spec.md`, sezioni 1-20; lista `tools/verify/removed-F4.txt`). Decisioni: D1 `PrinterCloudAuthDialog` e il login Obico in `PhysicalPrinterDialog` **tenuti** (print host esterni); D2 patch web del token nella finestra di invio applicata con id **13** (`/*PA:13*/`); D3 blocco macOS App Store in `src/CMakeLists.txt` tenuto (tolto solo l'elenco dei `.mm` cancellati); Q1 `AutoMappingLoginMsgDialog` cancellata (nessun utente); Q2 etichetta "Login Region" e Q3 descrizioni del sidecar invariate. `CxCloudPrint*` e `mqtt_client.*` erano già andati con F3. Da provare con la stampante: sezioni 17 e 19 della specifica (upload dal file list della pagina Dispositivo, pagina di dettaglio, condivisione timelapse che risponde con errore, uscita pulita con la scheda Dispositivo aperta).

### F5. Codice Bambu (~72.000 righe)

1. `use_bbl_network()` sempre falso; build e prova.
2. Pannelli e widget: monitor, AMS, calibrazione Bambu, multi-macchina, liveview, progetto.
3. Finestre e job: SelectMachine, il SendToPrinter **di Bambu** in `GUI/`, BindDialog, PrintJob, SendJob.
4. Nucleo: DeviceManager, NetworkAgent, UserManager, plugin di rete, e circa 1.000 righe morte in `GUI_App`.

**Attenzione:** esistono **due** `SendToPrinter`. Si cancella quello in `src/slic3r/GUI/` e si tiene quello in `print_manage/App/`.

- **Fatto il 2026-10-21** (specifica `doc/specs/F5-codice-bambu-spec.md`, stadi A-D; liste `tools/verify/removed-F5B.txt`, `removed-F5C.txt`, `removed-F5D.txt`). Decisioni dello stadio D: 5.1c **sì**, tolto `media` dai componenti wxWidgets in `src/CMakeLists.txt`; `F5-D-opt` **sì** (notifiche del plug-in di rete, `UpdatePluginDialog`, `refresh_plugin_tips`, pulizia dei log cifrati, `resources/printers` con la guardia in `DataDirectoryMigration.cpp`); Q1 **sì**, cancellati `filament_ams_list`/`sync_ams_list` (`PresetBundle`, unico tocco a libslic3r oltre alla guardia), la lista AMS della barra laterale e la ricolorazione degli slot in `PresetComboBoxes`; Q2 **sì**, cancellata `resources/printers`; Q4 **sì**, pulizie opzionali fatte subito; Q5 `LegacyTransitiveIncludes.hpp` **tenuto** (da potare dopo), incluso da `GUI_App.hpp`, `ReleaseNote.hpp`, `LoadOldPresets.hpp` e `PrinterPresetConfig.hpp`; Q6 stub `is_user_login()`/`is_compatibility_version()` **tenuti** (sempre falsi); Q7 **sì**, tolti il comando `homepage_modeldepot` della Home, `open_mall_page_dialog`/`open_publish_page_dialog` e `get_model_http_url`. Da provare con la stampante: protocollo di verifica completo (avvio, slicing, Dispositivo, invio, Preferenze, uscita) e controllo del log senza righe `on_init_network`/`NetworkAgent`/`DeviceManager`/`HMS`.

- **Note del revisore F4 (2026-10-21), da sistemare in F6**: nel blocco macOS App Store di `src/CMakeLists.txt` restano i framework `AuthenticationServices`/Firebase e in `src/slic3r/CMakeLists.txt` l'opzione `-F${FIREBASE_SDK_DIR}`, ormai senza sorgenti; file spazzatura tracciato `src/slic3r/GUI/mrgtmp0` (vecchia copia di `WebViewDialog.hpp`).

- **Revisore F5-D (2026-10-21)**: trovato e corretto un difetto grave (crash all'avvio: `PresetUpdater::get_printer_config_updates` copiava `resources/printers`, cancellata in D; aggiunta una guardia). Residui da togliere in F7: `AppConfig::get_hms_host` (morta, contiene `e.bambulab.com`), `m_show_http_errpr_msgdlg` e `m_last_config_version` inutilizzati in `GUI_App`, `add_common_publish_menu_items` vuota.

### F6. Repository e risorse

- **Senza decisioni da prendere:**
  - file spazzatura, CI di Creality, script di altre piattaforme;
  - `clang-tidy`, Perl, sandbox, `compare_3mf`;
  - `deps/Darwin`, binari ARM64, font `NotoSansKR`.
- **Da confermare:**
  - profili: solo Creality e Custom;
  - lingue: solo italiano e inglese, comprese le cartelle `.mo` non tracciate;
  - pagine web inutili, `tests/engine_test`, `tracy` tranne `public/`, `doc/` di Creality.
- **Fatto il 2026-10-21** (specifica `doc/specs/F6-repository-risorse-spec.md`, lista `tools/verify/removed-F6.txt`). Decisioni: lista base (sezione 1) **tutta**, compresi gli extra `src/slic3r/GUI/mrgtmp0`, `tools/vswhere.exe` e l'estensione non tracciata `.vscode/creality-build-button` + `.vsix`; **tenuti** `.claude/skills/upstream-update/` e `doc/MCP/upstream-update-plan.md` (aggiornamenti dal ramo Creality non ancora abbandonati) e `scripts/app_store.entitlements` (lo cita il blocco App Store di `CMakeLists.txt`, che resta); `deps/Darwin` cancellato insieme al blocco Firebase/`AuthenticationServices`/`GoogleService-Info.plist` di `src/CMakeLists.txt` e all'opzione `-F${FIREBASE_SDK_DIR}` di `src/slic3r/CMakeLists.txt`; il resto del blocco App Store resta. Blocco G **no** (restano i file di build Linux/macOS/Docker/Flatpak; tolto solo `-DSLIC3R_BUILD_TESTS=OFF` dai manifest Flatpak; i pacchetti GStreamer restano perché `deps/wxWidgets/wxWidgets.cmake` compila ancora wx con `wxUSE_MEDIACTRL=ON`). (a) profili **no**, (b) lingue **no**, (f) installer **no**. (c1) e (c2) **sì**: cancellato tutto `tests/` tranne `tests/print-advisor/` e `tests/regression/`, compreso `tests/gui/topbar_layout_tests.cpp` di F2 (`BBLTopbar::SetActionsCollapsed` resta); tolte da `CMakeLists.txt` le opzioni `SLIC3R_PERL_XS`, `SLIC3R_BUILD_SANDBOXES`, `SLIC3R_BUILD_TESTS`, il blocco `IS_CROSS_COMPILE` che le forzava e gli `add_subdirectory` di `xs`, `sandboxes`, `tests`, `compare_3mf`; da `src/CMakeLists.txt` l'`install` di `run_creality_on_e_cores.bat`. (d) `src/tracy` **sì**, restano `public/` e `LICENSE`. (e) `doc/` di Creality **sì**, restano `doc/MCP`, `doc/branding`, `doc/specs` e questo piano; `README.md` senza la sezione Discord e le immagini `SoftFever_doc`, con i passi di build Windows aggiornati. (h) riscrittura della storia **rimandata**. `tools/update_server` non esisteva più.

### F7. Easy mode e Home

- Si cancellano i rami dell'easy mode e i file `simple/*Simple*` dell'interfaccia semplificata. **Restano** `simple/gpu` (orientamento), `simple/filamentMapping`, `simple/bridge` e `simple/printAdvisor`.
- **Home:** decisione aperta (vedi le domande). La pagina `homepage` serve anche alle finestre "Aggiungi stampante" e "Aggiungi filamento", quindi non si cancella senza sostituirla.
- **Da togliere dalla Home in ogni caso** (segnalati dall'utente il 2026-10-21 con screenshot): il riquadro **Login/Registrati** in alto a sinistra, la voce **Parametri online** e la voce **Commenti** del menu laterale. Se la Home resta, va ripulita almeno di questi tre.
- **Fatto il 2026-10-21** (specifica `doc/specs/F7-easy-mode-home-spec.md`, lista `tools/verify/removed-F7.txt`). Decisioni: Home **opzione B** (resta, con le patch web **14** e **15**, numerate così perché 12, 12b e 13 erano già usate; i regex della specifica combaciavano ancora, nessun adattamento); nel menu resta solo "Progetti" (via Login/Registrati, Parametri online, Guide, Centro personale, Commenti, negozio); EasyPrintSender era già andato con F3 (niente da fare); `simple/filamentMapping` ridotta a `FilamentMappingService`, `SceneFilamentSourceSnapshotManager` e `match_color`; `GLGizmoSimplePaint` tolto del tutto (era raggiungibile in Pro solo con Ctrl+N a filamento singolo); azioni `list_printers`/`select_printer` tolte da SlicerBridge (il Print Advisor non le usa; le costanti `ActionID` restano); cancellati i bundle web di 2.5 (`login` era già andato con F4). Deviazioni: `AppConfig.cpp` (libslic3r) **non** è stato toccato, per il vincolo di non modificare `src/libslic3r`: resta il `set("easy_print_mode", "0")` di F2 (la riga `m_storage.erase` proposta dalla specifica avrebbe comunque cancellato una sezione, non la chiave: servirebbe `erase("app", "easy_print_mode")`); tolto anche l'accessor `get_png_textures_simple` e il ramo `##obj_drawer` di `GUI_ObjectList`; `should_hide_easy_mode_scene_side_controls`, il dock IA e `ArrangeJob`/`OrientJob` erano già puliti da F2/F3. Da provare dal vivo: Home con solo "Progetti", nessuna connessione Google/Creality, finestre "Aggiungi stampante/filamento", protocollo di verifica completo.

- **Revisore F7 (2026-10-22)**: nessun difetto. Residui da togliere con il codice morto: URL `web/orca/missing_connection.html` in `Plater.cpp` (evento mai gestito), `ObjectList::get_device_list_data` senza chiamanti, shader `thumbnail_recolor_no_light` senza utenti, `AppConfig.cpp` imposta ancora `easy_print_mode=0`. Ctrl+N con un solo filamento non fa piÃ¹ nulla (SimplePaint rimosso, deciso).

### F7-bis. Residui di codice morto (2026-10-22)

- **Fatto** (commit "Cleanup: remove the dead-code leftovers noted by the F1-F7 reviewers"): tutti i residui segnalati dai revisori F1, F3, F5-D e F7 (helper `update_package_*`/`UpdateParamPackage`, `update_http_extra_header`, `reload_machine_list`, blocco irraggiungibile di `sync_printer_cover`, `add_common_publish_menu_items`, URL `missing_connection.html` con `load_printer_url`/`LoadPrinterViewEvent` senza gestore, `get_device_list_data`, `get_hms_host`, shader `thumbnail_recolor_no_light`, chiave `easy_print_mode`, azione `send_to_printer` e cataloghi JSON del bridge).
- **Fatto** (commit "Cleanup: remove the remaining Bambu leftovers"): file `BBLStatusBar.*`, `PrinterWebView.*`, `SendSystemInfoDialog.*`, `BambuPlayer/`; finestre `Newer3mfVersionDialog`, `ReleaseNoteDialog`, `SecondaryCheckDialog`, `PrintErrorDialog` e i loro eventi; `use_bbl_network()` e i suoi rami; rami `bLogin` in `LoadOldPresets`; `BBL_RELEASE_TO_PUBLIC` risolto staticamente nei sorgenti GUI (pagina di debug delle Preferenze, selettore ambiente IoT, `is_engineering_region`, macro `ENV_*_HOST`); notifiche `BBLUserPresetExceedLimit`/`BBLPrinterConfigUpdateAvailable` e `do_printer_config_update`; helper login di `WebViewPanel` e il suo timer da 2 s; `jump_to_monitor`/`jump_to_multipage`; multi-macchina (`is_enable_multi_machine`, `ePrintMultiMachine`); catena `download_project`; corpi `is_privacy_checked` nei punti di esportazione; `enable_user_preset_folder`, `is_studio_active`, `getExtraHeader`, `get_local_models_path`, `has_model_mall`/`update_publish_status`, `ams_btn`.
- **Lasciati apposta** (richiedono verifiche): A7 download modelli cloud (`ModelDownloader`, `CloudDownloadProgressDialog`, `BBLStatusBarSend`: la pagina Home contiene ancora i comandi `3mf_download_start`/`models_download_start`); `AccountDeviceMgr` e i gestori `*_relate_to_account` di `PrinterMgrView` (no-op con `is_login()` falso; da togliere insieme a `UserInfo` riscrivendo il gestore web `get_user` di `SendToPrinter.cpp`); classi `BBLTask`/`BBLProfile` di `ProjectTask.hpp` (membri di `BBLProject`, usato dal 3MF); macro `BBL_RELEASE_TO_PUBLIC` in `version.inc` (letta ancora da `CrealityPrint.cpp` e `WebViewDialog.cpp`); `is_privacy_checked` nei quattro punti rimasti e `track_event` (no-op); `EVT_GLTOOLBAR_PRINT_MULTI_MACHINE`; URL wiki Bambu in `CreatePresetsDialog.cpp`; logo Bambu in `TextureImportDialog.cpp`.
- Revisore (2026-10-22): tre difetti trovati e corretti prima del commit (definizione di `Plater::priv::get_config` cancellata per errore, `list.txt` con sei file eliminati, `erase` della chiave `easy_print_mode` sulla sezione sbagliata) più un include mancante (`EVT_CHECKBOX_CHANGE` in `MsgDialog.cpp`) trovato dalla build.

### F8. MentorSlicer

- Nome, logo, icone, installer, chiavi di registro, README, sidecar `mentorslicer-mcp`, e controllo aggiornamenti sui rilasci GitHub del fork.
- Scansione dei segreti (per esempio con gitleaks) prima di pubblicare.
- **Decisioni del 2026-10-22**:
  - cartella dati nuova `%APPDATA%\MentorSlicer`; al primo avvio si copia una volta quella di Creality (`Creality\Creality Print\7.3 Alpha`), che resta intatta come backup;
  - cambio di nome completo: `MentorSlicer.exe`, `MentorSlicer_Slicer.dll`, server MCP `mentorslicer-advisor`, con script, pulsanti VS Code, `publish-stable` e registrazione MCP di Claude Code;
  - nessun controllo dei rilasci su GitHub: gli aggiornamenti restano ricompilando.
  - (dalle domande della specifica `doc/specs/F8-mentorslicer-spec.md`, parte F) `SLIC3R_APP_KEY` = `MentorSlicer` (configurazione `MentorSlicer.conf`, `Company="MentorSlicer"` nei 3MF; `Application` resta `Creality_Print`); copia stabile nella nuova cartella `C:\MentorSlicer` con il collegamento "MentorSlicer", la vecchia `C:\CrealityPrint-Advisor` resta di riserva; lo script toglie il vecchio collegamento "Creality Print Advisor"; tutti i messaggi che nominano l'app diventano MentorSlicer, con l'italiano aggiornato; il supporto alla vecchia copia resta finché `C:\CrealityPrint-Advisor` non viene cancellata.
- **Implementato il 2026-10-22, non ancora compilato né provato** (passi E1-E4 della specifica; E5, installer e identificativi Linux/macOS, non fatto):
  - E1: nome `MentorSlicer` in `version.inc`; cartella dati `%APPDATA%\MentorSlicer` con importazione una tantum della cartella di Creality Print (`DataMigration::import_legacy_directory`, finestra di errore in `GUI_Init.cpp`, correzione di `download_path`); valori fissati apposta (`Config.cpp`, `Http.cpp`, dominio gettext `SLIC3R_I18N_DOMAIN`); titolo della finestra e ricerca dell'altra istanza; icone rinominate `MentorSlicer*` (cancellate le tre `_gradient`); messaggi dell'app e del Print Advisor con il nuovo nome e italiano in `PrintAdvisor_it.po`; sonda del vecchio `endpoint.json` in `check_no_other_instance`;
  - E2: script (`close-build-instance`, `publish-stable` verso `C:\MentorSlicer`, `net-watch`, `verify-phase`, `slice-fixtures`) e pulsanti VS Code;
  - E3: sidecar `mentorslicer-advisor` con la variabile `MENTORSLICER_ADVISOR_DESCRIPTOR` (la registrazione in `~/.claude.json` va rifatta a mano);
  - E4: `CLAUDE.md`, `README.md`, lista di controllo della stampante, `tools/verify/removed-F8.txt`.

### Pulizia del 2026-10-23 (codice non usato e servizi Creality)

Verifica approfondita con 5 agenti in sola lettura (interfaccia, stampa in rete locale e pagine web, motore, risorse e dipendenze, chiamate di rete delle pagine), poi 5 blocchi con una build e un commit ciascuno; tag di partenza `pre-cleanup-20261023`.

- **Blocco 1** (`703a3c0b`): file non compilati (vecchie copie `Support/`, `Interlocking/`, gizmo non compilati, `KlipperSim` di prova), 447 immagini, file di calibrazione `.3mf`/`.step` mai caricati, certificati inutili, immagini e pagine web non referenziate, `OpenCSG`, Squirrel con il feed di aggiornamento Creality, `tools/data_migration`, moduli CMake e opzioni morte.
- **Blocco 2** (`bd106ab8`): SlicerBridge ridotto alle funzioni del Print Advisor, `filamentMapping`, `AccountDeviceMgr`, gestori web oscurati o mai chiamati, `OctoPrintInterface`, `MaterialMapPanel`, `TestHelper` (porta TCP 12345).
- **Blocco 3** (`53204c78`): `ConfigWizard`, `Config/Snapshot`, download dei modelli dal cloud, `AnalyticsDataUploadManager` e le sue chiamate, intestazioni HTTP globali, finestre di aggiornamento, eventi wx morti, barra di debug della WebView, link alla privacy di Creality.
- **Blocco 4** (`77976e97`): motore: test interni Creality, `AUTOMATION_TOOL`/`AUTO_CONVERT_3MF`, `Serialization`, `Debugger`, `creality_task_id`, export 3MF CxCloud, classi BBL inutilizzate, helper di sincronizzazione profili senza chiamanti, invio dei crash per e-mail (SMTP verso `DUMPTOOL_*`), Breakpad, `BBL_RELEASE_TO_PUBLIC`, chiavi di `MentorSlicer.conf` mai lette. Formati G-code, 3MF, config e `.info` invariati.
- **Blocco 5** (`f4fe5316`): foto delle stampanti in locale invece di `pic-cdn.creality.com`, patch web 20 (niente baidu.com nella diagnosi), `CrProject`.
- **Revisione** (`53d933b9`): corretto un crash (pulsante "aggiorna ugello" non più creato ma ancora aggiunto alla finestra), `ENABLE_BREAKPAD` spento nelle dipendenze.
- **Decisioni dell'utente**: motore pulito con confronto del G-code; funzioni Orca non chiamate tolte solo nei file già nostri o legati al cloud; `TestHelper` via; chiavi di config via; link wiki utili tenuti; file di build non tracciati cancellati (~400 MB); i file in `.claude/` non si toccano.
- **Verifiche**: G-code identico e deterministico su 4 fixture `tests/regression/fixtures/cleanup-*.3mf` (ricavate da un progetto dell'utente, non tracciate); `check-gcode.mjs` 20/20; smoke MCP 13/13 sulla build e sul pacchetto `build\OrcaSlicer` reinstallato da zero (510 MB, 8.898 file).
- **Blocco 6 (snellimento, stesso giorno)**: profili solo Creality e Custom (34 marche, ~5.260 file, ~51 MB; nessun profilo Creality eredita da altre marche); installer di runtime tolti dal pacchetto e dal template NSIS (compreso un invio di telemetria dell'installazione con `curl.exe` verso `devdata.cxswyjy.com`); patch web 21 (procedura del primo avvio senza "modalità software" e "area login"); `BaseException`/`StackWalker` tolti; voce "Zoom of view" tolta. Decisioni: lingue, video RTSP e import STEP **tenuti**; riscrittura della storia Git **rimandata**. G-code identico, smoke MCP 13/13.
- **Restano**: script Linux/macOS che passano ancora opzioni tolte; `SliceCommand.cpp` cerca ancora `profiles/BBL/cli_config.json` (solo un avviso nel log in modalità riga di comando).

## Protocollo di verifica per ogni fase

1. **Build** con `build_exe_only.bat`, con riconfigurazione se cambia un CMakeLists.
2. **`git grep`** dei nomi tolti: nessun risultato fuori dai file cancellati. Aggiornare `localization/i18n/list.txt`.
3. **G-code** dei 4 progetti di prova identico a F0, più `check-gcode.mjs`.
4. **Patch web**: `apply-web-patches.mjs --check`.
5. **Sidecar**: `npm run typecheck` e `npm run smoke` con l'app aperta.
6. **Avvio manuale** (circa 3 minuti):
   - l'app parte senza errori;
   - in Prepara si carica un modello e si fa lo slicing;
   - Anteprima funziona;
   - Dispositivo è collegato alla K1C, con telemetria e telecamera;
   - si apre la finestra di invio;
   - il Print Advisor è attivo;
   - si aprono le Preferenze;
   - l'app si chiude in modo pulito.
7. **Rete**, dopo F1, F3 e F4: 5 minuti con il monitor di rete di Windows, senza connessioni verso `*.crealitycloud.*`, `*.creality.com` o Google.
8. **Stampante**, nelle fasi che toccano Dispositivo o invio: lista di controllo (`doc/MCP/printer-test-checklist.md`), solo a stampante ferma.

## Domande aperte

1. **Home**: DECISO il 2026-10-21: la scheda resta. Si toglie solo ciò che riguarda il login e il cloud: riquadro Login/Registrati, Parametri online, Commenti, Centro personale (modelli, preset, download) e il tracciamento Google.
2. **Solo Windows?** DECISO il 2026-10-21: si lasciano Linux, macOS, Docker e Flatpak (costo nullo, utili se il fork verrà pubblicato). Non si compilano né si verificano.
3. **Profili**: DECISO il 2026-10-21: per ora si tengono anche le altre marche.
4. **Lingue**: DECISO il 2026-10-21: per ora si tengono tutte.
5. **Fluidd / Moonraker / OctoPrint**: DECISO il 2026-10-21: si lasciano tutti gli host esterni (compresi Obico e SimplyPrint, quindi `PrinterCloudAuthDialog` e `OAuthJob` restano).
6. **Mouse 3D (SpaceMouse)**: DECISO il 2026-10-21: si lascia.
7. **Test e documentazione di Creality**: DECISO il 2026-10-21: si cancellano `tests/engine_test` (regressione interna Creality, richiede build RelWithDebInfo; il nostro equivalente è `check-gcode.mjs`) e `doc/` di Creality tranne `doc/MCP`, `doc/branding`, `doc/specs` e il piano di pulizia.
8. **Installer di runtime** in `package/runtime`: DECISO il 2026-10-21: per ora si lasciano (WebView2 resta non tracciato).
9. **Storia Git**: DECISO il 2026-10-21: si riscrive per alleggerire `.git` (552 MB), con push forzato su origin. Da fare per ultima, dopo le fasi di pulizia, con backup del repository prima di partire.
