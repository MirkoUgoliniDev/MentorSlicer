# Punti di contatto con il codice Creality

Elenco dei file di Creality che modifichiamo e delle API di Creality da cui dipendono i nostri moduli. In un aggiornamento si controlla questa lista invece di tutto il codice (vedi `upstream-update-plan.md`, fase 2).

## Convenzioni

- Nei file di Creality ogni nostro blocco è racchiuso fra `// PRINT-ADVISOR BEGIN <tema>` e `// PRINT-ADVISOR END` (in CMake `# PRINT-ADVISOR ...`).
- Una riga singola porta in coda `// PRINT-ADVISOR <tema>`.
- Le righe tolte da Creality sono segnalate con un commento `PRINT-ADVISOR <tema>: ...` nel punto dove erano, oppure con `(upstream: ...)` dopo il marcatore.
- Ricerca completa: `git grep -n "PRINT-ADVISOR" -- src`.
- Temi:
  - `advisor-hook`: aggancio verso i nostri moduli;
  - `fork-update`: tolto in F1 insieme al controllo aggiornamenti di Creality (`PrintAdvisorUpdateNotice.*` non esiste piu');
  - `heat-soak`, `brim-layers`: opzioni aggiunte al motore;
  - `chamber-ui`: comportamento delle schede;
  - `build-fix`: correzioni di build per MSVC;
  - `uaf-fix`: correzione di un uso dopo la liberazione.

## File nostri dentro le cartelle di Creality

Non vanno in conflitto, ma dipendono dalle API elencate più sotto.

| File | Contenuto |
|---|---|
| `src/slic3r/GUI/simple/printAdvisor/*` | server, rotte, conferme, agganci (`PrintAdvisorHooks.*`), ponte verso la pagina dispositivi (`PrintAdvisorPageBridge.*`), elenco dei sorgenti (`PrintAdvisor.cmake`) |
| `src/slic3r/GUI/print_manage/App/DeviceTelemetryCache.*` | telemetria LAN |
| `src/slic3r/GUI/print_manage/App/PrintAdvisorMonitor.*` | monitor (preriscaldo, guardia, raffreddamento) |
| `src/slic3r/GUI/print_manage/App/WebSocketProxyAdvisor.*` | `SendToDevice`, registro dei proxy, host aperti, agganci per la telemetria |

## File di Creality modificati

### Agganci (`advisor-hook`)

| File | Aggancio | Scopo | Da controllare all'aggiornamento |
|---|---|---|---|
| `src/slic3r/CMakeLists.txt` | 1 riga: `include(.../PrintAdvisor.cmake)` prima di `add_library(libslic3r_gui ...)` | aggiunge i nostri sorgenti a `SLIC3R_GUI_SOURCES` | nome della variabile, posizione dopo `option(GUI_SIMPLE ...)` |
| `GUI/GUI_App.cpp` | include + `advisor_on_shutdown()` all'inizio di `OnExit` | arresto del server | `OnExit` esiste ancora |
| `GUI/GUI_App.cpp` (`fork-update`) | nessuno: il controllo aggiornamenti di Creality (`EVT_SLIC3R_VERSION_ONLINE`, `AppUpdater`, `UpdateVersionDialog`, `process_update_packages`) e il gancio `advisor_show_fork_update_notice` sono stati eliminati in F1 | - | se un aggiornamento di Creality reintroduce il controllo versioni, va tolto di nuovo (vedi `doc/specs/F1-privacy-rete-spec.md`, voce 2) |
| `GUI/MainFrame.cpp` | include; `advisor_on_app_init()` dopo `start_http_server()`; `advisor_on_shutdown()` in `shutdown()`; `advisor_create_ai_menu()` in `init_menubar_as_editor` | avvio, arresto, voce **Integrazione AI → Print Advisor MCP** (finestra `AdvisorInfoDialog` con Abilita/Disattiva) | i tre punti; `viewMenu` è ancora un `wxMenu*` |
| `print_manage/App/PrinterMgrView.cpp` | include; `incoming` non più `const`; `if (advisor_handle_page_message(incoming)) return;` in `OnScriptMessage` | risposta della pagina alla foto della telecamera, prima del log | forma di `OnScriptMessage` |
| `print_manage/App/PrinterMgrView.hpp` | `is_page_loaded()` (accesso a `m_browser`, `m_webview_loaded_successfully`, privati) | sapere se la pagina dispositivi è pronta | nomi dei due membri |
| `print_manage/App/SendToPrinter.cpp` | include `PrintAdvisorPageBridge.hpp`; 1 riga `advisor_append_send_page_data(top_level_json);` prima del `dump` in `get_onlygcode_plate_data_on_show` e `get_plate_data_on_show` | aggiunge `advisor_chamber_temps` (`{host: boxTemp}` dalla `DeviceTelemetryCache`) al messaggio `update_plate_data` della finestra di invio; la pagina (patch web 10) mostra l'avviso "camera fredda" per ASA/ABS | i due punti esistono ancora e il messaggio si chiama ancora `update_plate_data` con `filament_types` e `plate_extruders` |
| `print_manage/App/WebSocketProxySession.cpp` | include; in `Session`: `is_open()`, `TryEnqueueText()`; in `Manager::Impl`: base `advisor_link::DeviceSender`, `SendToDeviceHost()`, membro `advisor_hosts`, 4 righe `advisor_hosts.publish(...)` (fine di `Submit`, evento `open`, sessione finita, `Shutdown`), `on_device_message` sui messaggi del dispositivo, `on_session_finished`, registrazione nel costruttore di `Manager` | invio di messaggi alla stampante sulla connessione della pagina; telemetria | nomi privati usati: `m_state`, `State::Open/Closing`, `m_finished`, `m_close_requested`, `m_write_queue`, `m_write_in_progress`, `m_queued_bytes`, `MAX_*`, `PumpWrite`, `sessions`, `runtime.executor()`, `accepting`, `shutting_down`, `is_device_connection()`, `host()` |
| `print_manage/RemotePrinterManager.*` | `hasPendingUpload()` | il monitor non agisce durante un caricamento | `m_mtxUpload`, `m_latestTaskByAddress`, `m_tasksById`, `terminal` |
| `simple/bridge/SlicerBridge.hpp` | `AnalyzeOverhang` spostata da privata a pubblica | analisi degli sbalzi | firma della funzione |
| `simple/bridge/SlicerBridgeActionsConfig.cpp` | `item["is_vector"]` nello schema | il server distingue le opzioni vettoriali | `BuildConfigSchemaArray` |
| `libslic3r/AppConfig.cpp` | default `print_advisor_mcp_enabled = false` | Print Advisor spento di default | `set_defaults` |

### Motore e schede

| File | Tema | Scopo | Da controllare |
|---|---|---|---|
| `libslic3r/PrintConfig.hpp` | `heat-soak`, `brim-layers` | 4 opzioni nelle macro `PRINT_CONFIG_CLASS_*` (`heat_soak_time`, `heat_soak_bed_temp`, `brim_layers`, `brim_layers_object_gap`) | nomi nuovi uguali a opzioni di Creality/Orca |
| `libslic3r/PrintConfig.cpp` | `heat-soak`, `brim-layers` | definizioni, limiti, testi | `init_fff_params` |
| `libslic3r/Preset.cpp` | `heat-soak`, `brim-layers` | chiavi in `s_Preset_print_options` / `s_Preset_filament_options` | le due liste |
| `libslic3r/Print.cpp` | `heat-soak` | invalidazione del solo G-code | `invalidate_state_by_config_options` |
| `libslic3r/PrintObject.cpp` | `brim-layers` | invalidazione del solo G-code (`brim_layers`, `brim_layers_object_gap`) | stessa funzione |
| `libslic3r/GCode.hpp` | `brim-layers` | `m_brim_last_layer` | vicino a `m_objsWithBrim` |
| `libslic3r/GCode.cpp` | `brim-layers` | azzeramento in `do_export`; funzioni `set_brim_path_height`/`append_repeated_brim`; blocco brim di `process_layer` (sostituisce `object_first_layer`); sui layer > 0 il brim è ritagliato con `brim_layers_object_gap` | logica del brim di Creality, `extrude_entity`, `brim_map_by_instance`, `lslices` |
| `libslic3r/GCode.cpp` | `heat-soak` | soak prima di `machine_start_gcode`, con il ramo Klipper | condizione `gcode_flavor != gcfKlipper ...`, `_print_first_layer_bed_temperature`, `set_exhaust_fan`, `set_additional_fan`, `sync_bed_temperature` |
| `print_manage/App/SendToPrinter.cpp` | `heat-soak` | tempo di stampa con il soak nella pagina di invio | `get_gcode_display_info` |
| `GUI/Tab.cpp` | `brim-layers`, `heat-soak` | righe nelle schede Processo (`brim_layers`, `brim_layers_object_gap`) e Filamento | `TabPrint::build`, `TabFilament::build` |
| `GUI/Tab.cpp` | `chamber-ui` | opzioni della camera disattivate sulle Creality senza riscaldatore | `TabFilament::toggle_options`, `is_creality_vendor` |
| `GUI/ConfigManipulation.cpp` | `brim-layers` | `brim_layers` attiva solo con brim e senza raft; `brim_layers_object_gap` solo se inoltre `brim_layers` > 1 | `toggle_print_fff_options` |
| `resources/images/process/ProcessConfig.json` | `brim-layers` | voci `brim_layers` e `brim_layers_object_gap` nel gruppo Brim (nessun marcatore: è JSON) | gruppo "Brim" |
| `GUI/GUI_ObjectList.cpp` | `uaf-fix` | `is_model_part()` letto prima di `delete_volume` | `del_subobject_from_object` |

### Correzioni di build (`build-fix`)

| File | Correzione |
|---|---|
| `src/CMakeLists.txt` | Breakpad disattivato se mancano header o libreria. Attenzione: la regione `PRINT-ADVISOR BEGIN/END` spezza in due il blocco `if(ENABLE_BREAKPAD)` di Creality (`add_definitions(-DUSE_BREAKPAD)` è spostato dopo il controllo). Va riapplicata come unità, non togliendo solo le righe fra i marcatori. |
| `libslic3r/CMakeLists.txt` | GMP/MPFR presi dal prefisso delle dipendenze (Strawberry Perl) |
| `libslic3r/AppConfig.cpp` | C4309 (indice `int` su `npos`), C4101 |
| `libslic3r/PresetBundle.cpp` | C4018, C4390 |
| `libslic3r/MeshBoolean.cpp`, `admesh/stlinit.cpp` | C4018 |
| `libslic3r/AutomationMgr.cpp`, `GUI/AutoConvert3mfMgr.cpp` | STL4038: tolto `<format>` inutilizzato |

Se Creality corregge da sé lo stesso punto, si prende la loro versione e si toglie il blocco.

## API di Creality usate dai nostri moduli

Se una di queste cambia firma o comportamento, va adattato il nostro codice, non il loro.

| Area | API |
|---|---|
| `GUI_App` | `wxGetApp()`, `app_config` (`get_bool`, `set_bool`, `save`), `preset_bundle`, `plater()`, `mainframe`, `get_tab(type)`, `obj_list()`, `params_panel()`, `dark_mode()`, `UpdateDlgDarkUI`, `CallAfter` |
| `MainFrame` | `get_printer_mgr_view()`, `update_slice_print_status(SlicePrintEventType)`, `IsBeingDeleted` |
| `Plater` | `model()`, `changed_object`, `take_snapshot`, `set_plater_dirty`, `update`, `arrange`, `orient`, `set_prepare_state`, `is_background_process_slicing`, `is_background_process_update_scheduled`, `sidebar().update_presets`, `calib_flowrate`, `calib_pa`, `calib_temp`, `calib_max_vol_speed`, `calib_retraction`, `get_partplate_list()`, `select_bed_type`, `on_bed_type_change`, `sidebar().update_filament_panel`, `update_project_dirty_from_presets`, `is_export_gcode_scheduled` (tipo di piatto, `PrintAdvisorPlate.cpp`; il filtro dei tipi per vendor replica `SidebarPrinter::update_bed_type`) |
| `PartPlateList` / `PartPlate` | `get_curr_plate`, `get_curr_plate_index`, `get_current_fff_print`, `get_current_slice_result`, `contain_instance`, `get_bed_type`, `set_bed_type`, `get_plate`, `get_plate_count`, `get_plate_name`, `get_extruders`, `get_print`, `get_real_print_seq`, `is_slice_result_valid`, `update_slice_result_valid_state` |
| `PresetBundle` | `prints`, `filaments`, `printers`, `filament_presets`, `is_cx_vendor` |
| `PresetCollection` | `get_edited_preset`, `get_selected_preset_name`, `get_selected_idx`, `find_preset`, `is_base_preset`, `load_preset`, `save_current_preset`, `delete_preset`, `delete_preset_keep_selection`, `path_from_name`, `select_preset_by_name`, `set_idx_selected` |
| `Preset` | `TYPE_PRINT`, `TYPE_FILAMENT`, `print_options()`, `filament_options()` |
| Config | `print_config_def.get`, `ConfigOptionDef`, `DynamicPrintConfig::option/has/keys/set_key_value/set_deserialize_strict` |
| `Tab` | `select_preset`, `update_tab_ui` |
| `ObjectList` | `delete_from_model_and_list`, `notify_instance_updated`, `reorder_volumes_and_get_selection`, `object_config_options_changed`, `update_and_show_object_settings_item`, `load_generic_subobject` |
| `ModelObject` / `ModelVolume` / `PrintObject` | `add_volume`, `id`, `instance_bounding_box`, `invalidate_bounding_box`, `raw_mesh`, `layers`, `support_layers`, `model_object`, `get_matrix`, `mesh`, `set_transformation`, `is_model_part`, `is_support_modifier` |
| Risultato dello slicing | `GCodeProcessor`, `GCodeProcessorResult` (`filename`, `warnings`, `toolpath_outside`, `print_statistics.modes`, `total_used_filament`, `total_weight`) |
| `SlicerBridge` | `Instance().Execute(ActionID::GET_PRESETS / GET_EDITED_CONFIG / START_SLICE)`, `AnalyzeOverhang`, `BuildConfigSchemaArray` |
| `PrinterMgrView` | `ExecuteScriptCommand`, `request_print_control`, `is_page_loaded` (nostro), `OnScriptMessage` (aggancio) |
| `RemotePrinterManager` | `getInstance().hasPendingUpload` (nostro) |
| `DM` | `DataCenter::Ins().get_current_device_data`, `DM::Device` |
| WebView | `WebView::IsShuttingDown` |
| Proxy WebSocket | interni di `Session` e `Manager::Impl` elencati nella tabella degli agganci |
| Utilità | `RemotePrint::Utils::url_encode` |

## Lato web e traduzioni

- Le patch alle pagine compilate sono in `web-bundle-patches.md`.
- Le stringhe del menu ora stanno in `simple/printAdvisor/PrintAdvisorHooks.cpp`: va aggiunto a `localization/i18n/list.txt` perché `xgettext` le estragga ancora (le traduzioni esistenti restano valide). Stessa cosa per `PrintAdvisorVerdict.cpp` (finestra dell'esito di fine stampa); le traduzioni italiane stanno in `localization/i18n/it/PrintAdvisor_it.po`.
