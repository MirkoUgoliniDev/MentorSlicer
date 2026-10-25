# Patches to compiled web bundles

Two device-manager pages and the Home page ship only as compiled Vue bundles, with no sources in this repository. Print Advisor needs small changes in them. When Creality Print is updated, these files are replaced, so the changes must be applied again, with the tool described below or by hand: search for the original snippet and replace it. The snippets below are shown without the `/*PA:<id>*/` markers that the tool adds.

Both snippets are minified, and the variable names (`j`, `K`, `n`, `r`, `y`, `i`…) can differ in a new build. Find the snippet by its stable strings, such as `machine_ptc_exist`, `"caseFan"`, `"print_control"` or `getModelMatch`.

## Automatic tool: `tools/web-patches/apply-web-patches.mjs`

All the patches below are encoded in `tools/web-patches/patches.mjs` and applied by a script (Node 20+, no dependencies). For each patch the script:

- finds the target file **by content**, not by name: the device page is the `.js` under `resources/web/deviceMgr` that contains `registerHandler("print_control"`; the send dialog is the one under `resources/web/sendToPrinterPage` that contains `.getModelMatch(`. The hashed file names can change, and the script still finds the file;
- finds each edit point with a regular expression anchored on stable strings (labels, command names, property keys) and captures the minified names it needs (`j`, `K`, `le`, `ze`, `DeviceInterface`, `postResult`…);
- marks the inserted code with a comment `/*PA:<id>*/` (`/*PA:6.2*/` for the second edit of a patch). The Italian labels (patch 3) have no marker: the new text itself shows that they are applied;
- applies a patch whole or not at all. An anchor that matches 0 or more than 1 time makes the patch `NOT FOUND`, and so does a name searched elsewhere in the file that is missing or found with two different values, a captured name equal to a local declared by the inserted code (it would be shadowed), or a missing required patch (7 needs 1 and 6; 8 needs 6). With `--only`, a required patch that is not run counts only if its `/*PA:…*/` markers are already in the file.

Usage, from the repository root:

```bat
node tools\web-patches\apply-web-patches.mjs --check    :: report only (default), exit 1 if a patch is NOT FOUND or AMBIGUOUS
node tools\web-patches\apply-web-patches.mjs --apply    :: apply, idempotent; exit 1 (page maybe partially patched) if a patch fails
node tools\web-patches\apply-web-patches.mjs --revert   :: restore the backup written by --apply
```

Options: `--root <dir>` works on another copy that contains `resources/web` (for example the bundles extracted from a Creality tag); `--only 4,6` limits the run to some patches (unknown ids are an error); `--backup-dir <dir>` chooses where backups go (not `<root>` and not inside `<root>/resources/web`); `--all-or-nothing` makes `--apply` write nothing unless every selected patch succeeds (by default `--apply` writes what it can and prints which pages are left PARTIALLY patched); `--accept-unmarked` accepts code found without its marker. An option without a value is an error (exit 2).

Backups: `--apply` saves the file before changing it in `tools/web-patches/.backup/` (ignored by git), or in `<root>/.web-patches-backup/` with `--root`, plus `<file>.sha256`, the hash of the file as `--apply` wrote it. The backup is written once, and renewed only when the file on disk contains none of our patches (a fresh Creality bundle). `--revert` restores the backup only if the current file still has that hash, and exits 1 when the backup or the hash is missing or the file changed; `--apply` also skips a file that changed since its last `--apply` while an older backup is kept.

Status per patch: `applied` (nothing to do), `applicable` (`--check`) or `APPLIED NOW` (`--apply`), `NOT FOUND`, `AMBIGUOUS`. `AMBIGUOUS` means the patch's code is there but without its `/*PA:…*/` marker: it may be upstream code that only looks like ours, so it is not counted as applied (exit 1, dependent patches refused) unless `--accept-unmarked` is given after checking it by hand. The bundles of this repository carry all the markers.

`build_exe_only.bat` runs `--check` after every successful build and prints a WARNING (the build does not fail) when a patch is missing.

### After a Creality update

1. Merge or copy the new `resources/web` from Creality.
2. `node tools\web-patches\apply-web-patches.mjs --check`: all patches should be `applicable`.
3. `node tools\web-patches\apply-web-patches.mjs --apply`, then check the syntax of the written bundles, for example by copying each one to a temporary `x.mjs` and running `node --check x.mjs`.
4. For each `NOT FOUND` patch: read its section below, find the new code by its stable strings, and update its `find` expression (and, if needed, `vars`) in `patches.mjs`. Do not edit the bundle by hand: fix the expression, then run `--apply` again.
5. Test the device page and the send dialog in Creality Print (chamber card, fans, labels, auto-open, preheat, snapshot, K1C selection in the send dialog, cold-chamber notice with an ASA/ABS plate).

Verified on 2026-10-19: from the original bundles of commit `59ae8cbb`, `--apply` produces the bundles of this branch, identical apart from line endings. The bundles were patched by hand before the tool existed; they were then rewritten with the tool, so they now contain the `/*PA:…*/` markers, and they are otherwise identical.

## `resources/web/deviceMgr/assets/BZCDzYbb.js` (device page)

### 1. Chamber temperature card for printers with a sensor but no heater (e.g. K1C)

In the `DetailParams` component, after the `Chamber Temp` entry that ends with `isExist:j.machine_ptc_exist===1}`, append a read-only entry:

```js
,{name:n("Chamber Temp"),type:"boxSensor",curTemp:parseFloat(j.data&&j.data.boxTemp)||0,targetTemp:0,maxTemp:0,unit:"℃",isExist:j.machine_ptc_exist!==1&&K!==1&&j.data!=null&&j.data.boxTemp!==void 0}
```

`PrintParams` sends commands only for the types `nozzle`, `bed` and `box`, so the type `boxSensor` is display-only.

