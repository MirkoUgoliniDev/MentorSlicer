# Revisione avversariale e correzioni del 2026-10-20

Branch `advisor`, partendo dal commit `32f5ea42`. Le modifiche **non sono ancora committate**.

## Come è stata fatta

1. **Prima analisi**: cinque agenti avversariali (API locale, telemetria e comandi alla stampante, motore e interfaccia, sidecar con patch web e script, controllo trasversale) hanno letto tutto il codice del fork (`59ae8cbb..32f5ea42`). Risultato: nessun problema critico, 4 gravi, 13 medi, circa 30 minori.
2. **Correzioni**: quattro agenti su file separati. I raccordi tra i file, CLAUDE.md e la build li ha fatti il coordinatore.
3. **Seconda analisi**: quattro agenti nuovi hanno rivisto solo le correzioni. Risultato: nessun problema critico o grave, 11 medi e circa 25 minori, tutti corretti.
4. **Verifica**: build MSVC riuscita senza avvisi nei file del Print Advisor, controllo dei tipi del sidecar, smoke test, prove avversariali dal vivo e test del G-code.

## Correzioni principali

### Gravi

| Problema | Correzione |
|---|---|
| Il server serviva una richiesta alla volta: una seconda modifica restava in coda invece di ricevere 409, le letture scadevano durante una finestra di conferma, e una modifica già data per fallita dal client poteva essere applicata dopo | Un thread per connessione (massimo 8, oltre HTTP 503 che il sidecar ritenta da solo). Una richiesta il cui client si è disconnesso viene annullata e la sua finestra non applica nulla. Le letture funzionano durante le finestre di conferma e vengono rimandate mentre una modifica è a metà (per esempio un progetto che si carica) |
| Un preriscaldo su una seconda stampante faceva dimenticare lo spegnimento automatico del piatto della prima | Il secondo preriscaldo viene rifiutato (409) prima e dopo la finestra, finché il primo è seguito |
| La chiusura forzata (build e "Pubblica stabile") saltava lo spegnimento del piatto | Nuovo `tools/close-build-instance.ps1`: legge lo stato del monitor e si ferma se c'è un preriscaldo o un raffreddamento in corso. Altrimenti chiude normalmente e forza solo dopo 20 s |
| `brim_layers_object_gap` non aveva effetto tra 0,1 e 0,3 mm | Il taglio tiene conto della larghezza della linea: il **bordo** del brim resta alla distanza chiesta. Con il valore predefinito il G-code è identico a prima |

### Medi

- **Sicurezza dei profili**
  - `remove` di `set_object_settings` segue la stessa lista delle chiavi ammesse, quindi l'estrusore di un oggetto non si può più cambiare.
  - `material_flow_dependent_temperature` e il suo grafico non sono più scrivibili.
- **Piatto**
  - Con la telemetria persa, lo spegnimento resta in sospeso per 12 h e viene inviato quando la stampante torna inattiva.
  - Un raffreddamento lento interrotto invia la temperatura finale, mai più alta, anche per più stampanti.
  - Un caricamento in corso rimanda lo spegnimento al massimo di 15 min.
  - Il raffreddamento lento non può più partire sopra la temperatura attuale: prima ammetteva fino a +5 °C.
- **Robustezza**
  - Un testo UTF-8 non valido non chiude più l'app.
  - Il G-code non ASCII nel contesto non rompe più la lettura.
  - Le modifiche vengono rifiutate mentre è aperta la finestra di invio alla stampante.
- **Esito di fine stampa**
  - Le note lunghe sono limitate a 500 byte.
  - La domanda resta in sospeso se la registrazione fallisce e viene scartata se il diario non esiste più.
  - L'archivio si crea solo se l'esito è stato scritto.
  - Non vengono più scritti esiti doppi: se l'assistente ha già registrato un esito, l'utente viene avvisato.
- **Seconda istanza**: non avvia il server solo se l'altra risponde davvero sulla sua porta, e l'impostazione condivisa non cambia.
- **Camera delle K2**: le opzioni camera sono di nuovo attive (sono disattivate solo per la famiglia K1). `chamber_temp` è ammesso solo sulle stampanti con controllo camera o sulla famiglia K2.
- **Sidecar**
  - I dati delle stampe ASA sulla K1C sono corretti: distacco senza colla, riuscita con la colla.
  - La guardia dell'estrazione è descritta come inefficace sulla K1C.
  - Ora riporta `address_matched` della foto.
  - Il timeout di `set_bed_type` non viene più trattato come una lettura.
  - Il riconoscimento dei materiali che si ritirano è stato allargato (ABS+, ASA-CF, PA6-CF e simili).
- **Script**
  - `publish-stable` controlla i file sorgente prima di chiudere qualsiasi cosa.
  - robocopy usa `/R:2 /W:2`.
  - `stable-version.txt` resta INCOMPLETA finché la copia non è finita.
  - Lo script di build con avanzamento passa la versione 7.3.0.0.

### Minori (selezione)

- Nomi di archivio e diario con `..` o punti finali; nomi riservati di Windows.
- Copie temporanee dei progetti riaperti eliminate dopo 1 giorno, mai quella aperta.
- Percorsi assoluti non più inviati all'LLM.
- Il pulsante dell'avviso di aggiornamento apre la pagina dei rilasci, mai l'installer.
- Validazione UTF-8 completa.
- Età della telemetria misurata con un orologio monotono.
- Avviso nella pagina di invio solo con `boxTemp` recente.
- Guardia dell'estrazione rallentata quando il firmware la ignora.
- Torre di temperatura controllata sull'altezza stampabile.
- Disposizione e orientamento rispondono 409 se un altro lavoro è in corso.
- Traduzioni italiane per le nuove opzioni del motore e i nuovi messaggi.
- Breakpad non viene più spento per sempre.
- Ordine stabile delle opzioni per oggetto.
- Il test del G-code calcola il soak come il motore e misura la distanza del brim.

## Verifiche eseguite

| Prova | Esito |
|---|---|
| Build `build_exe_only.bat` | OK, nessun avviso nei file del Print Advisor |
| `tsc --noEmit` del sidecar | OK |
| Smoke test con l'app aperta / senza app | 13 ok / 7 ok |
| 20 richieste parallele dal sidecar | 20 riuscite (i 503 vengono ritentati) |
| Client lento (slowloris) | chiuso dal server dopo 2 s, nel frattempo le altre letture rispondono |
| Corpo con UTF-8 non valido | nessun crash |
| Token sbagliato | 401 |
| Test del G-code | 20 ok |

## Cosa resta da provare

- **Modifiche dal vivo**: non le ho provate, perché nell'app era aperta la finestra "Ripristina progetto". Il 409 ricevuto è il comportamento corretto, ma la scelta di ripristinare o no spetta all'utente. Da provare con l'app libera: anteprima e applicazione di un profilo, `set_object_settings` con `remove` di `extruder` (deve essere rifiutato), `record_print_verdict` doppio.
- **Con la stampante**: il preriscaldo su due stampanti, lo spegnimento dopo una perdita di telemetria e il raffreddamento lento interrotto.
- **Nuovo fixture del G-code**: con `brim_layers_object_gap` = 0,3, due oggetti vicini e `heat_soak_bed_temp` = 0. La procedura è in `tests/print-advisor/README.md`.
