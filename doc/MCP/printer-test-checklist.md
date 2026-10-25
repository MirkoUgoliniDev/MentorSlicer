# Prove con la stampante dopo ogni aggiornamento (10 minuti)

Da fare dopo `npm run smoke` e `check-gcode.mjs` superati. Stampante: K1C in LAN, **ferma**, piatto vuoto e freddo, utente davanti alla stampante.

## Preparazione

- MentorSlicer aperto, Print Advisor attivo (**Integrazione AI → Print Advisor MCP**, Abilita), scheda **Dispositivo** aperta sulla K1C.
- Client MCP collegato (`claude mcp get mentorslicer-advisor` → `Connected`).
- Ogni comando apre la finestra "MCP Advisor" in MentorSlicer: l'utente legge e conferma. Una finestra annullata non deve inviare nulla.

## 1. Sola lettura (2 min)

| Prova | Tool | Risultato atteso |
|---|---|---|
| Stato | `get_printer_status` | la K1C con `state` 0 (inattiva), `bedTemp0` e `boxTemp` vicini alla temperatura ambiente, `maxBedTemp` 100, `lightSw`, `modelFanPct`/`auxiliaryFanPct`/`caseFanPct` presenti, nessun `err` attivo |
| Foto | `camera_snapshot` | un'immagine della camera; errore chiaro se la telecamera non è in riproduzione nella scheda Dispositivo |
| Monitor | `get_monitor_status`, `get_preheat_status` | nessuna guardia o preriscaldo attivo |

## 2. Luce spenta e riaccesa (1 min)

1. `set_light` con `on: false`, poi confermare: dopo 2–5 s `get_printer_status` riporta `lightSw` 0 e la luce è spenta.
2. Ripetere con `on: true` e annullare la finestra: niente cambia.
3. Ripetere con `on: true` e confermare: `lightSw` 1.

## 3. Preriscaldo di 2 minuti con spegnimento (4 min)

1. `preheat_chamber` con `bed_temp: 60`, `max_minutes: 5`, confermare.
2. Dopo 30 s: `get_printer_status` → `targetBedTemp0` 60, `bedTemp0` in salita; `get_preheat_status` → `active` true, `outcome` `running`.
3. Dopo 2 minuti: `preheat_chamber` con `bed_temp: 0`. Atteso: `outcome` `cancelled`, `targetBedTemp0` 0 entro 5 s, `bedTemp0` in discesa.
4. Facoltativo (5 min in più): ripetere senza il passo 3 e verificare che dopo `max_minutes` il piatto si spenga da solo con `outcome` `timeout`.

## 4. Ventola laterale accesa e spenta (2 min)

1. `set_fan` con `fan: "side"`, `percent: 50`, confermare: gira la **ventola laterale grande**, `auxiliaryFanPct` circa 50.
2. `set_fan` con `fan: "side"`, `percent: 0`: si ferma, `auxiliaryFanPct` 0.
3. Se gira un'altra ventola (per esempio l'estrazione posteriore, `caseFanPct`), la mappatura `M106 P1`/`P2` è cambiata nel firmware o in Creality: fermarsi e annotarlo prima di usare soak e guardia.

## Note di sicurezza

- Mai avviare una stampa da queste prove; il Print Advisor non ne è capace e non deve diventarlo.
- Piatto mai sopra 100 °C sulla K1C. Alla fine `targetBedTemp0` deve essere 0: se non lo è, spegnere il piatto dallo schermo della stampante.
- Se la telemetria si interrompe (`telemetry_lost`) il piatto **può essere ancora caldo**: controllarlo a mano.
- Un errore HTTP 409 vuol dire che c'è un'altra finestra aperta: chiuderla e ripetere una sola volta.
- Annotare in `CLAUDE.md` ogni risultato diverso da quello atteso.