Update 2026-10-22: the K1C repeats `boxTemp` only now and then, so the page data often has `maxBoxTemp` and `targetBoxTemp` but no `boxTemp`, and the card disappeared. The card now exists when `boxTemp` is present **or** `maxBoxTemp > 0`; its current temperature falls back to `advisorChamberTemp(address)`, the `chamber` field that the Print Advisor monitor adds to its `advisor_chamber` push (from the native telemetry cache). Without the Print Advisor running the fallback is 0.

### 2. Case fan card when the printer reports its speed

Change `type:"caseFan",isExist:j.support_air_filtration===1` to:

```js
type:"caseFan",isExist:j.support_air_filtration===1||(j.data!=null&&j.data.caseFanPct!==void 0)
```

### 2b. Side fan card when the printer reports its speed

Change `type:"sideFan",isExist:j.auxiliary_fan===1}` (the `Side fan` entry, not the `exhaust fan` one) to:

```js
type:"sideFan",isExist:j.auxiliary_fan===1||(j.data!=null&&j.data.auxiliaryFanPct!==void 0)}
```

### 3. Italian labels

| Key | Original | New |
|---|---|---|
| `"Nozzle Temp"` | `Temperatura dell'ugello` (truncated in the card) | `Temp ugello` |
| `"Model fan"` | `Modello di ventilatore` (truncated) | `Ventola ugello` |
| `"Case fan"` | `Ventilatore caso` (wrong translation) | `Ventola estrazione`: on the K1C, fanCase/caseFanPct is the rear exhaust fan (`M106 P1`, verified 2026-10-19) |
| `"Side fan"` | `Ventilatore laterale` | `Ventola laterale`: on the K1C, fanAuxiliary/auxiliaryFanPct is the big side fan (`M106 P2`, verified 2026-10-19) |
| `"Wind Speed"` | `Velocità del vento` (wrong translation) | `Velocità` |

### 4. `print_control` accepts `set_bed_temp` (Print Advisor `preheat_chamber`)

In `handlePrintControl`, replace:

```js
if(!["pause","resume","stop"].includes(i))throw new Error("Invalid print control action");const y=resolvePrinter(r);if(!y)throw new Error("Printer not found");await sendPrintControl(y,i),
```

with:

```js
if(!["pause","resume","stop","set_bed_temp"].includes(i))throw new Error("Invalid print control action");const y=resolvePrinter(r);if(!y)throw new Error("Printer not found");if(i==="set_bed_temp"){const t=Number(r.value),bm=parseFloat(y.data&&y.data.maxBedTemp)||parseFloat(y.temperature&&y.temperature.bed&&y.temperature.bed.max)||0,lim=bm>0?Math.min(bm,130):120;if(!(t>=0&&t<=lim))throw new Error("Invalid bed temperature (max "+lim+")");if(y.deviceType!==0||y.oldPrinter)throw new Error("Bed temperature control is available only for LAN printers");DeviceInterface.SetTargetBedTempTemp(y.address,0,t)}else await sendPrintControl(y,i);
```

`DeviceInterface.SetTargetBedTempTemp` is the same call that the bed "target" field of the page uses. The limit is the printer's `maxBedTemp` (from `data.maxBedTemp`, else `temperature.bed.max`, never above 130), or 120 °C when the printer does not report it (K1C: 100 °C).

### 6. Print Advisor helpers: chamber state and camera snapshot

Insert this block immediately before `const _hoisted_1={id:"rooClassWrapper"` (the module-level code right after `handlePrintControl`). It defines the reactive chamber state, kept per printer address (`advisorChamberStates[address]`), the text shown in each printer's chamber card, and the two MessageCenter handlers. The handlers accept the payload either at the top level or inside `data`, as an object or a JSON string. The clock of the card is one module-level 1 s interval (`advisorNowTimer`): `advisor_chamber` messages start it (only once, guarded by the variable) while a preheat or a running soak is active, and it clears itself when neither is; the render function `advisorChamberText(address)` never creates timers. It looks the state up by the card's address (exact, then by host without port, then through the LAN printer linked to a cloud entry); a message without `address` is stored under `""` and used only as a last fallback. `advisorDetailState` is set by patch 8 to the `App` detail state (`le`: `visable`, `address`).

