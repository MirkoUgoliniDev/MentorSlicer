# Fixture di regressione

Un paragrafo per fixture: stampante, filamento, processo, tipo di piatto, oggetti e posizioni,
valori modificati nel progetto. Da compilare quando le fixture vengono create nella GUI
(`doc/specs/F0-baseline-verifica-spec.md`, sezione 2.3). Ogni fixture ha un solo piatto e tutti gli
oggetti sono interamente dentro il piatto. Build usata: dev build `build\src\Release`, commit `<da compilare>`.

## k1c-pla.3mf

- Stampante: Creality K1C 0.4 nozzle. Filamento: Hyper PLA @Creality K1C 0.4 nozzle (sistema).
  Processo: 0.20mm Standard @Creality K1C 0.4 nozzle (sistema).
- Tipo di piatto: `<da compilare: quello usato per il PLA>`.
- Oggetti: `resources\creality_models\ksr_fdmtest_v4.stl`, lasciato dove lo mette l'importazione (centrato).
- Modifiche di progetto: nessuna.
- Note: `<da compilare>`

## asa-soak-brim.3mf

- Stampante: Creality K1C 0.4 nozzle. Filamento: ASA K1C - pezzi grandi (copia utente,
  `heat_soak_time` 10, `heat_soak_bed_temp` 100). Processo: 0.20mm ASA K1C.
- Tipo di piatto: PEI liscio (`<confermare>`).
- Oggetti: `Block20XY.stl` due volte come oggetti separati (non istanze); cubo A centro X 98 Y 110,
  cubo B centro X 122 Y 110 (4 mm tra le facce, i brim da 5 mm si sovrappongono).
- Modifiche di progetto (processo, non salvate nel profilo): tipo di brim solo esterno, larghezza 5,
  distanza brim-oggetto 0, layer del brim 3, distanza brim-oggetto sui layer superiori 0.3.
- Valori attesi: `asa-soak-brim.expected.json` (aggiungere `first_layer_z` dopo il primo G-code CLI).
- Note: `<da compilare>`

## supports-tree.3mf

- Stampante: Creality K1C 0.4 nozzle. Filamento: Hyper PLA @Creality K1C 0.4 nozzle.
  Processo: 0.20mm Standard @Creality K1C 0.4 nozzle.
- Tipo di piatto: `<da compilare>`.
- Oggetti: `Overhang.stl` (centro X 90 Y 110) e `torus.stl` ruotato di 90° attorno a X (centro X 160 Y 110).
- Modifiche di progetto: supporti abilitati, tipo albero(auto), stile predefinito, angolo di soglia del profilo.
  Entrambi gli oggetti ricevono supporti ad albero (verificato in anteprima: `<da compilare>`).
- Note: `<da compilare>`

## calib-flow.3mf

- Stampante: Creality K1C 0.4 nozzle. Filamento: Hyper PLA @Creality K1C 0.4 nozzle.
  Processo: 0.20mm Standard @Creality K1C 0.4 nozzle.
- Generato da Calibrazione → Flusso → Passaggio 1 (`Plater::calib_flowrate(1)`): blocchi con
  `print_flow_ratio` per oggetto e modifiche al processo fatte dal generatore (`layer_height`,
  `initial_layer_print_height`, `max_volumetric_extrusion_rate_slope`, ...), tutte salvate nel 3MF.
- Tipo di piatto: `<da compilare>`.
- Note: `<da compilare>`
