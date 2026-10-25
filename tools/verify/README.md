# Liste dei nomi rimossi: `tools/verify/removed-<fase>.txt`

Una lista per fase (`removed-F1.txt`, `removed-F2.txt`, ...), scritta all'inizio della fase dalla sua
specifica (`doc/specs/<fase>-*.md`), letta da `tools/verify-phase.mjs --phase <fase>` (passo 2).
Se manca: SKIP per F0, FAIL per F1..F8. Codifica UTF-8, una voce per riga, `#` inizia un commento.

```
# F1 - privacy and network. One entry per line; "#" starts a comment.
word:AnalyticsDataUploadManager          # whole-word, fixed string (git grep -w -F)
word:check_new_version
re:creality_task_id_pending              # extended regex (git grep -E)
file:src/slic3r/GUI/AppUpdater.cpp       # must not exist
file:tools/updater
allow:src/slic3r/GUI/AnalyticsDataUploadManager.hpp   # path where matches are still allowed (stub kept on purpose)
```

| prefisso | controllo |
|---|---|
| `word:` | `git grep -n -I --untracked -w -F -e <Nome>`: parola intera, stringa fissa. Esito atteso: nessuna corrispondenza (`git grep` esce 1). |
| `re:` | come sopra con `-E` (regex estesa) senza `-w -F`. |
| `file:` | il percorso (relativo alla radice del repository) non deve più esistere nell'albero di lavoro. |
| `allow:` | percorso escluso dalla ricerca (`:(exclude)<path>`) per tutte le voci `word:`/`re:` della lista: usato per uno stub tenuto apposta. |

Le ricerche escludono sempre `doc/**`, tutti i file `.md` (anche nella radice), `tools/verify/**`, `tests/regression/**` e i
cataloghi `localization/i18n/*.po`/`*.pot` (le stringhe tradotte restano finché non si rigenerano).
Una corrispondenza è FAIL con le prime 20 righe `file:riga: testo`.

Nota per F1: la classe `AnalyticsDataUploadManager` resta come no-op, quindi il nome della classe va
in `allow:` sui suoi file e si mettono in `word:` solo i membri rimossi (`send_print_send_event`,
`uploadSlice822ClickEvent`, ...).
