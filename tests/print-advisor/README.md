# Test G-code del Print Advisor

`check-gcode.mjs` controlla nel G-code le funzioni aggiunte al motore:

- **heat soak**: un solo blocco `; HEAT_SOAK: <min> min at bed <t>`, poi `M106 P1 S0` (se `support_air_filtration`), `M106 P2 S0` (se `auxiliary_fan`), `M190 S<t>`, `G4 P<ms>` con `ms = min * 60000 <= 3 600 000`, e subito dopo il primo comando di `machine_start_gcode` (`START_PRINT`); su Klipper monoestrusore nessun `M190`/`M140` in mezzo;
- **brim_layers**: `;TYPE:Brim` con estrusioni su ogni layer `0..N-1`, lo stesso numero di blocchi per oggetto del layer 0 (cioè una volta per layer), flusso riscalato all'altezza del layer (rapporto E entro il 10%), nessun brim sui layer `>= N`;
- **brim_layers_object_gap**: se è maggiore di `brim_object_gap`, sui layer `1..N-1` il brim ripetuto viene ritagliato attorno agli oggetti, quindi il rapporto E può solo essere minore (mai oltre il 10% in più) e il brim non deve sparire. La distanza si misura dal G-code: segmenti estrusi del brim contro i segmenti `Outer wall`/`Overhang wall` dello stesso layer (di tutti gli oggetti), meno metà delle due larghezze `;WIDTH:` (bordo del brim → contorno dello slice), con tolleranza 0,05 mm; gli archi `G2`/`G3` vengono spezzati in corde. Su ogni layer `1..N-1` è sempre un errore un segmento di brim con il punto medio dentro un'isola (anelli `Outer wall` chiusi, regola pari-dispari: i fori contano come fuori). Limite: le distanze usate sono quelle globali del blocco `CONFIG`; le modifiche per oggetto (`brim_object_gap`/`brim_layers_object_gap` impostati sul singolo oggetto nel 3MF) non compaiono nel G-code, quindi un file con distanze diverse per oggetto viene giudicato con il valore di processo. Con più oggetti la distanza si misura a parte: dalle pareti dell'oggetto del brim almeno `brim_layers_object_gap`; dalle pareti degli altri oggetti almeno il minore tra `brim_layers_object_gap` e `brim_object_gap`, come fa il motore (che per un vicino senza brim usa 0: in quel caso il controllo può fallire anche se il motore è corretto). Gli oggetti si distinguono dai commenti `; printing object`. Se `brim_layers_object_gap` non supera `brim_object_gap` la distanza viene solo stampata come informazione (con la compensazione della zampa d'elefante può essere negativa: il brim tocca la parete). I due valori si possono fissare in `.expected.json` (`brim_object_gap`, `brim_layers_object_gap`).
- **soak con `heat_soak_bed_temp` = 0**: il checker risolve la temperatura come il motore: per ogni filamento usato con `heat_soak_time` > 0, 0 diventa la temperatura del piatto al primo layer di quel filamento per `curr_bed_type`, poi vale la massima. I filamenti usati sono i comandi `T<n>` del file: una fixture ridotta può non contenere i cambi utensile successivi.

Le impostazioni si leggono dal blocco `CONFIG` in fondo al file.

```powershell
node tests/print-advisor/check-gcode.mjs                     # tutte le fixture con il loro .expected.json
node tests/print-advisor/check-gcode.mjs <file.gcode>        # un G-code qualsiasi (anche completo)
node tests/print-advisor/check-gcode.mjs <file.gcode> --expected <file.json>
```

## Fixture

`fixtures/brim3-soak10.gcode`: tubo `BorchiaMuro_tubo.stl`, filamento "ASA K1C - pezzi grandi", K1C, soak 10 min a 100 °C, brim 10 mm su 3 layer. Contiene intestazione (senza miniature), i primi 5 di 431 layer e il blocco `CONFIG`. I valori attesi sono in `fixtures/brim3-soak10.expected.json`.

**Non coperto dalla fixture attuale**: è stata generata prima di `brim_layers_object_gap` (la chiave manca nel `CONFIG`, quindi vale `brim_object_gap` = 0) e prima delle correzioni del 2026-10-20 al ritaglio del brim ripetuto (bordo del brim distante almeno `brim_layers_object_gap`, ostacolo formato da tutti gli oggetti del layer) e alla risoluzione per filamento del soak con piatto 0. Il controllo della distanza c'è ma su questa fixture è solo informativo; contiene un solo oggetto (il ritaglio contro gli oggetti vicini non è verificato) e `heat_soak_bed_temp` = 100. Per coprirli serve una nuova fixture con `brim_layers_object_gap` > `brim_object_gap` (per esempio 0,3), due oggetti vicini e un filamento con `heat_soak_bed_temp` = 0, generata con il motore corretto.

## Rigenerare dopo una modifica al motore

1. In MentorSlicer aprire un progetto con soak attivo (`heat_soak_time` > 0) e `brim_layers` = 3, poi **Slice**.
2. Il G-code dell'ultimo slicing è in `%LOCALAPPDATA%\Temp\mentorslicer_model\<data>\<ora>\Metadata\.<pid>.<piatto>.gcode`. Possono essercene più d'uno: scegliere quello giusto (numero di layer nella prima riga).
3. Ridurlo:
   ```powershell
   node tests/print-advisor/make-fixture.mjs "<percorso del .gcode>" --layers 5
   ```
   Senza percorso usa il più recente. `--out` cambia il file di uscita.
4. `node tests/print-advisor/check-gcode.mjs` e, se il cambiamento è voluto, aggiornare `brim3-soak10.expected.json`. Un fallimento non voluto va corretto nel motore, non nella fixture.

## Fixture per le nuove funzioni (da generare)

Nessuna fixture copre ancora `brim_layers_object_gap` > `brim_object_gap`, il ritaglio contro gli oggetti vicini e il soak con `heat_soak_bed_temp` = 0. Non va scritta a mano: va generata con un motore che contiene le correzioni del 2026-10-20.

1. Compilare (**Build EXE**) e avviare MentorSlicer; stampante K1C, piatto **High Temp Plate** (`hot_plate_temp_initial_layer` del filamento diverso da 0, per esempio 100).
2. Filamento: copia di "ASA K1C - pezzi grandi" con `heat_soak_time` = 10 e `heat_soak_bed_temp` = **0** (ci si aspetta `; HEAT_SOAK: 10 min at bed <hot_plate_temp_initial_layer>`).
3. Processo: `brim_type` = outer_only, `brim_width` = 5, `brim_object_gap` = 0, `brim_layers` = 3, `brim_layers_object_gap` = **0.3**, nessuna modifica per oggetto.
4. Piatto: due cubi da 20 mm affiancati a 4 mm l'uno dall'altro (i brim si toccano tra i due), senza disporre automaticamente; poi **Slice**.
5. Ridurre il G-code come sopra con `--layers 5 --out tests/print-advisor/fixtures/brim3-gap03-soak0.gcode` e creare `brim3-gap03-soak0.expected.json` con almeno `heat_soak_time` 10, `heat_soak_bed_temp` (il valore risolto), `brim_layers` 3, `brim_object_gap` 0, `brim_layers_object_gap` 0.3, `brim_objects` 2, `layers_in_file` 5.
6. `node tests/print-advisor/check-gcode.mjs`: sui layer 1 e 2 il bordo del brim deve risultare ≥ 0,3 mm dalle pareti del proprio cubo e ≥ 0 da quelle dell'altro (tolleranza 0,05), e nessun segmento dentro un'isola. Controllare a occhio nell'anteprima che il layer 1 tocchi i cubi e i layer 2–3 no.