```js
var advisorChamberStates=reactive({}),advisorDetailState=null,advisorNow=ref(Date.now()),advisorNowTimer=null;function advisorParsePayload(r){let d=r;if(typeof d=="string")try{d=JSON.parse(d)}catch{d={}}return d&&typeof d.data=="object"&&d.data!==null?d.data:d||{}}function advisorHost(a){return String(a||"").split(":")[0]}function advisorChamberHandler(r){const d=advisorParsePayload(r),num=v=>typeof v=="number"&&isFinite(v)?v:null,key=typeof d.address=="string"?d.address:"";advisorChamberStates[key]={preheat_started_ms:num(d.preheat_started_ms),preheat_bed:num(d.preheat_bed),soak_seconds:num(d.soak_seconds),print_start_s:num(d.print_start_s),file:typeof d.file=="string"&&d.file?d.file:null,state:num(d.state)},advisorNow.value=Date.now(),advisorStartTimer()}function advisorStateFor(address){const S=advisorChamberStates;if(S[address])return S[address];const h=advisorHost(address);for(const k in S)if(k&&advisorHost(k)===h)return S[k];try{const p=dataCenter.getPrinter(address);if(p&&p.identity!=null){const q=dataCenter.getPrinter(p.identity,!1,0);if(q&&S[q.address])return S[q.address]}}catch{}return S[""]||null}function advisorStateNeedsTimer(st){return!!st&&(st.preheat_started_ms!=null||!!(st.soak_seconds&&st.print_start_s!=null&&st.print_start_s+st.soak_seconds>Date.now()/1e3))}function advisorTimerNeeded(){return Object.values(advisorChamberStates).some(advisorStateNeedsTimer)}function advisorStartTimer(){advisorNowTimer||!advisorTimerNeeded()||(advisorNowTimer=setInterval(()=>{if(!advisorTimerNeeded()){clearInterval(advisorNowTimer),advisorNowTimer=null;return}advisorNow.value=Date.now()},1e3))}function advisorMmss(t){t=Math.max(0,Math.floor(t));const m=Math.floor(t/60),sec=t%60;return(m<10?"0":"")+m+":"+(sec<10?"0":"")+sec}function advisorBase(f){return String(f||"").split(/[\\/]/).pop().toLowerCase()}function advisorChamberText(address){const now=advisorNow.value,st=advisorStateFor(address);if(!st)return"—";const p=dataCenter.getPrinter(address),state=p?Number(p.state):st.state!=null?st.state:-1,printing=state===1,idle=state!==1&&state!==5&&state!==6&&state!==7;if(st.preheat_started_ms!=null&&idle)return"Preriscaldo "+advisorMmss((now-st.preheat_started_ms)/1e3);if(printing&&st.soak_seconds&&st.print_start_s!=null&&st.file&&p&&advisorBase(p.printFileName)===advisorBase(st.file)){const left=st.print_start_s+st.soak_seconds-now/1e3;if(left>0)return"Soak -"+advisorMmss(Math.ceil(left))}return"—"}async function handleAdvisorSnapshot(r={}){const d=advisorParsePayload(r),id=d&&d.request_id!=null?d.request_id:"",addr=d&&typeof d.address=="string"?d.address:"";try{const all=Array.from(document.querySelectorAll("video")).filter(x=>x.readyState>=2&&x.videoWidth>0),vis=all.filter(x=>x.offsetParent!==null),det=advisorDetailState,detAddr=det&&det.visable&&det.address?det.address:"",matched=!!addr&&!!detAddr&&advisorHost(detAddr)===advisorHost(addr),v=vis[0]||all[0];if(!v){const im=document.querySelector("img.camera-image");throw new Error(im&&im.src?"No playing camera video (MJPEG stream only)":"No playing camera video")}const sc=Math.min(1,1280/v.videoWidth),w=Math.round(v.videoWidth*sc),h=Math.round(v.videoHeight*sc),c=document.createElement("canvas");c.width=w,c.height=h,c.getContext("2d").drawImage(v,0,0,w,h);const u=c.toDataURL("image/jpeg",.8);postResult({command:"advisor_snapshot_result",request_id:id,ok:!0,data_url:u,width:w,height:h,address:detAddr||null,address_matched:matched})}catch(e){postResult({command:"advisor_snapshot_result",request_id:id,ok:!1,error:(e==null?void 0:e.message)||"snapshot failed"})}}
```

`reactive`, `ref`, `dataCenter` and `postResult` are module-level names of the bundle; `postResult` is the function that `handlePrintControl` uses (`window.wx.postMessage(JSON.stringify(r))`).

Then register the handlers next to `print_control`. Replace:

```js
MessageCenter$1.registerHandler("print_control",handlePrintControl),
```

with:

```js
MessageCenter$1.registerHandler("print_control",handlePrintControl),MessageCenter$1.registerHandler("advisor_chamber",advisorChamberHandler),MessageCenter$1.registerHandler("advisor_snapshot",handleAdvisorSnapshot),
```

Messages (C++ → page, same envelope as `PrinterMgrView::request_print_control`):

- `advisor_chamber`: `{address, state, preheat_started_ms, preheat_bed, soak_seconds, print_start_s, file}` (sent by `PrintAdvisorMonitor`); every field except `address` may be `null`. Each message replaces the whole state of that address, so send `preheat_started_ms:null` when the preheat ends. `state` is the printer state, used only when the page has no printer for that address.
- `advisor_snapshot`: `{request_id, address?}`. The page takes the first visible playing `<video>`, otherwise the first playing one. The detail page shows one printer at a time (no per-printer DOM marker), so `address_matched` is true only when `address` was given and equals the printer of the open detail view. The page answers with `{command:"advisor_snapshot_result", request_id, ok:true, data_url, width, height, address, address_matched}` (`address` = printer of the open detail view or `null`) or `{command:"advisor_snapshot_result", request_id, ok:false, error}` through `window.wx.postMessage`, which reaches `PrinterMgrView::OnScriptMessage`. Only a playing WebRTC `<video>` can be captured; an MJPEG `<img>` from another origin would taint the canvas, so in that case the error says "MJPEG stream only" (the stream URL is not reported).

### 7. Chamber card shows the Print Advisor state instead of a target field

In `PrintParams`, the `boxSensor` card (patch 1) showed a "Target" input that did nothing. Replace:

```js
createTextVNode(toDisplayString$1(j.$t("Target")),1)]),_:1})]),_:1}),
```

with:

```js
createTextVNode(toDisplayString$1(n.type==="boxSensor"?"Advisor":j.$t("Target")),1)]),_:1})]),_:1}),
```

and replace:

```js
createVNode$1(ie,{span:12},{default:withCtx(()=>[createBaseVNode("div",_hoisted_5$F,[createVNode$1(le,
```

with:

```js
createVNode$1(ie,{span:12},{default:withCtx(()=>[n.type==="boxSensor"?createBaseVNode("div",{class:"target-temp advisor-chamber",style:{"line-height":"32px","white-space":"nowrap"}},toDisplayString$1(advisorChamberText(n.address)),1):createBaseVNode("div",_hoisted_5$F,[createVNode$1(le,
```

The card shows `Preriscaldo mm:ss` (time since the preheat started, while the printer is not printing), `Soak -mm:ss` (time left, while printing the file named in the payload, basename compared case-insensitively), otherwise `—`. The current temperature is unchanged.

### 8. Open the only printer automatically

In the `App` component, replace:

```js
BackendInterface.viewDetialEvent=ze,BackendInterface.viewDeviceList=$e;
```

with:

```js
BackendInterface.viewDetialEvent=ze,BackendInterface.viewDeviceList=$e;advisorDetailState=le;{let advAutoKey="";const advAutoTimer=setInterval(()=>{if(le.visable){clearInterval(advAutoTimer);return}if(!ie.value)return;const advSeen=new Map;for(const g of de.data.printerList||[])for(const p of g&&g.list||[]){if(!p||!p.address)continue;const key=p.deviceType==1&&p.identity!=null?p.identity:p.address;advSeen.has(key)||advSeen.set(key,p)}if(advSeen.size!==1){advAutoKey="";return}const[[key,p]]=advSeen;if(advAutoKey!==key){advAutoKey=key;return}clearInterval(advAutoTimer);try{ze(p.name,p.address)}catch(err){console.log("advisor auto-open failed",err)}},1e3)}
```

Every second, the timer checks the printer list. When the list contains exactly one printer for two consecutive checks, it opens that printer's detail page with `ze(name,address)`, the same handler a click on the printer uses. A cloud entry linked to a LAN printer (`identity`) counts as the same printer. The timer stops after it opens the page or when a detail page is already open, so the page is not reopened after the user goes back to the list. Until 2026-10-22 it also stopped after 60 s, which expired before the user opened the Device tab. With 0 or several printers it keeps checking, so if the list later drops to one printer that printer opens by itself (low impact). `advisorDetailState=le` exposes the detail state to the snapshot handler of patch 6.

### 11. Wider chamber card for the Advisor state

The `PrintParams` card (`.print-params-item`, scope `data-v-9fc1b45c`) has a fixed width (`180px`, `130px` under 1380 px), and the Advisor text of patch 7 (`Preriscaldo mm:ss`, `Soak -mm:ss`, `white-space:nowrap`) overflows its half column. The CSS lives in a JS template literal of the bundle, so the inserted text must not contain a backtick or `${`. Right after the end of the `PrintParams` media query:

```css
@media screen and (min-width:80px)and (max-width:1380px){.print-params-item[data-v-9fc1b45c]{width:130px;min-width:80px}[data-v-9fc1b45c] .el-input__wrapper{padding:1px 3px!important}}
```

append (CSS only, no Vue props change, same layout above and below the breakpoint):

```css
.print-params-item[data-v-9fc1b45c]:has(.advisor-chamber){flex:1 1 320px;width:auto;min-width:320px}.print-params-item[data-v-9fc1b45c]:has(.advisor-chamber) .el-row>.el-col:first-child{flex:0 0 30%;max-width:30%}.print-params-item[data-v-9fc1b45c]:has(.advisor-chamber) .el-row>.el-col:last-child{flex:0 0 70%;max-width:70%}
```

Only the card that contains `.advisor-chamber` (the chamber card of patch 7) grows to fill the rest of the row, at least 320 px (240 px fixed until 2026-10-22), so its right edge lines up with the LED card of the fan row below: 320 − 16 px padding leaves about 212 px for the Advisor column, enough for `Preriscaldo 59:59` plus the Stop button of patch 17. The LED card moves to the fan row (patch 18) to make room. `:has()` needs Chromium 105+ (WebView2 is evergreen); on an older runtime the rule is ignored and the card keeps its old width. `.print-params-wrapper` is a non-wrapping flex row, so the other cards shrink toward their `min-width`. Requires patch 7.

### 12. Hide the "Buy filament" link

The insufficient-filament notice of the device page renders an inline link ("Buy filament", class `inline-buy-link`) that posts `buy_filament_cmd`. The native handler (`GUI_App::OpenEshopRecommendedGoods`, a cloud eshop request) was removed in cleanup phase F3, so the link would do nothing. Right after its scoped CSS rule (inside the CSS template literal: no backtick, no `${`):

```css
.inline-buy-link[data-v-cf516695]{display:inline-block;padding:0 2px;line-height:1;position:relative;vertical-align:baseline}
```

append:

```css
.inline-buy-link[data-v-cf516695]{display:none!important}
```

The notice text stays; only the link is hidden. The JS that builds the `buyLink` part and sends `buy_filament_cmd` is left in place (it is unreachable without the link).

### 17. "Preriscalda" / "Stop" button in the chamber card

User request of 2026-10-22: start the chamber preheat through the bed from the Device tab, without the MCP client. Requires patches 6 and 7.

1. Before `function advisorChamberText(address){` four helpers are inserted: `advisorPreheatVisible` (printer state 0/2/3/4, idle), `advisorPreheatActive` (the Advisor state of patch 6 has `preheat_followed`, sent by the monitor while the preheat runs or the bed waits for its automatic switch-off, or `preheat_started_ms`; and the printer is idle), `advisorPreheatClick` (posts `{"command":"advisor_preheat","address":<address>,"start":true|false}` with the page's `postResult`) and `advisorPreheatButton` (a `<button class="advisor-preheat-btn">` labelled "Preriscalda", or "Stop" on a green background while a preheat is followed; hidden with `display:none` while the printer is not idle). The button is always rendered, with patch flag 17 (FULL_PROPS | TEXT): a node that appears and disappears would change the number of tracked children of the stable slot fragment and break the card's re-render. Patch 6 also stores `preheat_followed`.
2. The Advisor cell of patch 7.2 becomes `createBaseVNode("div",{class:"target-temp advisor-chamber",...},[createTextVNode(toDisplayString$1(advisorChamberText(n.address)),1),advisorPreheatButton(n.address)])`: text plus button.

3. Chamber target (user request of 2026-10-23): `advisorPreheatTargetInput` renders a small text field (`<input class="advisor-preheat-target">`, placeholder `°C`) between the text and the button, visible while the printer is idle and no preheat is followed; always rendered with patch flag 16 (FULL_PROPS) for the same reason as the button. The typed text is kept per printer host in `advisorPreheatTargets` and in `localStorage` (`advisorPreheatTarget:<host>`), and `advisorPreheatClick` sends it as `target_chamber_temp` (string, empty = no target) with `start:true`. Patch 6 stores `preheat_chamber_target` from `advisor_chamber` (sent by the monitor while the preheat with a target runs) and the card shows `Preriscaldo mm:ss → 35°`.

The native side (`advisor_preheat_from_page`, `PrintAdvisorServer.cpp`, routed by `advisor_handle_page_message`) runs the same code as the MCP tool `preheat_chamber`: confirmation dialog, printer limits, `max_minutes` 60, automatic switch-off by the monitor. The bed temperature is the printer's maximum capped at 100 °C. The chamber target, when given, must be a number between 20 °C and the printer's chamber maximum (70 °C without one; `,` accepted as decimal separator) and above the current chamber reading, otherwise a message box explains why; the monitor ends the preheat with "Camera a temperatura" when the chamber reaches it. A message without a boolean `start` is refused. The confirmation warns when a slow cool-down of the same printer would be interrupted. It refuses with a message box when the Print Advisor local API is off, another Print Advisor request or a modal window is open, or the card's printer is not the one Print Advisor acts on. History entries carry `"source": "device_page"`.

### 18. LED card in the fan row

With the 320 px chamber card (patch 11) the four cards of the temperature row no longer fit. In the `DetailParams` render function the LED `PrintControl` (`k.value?(openBlock(),createBlock(PrintControl,{key:0,class:"led-light-control",isLightComponent:!0,name:"LED",...})):createCommentVNode("",!0)`) is cut from the end of the temperature row (`_hoisted_1$13`) and appended to the fan row (`_hoisted_2$V`) after the fan list. The edit that appends runs first, because it reads the expression from the unpatched text. Requires patch 11.

### 19. Open the only printer each time the Device tab is selected

Patch 8 opens the only printer once, at page load. Selecting the Device tab later makes the native side send `init_device`, which can bring the list back. `PrinterMgrView::on_switch_to_device_page` now also sends `{"command":"advisor_open_single"}` (PRINT-ADVISOR hook), and the page:

1. after the patch 8 timer defines `window.advisorOpenSingle`: if no detail card (`.print-params-item`) is visible and the printer list holds exactly one printer (cloud entries linked by `identity` count once), it opens it with the same `ze(name,address)` as a click;
2. registers `advisor_open_single` next to `advisor_chamber`, calling it 400, 1500 and 3000 ms later, because `init_device` refreshes the list asynchronously.

Requires patches 6 and 8.

### 20. LAN diagnosis without the internet probe to baidu.com

Found in the network audit of 2026-10-23. The "Diagnosis" button shown on an offline LAN printer first calls `cxApi.checkInternet()`, which fetched `https://www.baidu.com/` (`mode:"no-cors"`); without internet the diagnosis stopped there with "Network abnormality", although the printer is on the LAN and the check is irrelevant to it. The method now reports online at once:

```js
static async checkInternet(){if(!navigator.onLine)
```

becomes

```js
static async checkInternet(){return!0;if(!navigator.onLine)
```

The rest of the method is unreachable but valid. The LAN diagnosis goes straight to the native `diagnosis_lan_connect`; the cloud diagnosis branch (`deviceType===1`, `checkLatency` to `www.crealitycloud.*`) stays unreachable without an account. The anchor `static async checkInternet(){if(!navigator.onLine)` is unique in the bundle.

### 23. Unused Firebase SDK copies removed

The page bundles two copies of the Firebase JS SDK (11.4.0 and 11.10.0; app, installations, analytics), and nothing calls `initializeApp`: they were dead code kept only because of their side effects at load time (component registration). Each copy is removed whole, from the license comment of `@firebase/util` in front of `const stringToByteArray…=function(…){const …=[];let …=0;for(let ` to the call that ends `@firebase/analytics`, `….create("interop-component-reg-failed",{reason:…})}}}registerAnalytics();` (the shared expression `FIREBASE_SDK` of `patches.mjs`; the names are minified in some pages): the first edit removes the copy followed by another copy, the second one the copy that is left (cleanup of 2026-10-23).

Checked with a scope analysis (acorn + eslint-scope): no code outside the copies references any of their 231 top-level names, the bundle still parses, and the only globals it no longer uses are `indexedDB`, the `IDB*` classes, `DOMException`, `BroadcastChannel`, `chrome` and `browser`. About 134 KB less.

### 26. No "log in to bind the device" banner

Without the Creality login (F4) the user token is always empty, so the page always showed the `LoginView` banner ("Accedi per collegare il dispositivo…"); its button switched to the Home and nothing happened (cleanup of 2026-10-24). The condition of the banner becomes false:

```js
unref(sysStore)().token.length===0?(openBlock(),createBlock(LoginView,{key:3})):createCommentVNode("",!0)
```

becomes

```js
!1?(openBlock(),createBlock(LoginView,{key:3})):createCommentVNode("",!0)
```

The anchor `unref(sysStore)().token.length===0?(openBlock(),createBlock(LoginView,` is unique in the bundle. The `LoginView` component stays (unused).

## `resources/web/sendToPrinterPage/assets/BDAlyC64.js` (send dialog)

### 5. Model match when the downloaded model list lacks the printer model

Alpha builds download `system/Creality/machineList.json` from a development cloud that lists only a few models. A LAN printer missing from that list, such as a K1C, was then reported as "not compatible with the print preset". In the function that calls `n.getModelMatch(h,u,t.printerModel,p)`, replace:

```js
else Object.getOwnPropertyNames(t.modelMap).length>0&&(e.isModelMatch=1)
```

with a comparison by printer model, handled by the C++ `get_model_match` fallback:

```js
else if(!t.presetName)Object.getOwnPropertyNames(t.modelMap).length>0&&(e.isModelMatch=1);else{const mm=String(e.selectDevice.model||"");n.getModelMatch(t.presetName,"",t.printerModel,mm.startsWith("Creality")||mm.startsWith("SPARKX")?mm:"Creality "+mm)}
```

With an empty `presetName` the original behaviour is kept (`isModelMatch=1`, a warning): the fallback must not turn an unknown preset into a match.

### 9. Select the only online printer when the model list lacks its model

The automatic selection in the `n.devices` watcher matches devices through `n.modelMap`, which lacks the K1C in Alpha builds, so "My device" stayed on "Select". Replace:

```js
break}}!l.selectDevice&&U&&(l.selectDevice=U,l.selectDeviceAddress=w,m("select",U));
```

with:

```js
break}}if(!l.selectDevice&&!U){const advOn=new Map;for(const D of z){if(!D||!D.online)continue;const k=D.mac||D.address,P=advOn.get(k);(!P||P.deviceType!==0&&D.deviceType===0)&&advOn.set(k,D)}advOn.size===1&&(U=[...advOn.values()][0],w=U.address,l.currentDeviceAddress=w)}!l.selectDevice&&U&&(l.selectDevice=U,l.selectDeviceAddress=w,m("select",U));
```

When the model match finds nothing and exactly one printer is online, that printer is selected. The same printer seen over both LAN and cloud (same MAC) counts once, and the LAN entry is preferred.

### 10. Cold-chamber notice for ASA/ABS

A warning only, shown above the "Start Print" / "Send Only" row of the single-printer send dialog (`PrintFile` component) when the selected plate uses an ASA or ABS filament and the chamber of the selected LAN printer is below 30 °C. The buttons stay enabled. Five edits:

1. In the `update_plate_data` handler, after `s.setFilamentTypes(T),` (where `_` is the parsed payload and `s` the Pinia store), append:

   ```js
   s.advisorChamberTemps=_.advisor_chamber_temps||{},
   ```

   `advisor_chamber_temps` is `{ "<host>": <boxTemp> }`, added by C++ (`advisor_append_send_page_data`, `PrintAdvisorPageBridge.cpp`, hooked in `SendToPrinter.cpp`) from `DeviceTelemetryCache`: only printers connected through the Device tab, and only values reported on the current connection. It is sent once per `update_plate_data` (dialog open / plate data refresh), so a chamber warming up while the dialog stays open is not followed.

2. In the hoisted-props chain of `PrintFile`, after `Jie={class:"bottom-container"},Qie={class:"cmd-btn"},`, declare the helper:

   ```js
   advisorColdChamber=(st,store)=>{const dev=st&&st.selectDevice,pl=st&&st.selectPlate;if(!dev||!pl||!dev.online||dev.deviceType!==0)return"";const types=Array.isArray(store&&store.filamentTypes)?store.filamentTypes:[],ex=Array.isArray(pl.extruderIndexs)?pl.extruderIndexs:[];if(!ex.some(k=>/^(ASA|ABS)/i.test(String(types[k-1]||""))))return"";const temps=store.advisorChamberTemps||{},addr=String(dev.address||""),host=addr.split("(")[0].split(":")[0];let tmp=temps[addr];typeof tmp=="number"||(tmp=temps[host]);return typeof tmp=="number"&&isFinite(tmp)&&tmp<30?String(Math.round(tmp)):""},
   ```

   It returns the rounded chamber temperature as a string when the notice must be shown, else `""`. `selectPlate.extruderIndexs` are the 1-based `plate_extruders` of the plate; `store.filamentTypes` are the `filament_types` per extruder; a type that starts with `ASA` or `ABS` (`ASA-CF`, `ABS-GF`…) counts. Only online LAN printers (`deviceType===0`), looked up by full address, then by host (cloud entries are `ip(id)`).

3. In the render function of `PrintFile`, before `O("div",Qie,[H(q,{class:"start-print"` (the button row; `O`, `$`, `P`, `ne`, `d`, `a`, `te`, `i`, `t` are captured from the "not compatible with the print preset" warning just before), insert the sibling:

   ```js
   advisorColdChamber(i,d(t))?(P(),$("div",{class:"advisor-cold-chamber",style:{color:"#ff7d00","font-size":"12px","line-height":"16px",margin:"6px 15px 0","white-space":"normal"}},ne(d(a)("Advisor cold chamber",{temp:advisorColdChamber(i,d(t))})),1)):te("",!0),
   ```

4. and 5. Texts, added to the vue-i18n tables of the page (no `/*PA*/` marker, like the labels of patch 3): after `en_GB:{All:"All","Start Print":"Start Print",` the key `"Advisor cold chamber":"Cold chamber ({temp} °C): for ASA/ABS it is better to preheat before starting (Print Advisor: preheat_chamber)."`, and after `it_IT:{All:"Tutti","Start Print":"Avvia stampa",` the Italian `"Camera fredda ({temp} °C): per ASA/ABS conviene preriscaldare prima di avviare (Print Advisor: preheat_chamber)."`. The other languages fall back to `en_GB`.

The multi-printer send component (the one with the "Wait … Minute each batch" options) is not patched.

### 12b. Hide the "Buy filament" link

Same as patch 12, for the send dialog (scope `data-v-786de3ae`): the insufficient-filament notice keeps its text, the inline "Buy filament" link is hidden with `display:none!important`, because `buy_filament_cmd` has no native handler since F3.

### 13. No Creality Cloud token check without an account

The `get_user` handler of the send dialog stores the user and then calls `checkTokenIsValid()`, which posts to `api-dev.crealitycloud.cn` (`getDeviceCount`) even with an empty token. Since F4 the application has no account and always answers `get_user` with an empty user, so the call is made only when a token exists (never, in this build):

```js
a.setToken(v||""),a.checkTokenIsValid(),
```

becomes

```js
a.setToken(v||""),v&&a.checkTokenIsValid(),
```

`v` is the token from `const{region:g,userId:f,token:v}=h||{}` and `a` the user store; the anchor is the only `setToken(...||""),...checkTokenIsValid(),` pair in the file. The device page needs no patch: it returns before `checkTokenIsValid` when `userId` is empty.

### 24. Unused Firebase SDK removed

Same as patch 23, for the one SDK copy (11.4.0) of the send dialog, never initialized either: the copy is removed whole, from the license comment of `@firebase/util` in front of `const stringToByteArray…=function(…){const …=[];let …=0;for(let ` to the call that ends `@firebase/analytics`, `….create("interop-component-reg-failed",{reason:…})}}}registerAnalytics();` (the shared expression `FIREBASE_SDK` of `patches.mjs`; the names are minified in some pages). Same checks as patch 23; about 58 KB less.

## `resources/web/homepage/assets/index-806b308b.js` (Home tab, "Add printer" and "Add filament" windows)

The same bundle serves the Home tab (`#/Community/Home Page`, `WebViewDialog.cpp`) and the two Guide routes of `GuideFrame` (`#/Guide/addPrinter`, `#/Guide/createFilament`, `WebGuideDialog.cpp`), so it stays even though the fork keeps only "Projects" in the Home. The script finds it as the `.js` under `resources/web/homepage` that contains `initCxAnalytics(`.

### 14. No Firebase/Google Analytics, no Creality tracking, no internet probe

Five edits, all in the same bundle (cleanup phase F7; the fifth added on 2026-10-24):

1. `initFirebase(U){U?initFirebase_dev():initFirebase()}` becomes `initFirebase(U){}`: the root `App` component calls it for every route when `cp.privacy` is set, which started Firebase and Google Analytics.
2. `initCxAnalytics(U){initDataTracking(U)}` becomes `initCxAnalytics(U){}`: the `HomePage` component started Creality's own tracking (`devdata.cxswyjy.com`, or a test server for Alpha/Beta builds). `sendTrackingEvent` already returns while the tracker is not initialised.
3. `sendGAEvent=(C,U)=>{` gets `return;` as first statement, so no GA event is built (the code after it is unreachable but valid).
4. `function checkInternet(C){return Xn(this,null,function*(){` gets `return!1;` as first statement: the page reports itself offline without fetching `https://api.crealitycloud.com/` (or `.cn`). The online sections of the Home show their offline state; recent projects come from C++ and still work.
5. `sendTrackingEvent=(C,U)=>Xn(null,null,function*(){` gets `return;` as first statement: Creality's event sender posts nothing even if the tracker were initialised (defence in depth for edit 2; its 12 callers are unchanged).

### 15. Only "Projects" in the side menu

The Community layout (`_sfc_main$7`, scope `data-v-f36bccec`) is reduced to the "Home Page" item (recent projects, new/open project). Requires patch 14.

1. The login/account block `createVNode(Account,{style:{marginBottom:"10px"}}),` at the top of the menu is removed.
2. The menu items from "Online Params" to "Feedback" (Online Params, Guide Forum, the "Personal Center" sub-menu with My Models/Presets/Downloads, Feedback) are removed: the anchor spans from `,(openBlock(),createBlock(de,{key:Z["Online Params"]}` to the end of the Feedback item `toDisplayString$1(ae.$t("Feedback")),9,_hoisted_25$1)])]),_:1}))` (2,106 characters), and the remaining array `[HomePageItem]` stays valid (`]),_:1},8,["selectedKeys","onClick"])` follows).
3. The bottom "Creality Mall" link `createBaseVNode("div",{class:"menu-item",onClick:` gets `style:{display:"none"}`.

The content panes (`OnlineParams`, `GuideForum`, `MyModels`...) stay in the bundle but cannot be selected from the menu; the router still redirects unknown routes to `/Community/Home Page`. The two Guide routes are untouched; the first-run "BeginGuide" flow is shortened by patch 21.

### 16. No official printer list from the Creality cloud

Found with the network monitor on the lean build (2026-10-22): at start-up the `OnlineParams` pane (still in the bundle after patch 15, and mounted by the Community layout) posted `/api/cxy/v2/slice/profile/official/printerList` to `admin-pre.crealitycloud.com` (Alpha build; `api.crealitycloud.*` for Release) and kept the connection open. `function getParameterModelList(){return index$a({url:"/api/cxy/v2/slice/profile/official/printerList",...})}` now returns `Promise.resolve({printerList:[],series:[]})`, so the pane builds an empty table without any request. Requires patch 15.

### 21. First-run guide without the software-mode and login-region steps

The first-run guide (`BeginGuide`, `_sfc_main$8`, scope `data-v-af276cbd`) is shown by the Community layout while C++ answers `get_is_first_install` with `deviceAddEnd` = `"0"`. It had three steps: `CHOOSE_VERSION` ("Selezionare la modalità software", only "Edizione professionale" since easy mode was removed in F7), `CHOOSE_AREA` ("Seleziona la tua area login": it sent `set_user_login_area` and then `open_login_dialog`, which does nothing since the login was removed in F4, so the step never moved on) and `CONNECT_PRINTER` (connect a printer via LAN or add a printer preset). Three edits (cleanup of 2026-10-23):

1. `Q=ref(U.CHOOSE_VERSION),` becomes `Q=ref(U.CONNECT_PRINTER),`: the guide opens on the last step.
2. `[1,2,3].includes(_e==null?void 0:_e.step)` becomes `[3].includes(...)`: a step 1 or 2 saved in `sessionStorage` (`community_begin_guide_state`) by an older page is not restored.
3. The "Back" button condition `Q.value!==U.CHOOSE_VERSION?(openBlock(),createElementBlock("div",` becomes `!1?(...`: the button only led back to the removed steps.

Nothing the two steps stored is lost: `set_begin_guide_version_mode` has had no C++ handler since F7 (only the professional mode exists), and the `region` key of `MentorSlicer.conf` keeps its value (an empty region is already handled by `Preferences.cpp`, `WebViewDialog.cpp` and `AppConfig::get_country_code`, which returns `"Others"`); it can still be changed in Preferences. The `ChooseVersion` and `ChooseArea` components stay in the bundle, unused.

### 22. Firebase SDK and GA event code removed

After patch 14 Firebase is never started, but the bundle still contained the SDK and the Creality Firebase configurations (production and dev API keys, flagged by GitHub secret scanning). Three edits (cleanup of 2026-10-23), requires patch 14:

1. The SDK, from the license comment of `@firebase/util` in front of `const stringToByteArray…=function(…){const …=[];let …=0;for(let ` to the call that ends `@firebase/analytics`, `….create("interop-component-reg-failed",{reason:…})}}}registerAnalytics();` (the shared expression `FIREBASE_SDK` of `patches.mjs`; the names are minified in some pages), is removed together with the `getDefaultsFromPostinstall` stub in front of it and `let isDev=!1;` after it.
2. `const initFirebase=()=>{…},initFirebase_dev=()=>{…},getAppVersion=` becomes `const getAppVersion=`: the two functions held the configurations and called `initializeApp`/`getAnalytics`.
3. `,enrichEventData=C=>{…},sendGAEvent=(C,U)=>{return;…}` becomes `,sendGAEvent=(C,U)=>{}`: `enrichEventData` was used only by `sendGAEvent`, and `sendGAEvent` stays as an empty function for its 12 callers. The marker of 14c stays in it, so patch 14 still counts as applied.

`getAppVersion`, `getClientId`, `getOperatingSystemFromUrl` and `getUserId` stay: Creality's tracking (`initDataTracking`, off since patch 14) uses them. The method `initFirebase(U){}` of the C++ bridge (patch 14) and the `FIREBASE_APPLE` login type stay. Checked as patch 23: of the 252 top-level names of the SDK only `initializeApp`, `getAnalytics` and `logEvent` were used outside it, all in the removed code; no new unresolved name. About 83 KB less.

### 25. Alibaba OSS SDK removed

The bundle still contained the browser build of the Alibaba Cloud OSS SDK (`ali-oss`, with its own core-js polyfills), used by the removed cloud upload of 3MF/G-code (F4) and never called. It is a CommonJS module wrapped by Rollup:

```js
var aliyunOssSdk={exports:{}};(function(C,U){…})(aliyunOssSdk);
```

The whole statement pair is removed (cleanup of 2026-10-24). `aliyunOssSdk` is its only top-level name and appears nowhere else (the patch refuses to apply if another reference exists). Checked as patch 23: no code outside it references a name it declares, the bundle still parses, no new unresolved name; it no longer uses globals such as `Deno`, `WritableStream`, `SharedArrayBuffer`, `WeakRef` or `Atomics`. The global `regeneratorRuntime` it also installed is set by another polyfill of the bundle and read by nobody outside the SDK. `commonjsRequire` in front of it stays: other modules use it. About 730 KB less.

### 27. First-run screen: "Welcome to MentorSlicer", no cloud subtitle

The first-run screen of the Home (shown with an empty data folder) said "Welcome to Creality Print" and "Add printers and manage print jobs from the cloud", in each of its 24 languages (found while testing the 1.0.1 release package, 2026-10-25). The two render calls are patched instead of the 24 translations:

```js
toDisplayString$1(U.$t("Welcome to Creality Print"))
toDisplayString$1(U.$t("Add printers and manage print jobs from the cloud"))
```

become

```js
toDisplayString$1(/*PA:27*/U.$t("Welcome to Creality Print").replace(/Creality Print/g,"MentorSlicer"))
toDisplayString$1(/*PA:27b*/"")
```

The title keeps its translation with the app name swapped (every translation keeps the literal "Creality Print"); the subtitle is empty because MentorSlicer has no cloud. Each `$t("…")` call is unique in the bundle.

## Related native code

These C++ changes are part of the branch and need no manual work:

- `print_manage/App/DeviceTelemetryCache.*`, hooked into `WebSocketProxySession.cpp`, keeps the last LAN telemetry for Print Advisor.
- `print_control` (pause/resume/stop) and the preheat start (`set_bed_temp`, patch 4) go through the page with `PrinterMgrView::request_print_control`.
- Device controls (`/v1/device_control`: fans, temperatures, speed, light), the exhaust guard, the slow cooldown and the preheat automatic bed-off are written by C++ directly to the printer WebSocket with `WebSocketProxy::SendToDevice` (the proxy session of the Device tab), not through the page.
- `PrintAdvisorMonitor` pushes `advisor_chamber` and `PrintAdvisorCamera` sends `advisor_snapshot` through `advisor_push_device_page` (`PrinterMgrView.cpp`).
- The three `load_machine_preset_data()` (`GUI_App.cpp`, `PrinterMgrView.cpp`, `SendToPrinter.cpp`) pass the printer list of `machineList.json` through `localize_printer_list_images` (`PrinterCoverRoute.hpp`) before sending it as `get_machine_list`: each `thumbnail` (`pic-cdn.creality.com`) becomes the local cover route of `resources/profiles/Creality/<model>_cover.png` (empty while the local HTTP server is not started) and `zipUrl` (`file-cdn.creality.com`) is removed, so the device page and the send dialog no longer load printer pictures from the Creality CDN.
- `advisor_append_send_page_data` (`PrintAdvisorPageBridge.cpp`, called from the two plate-data functions of `SendToPrinter.cpp`) adds `advisor_chamber_temps` to `update_plate_data` for patch 10.
- The `print_control_result` that the page posts after `print_control` (patch 4) is handled by `PrinterMgrView::OnScriptMessage`, which passes the whole message to `advisor_on_print_control_result` (`PrintAdvisorInternal.cpp`).
- The local HTTP server (`HttpServer.cpp`) serves only files inside `resources/web` (and `resources` for `/resources/...`), without `Access-Control-Allow-Origin`; `/proxy` (used by the pages for the `protocal.csp` API of older Creality WiFi printers) accepts only private LAN IPv4 hosts and `/protocal.csp` paths, and answers `Access-Control-Allow-Origin: null` only to the `file://` pages (request `Origin: null`).
- Every request to the local HTTP server must carry `Host: localhost:<port>` or `Host: 127.0.0.1:<port>` (403 otherwise: DNS rebinding guard). `/proxy` also needs the per-process session token as its first parameter (`/proxy?pa_t=<token>&host=...&path=...`, 403 otherwise); the server strips it before forwarding. The pages do not know the token: `HttpServer::proxy_token_user_script()` is added with `AddUserScript` to the Device page WebView (`PrinterMgrView`) and the Home WebView (`WebViewPanel`); it wraps `XMLHttpRequest.prototype.open` and `fetch` and inserts the token into every `/proxy?` URL of the local server (all call sites of `BZCDzYbb.js` use axios, the "Connect via IP" dialog of `index-806b308b.js` uses `fetch`). `/videostream` and `/rtspvideostream` accept only a private LAN IPv4 in `ip`. After a Creality update, check (search for `/proxy?`) that the new bundles still call `/proxy` only with XHR/fetch and only from those WebViews.
