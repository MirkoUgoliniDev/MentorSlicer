// Print Advisor patches to the compiled Creality web bundles.
//
// One entry per patch of doc/MCP/web-bundle-patches.md. The bundles are
// minified and their local names (j, K, n, le, ze...) change at every Creality
// release, so every edit is anchored on STABLE strings (UI labels, command
// names, JSON/property keys) and captures the minified names it needs.
//
// Patch fields:
//   id, title       id = section number in web-bundle-patches.md
//   dir             page folder, relative to the root (resources/web/...)
//   fileContains    stable string that identifies the target .js by content
//   requires        ids of patches whose code this one uses
//   edits[]         applied in order, all or nothing per patch:
//     marker        string written into the inserted code (/*PA:<id>*/);
//                   null for label-only edits, detected by `applied`
//     applied       RegExp: the edit is already present (marker or the code
//                   written by hand before the tool existed)
//     find          RegExp: must match exactly once in the unpatched file
//     vars(text, m) optional: more captured names, searched elsewhere in the file
//     locals        names the inserted code declares (consts, lets, params);
//                   a captured name equal to one of them would be shadowed,
//                   so the edit is refused. Either an array (checked against
//                   every captured name) or { capturedName: [names declared
//                   in the scopes where that captured name is used] }.
//     replace(m, v) returns the text that replaces m[0]; v = m.groups + vars
//
// Templates use @@name@@ placeholders, filled from v. String.raw keeps the
// backslashes of the inserted code exactly as they must appear in the bundle.

const DEVICE = "resources/web/deviceMgr";
const SEND = "resources/web/sendToPrinterPage";
const HOME = "resources/web/homepage";
const ID = String.raw`[\w$]+`;

// A bundled Firebase JS SDK (patches 22-24): from the license comment of
// @firebase/util in front of stringToByteArray to the registerAnalytics() call
// at the end of @firebase/analytics. The names are minified in some pages.
const FIREBASE_SDK_START = String.raw`\/\*\*\r?\n \* @license\r?\n \* Copyright 2017 Google LLC(?:(?!\*\/)[\s\S])*\*\/const ${ID}=function\(${ID}\)\{const ${ID}=\[\];let ${ID}=0;for\(let `;
const FIREBASE_SDK = String.raw`${FIREBASE_SDK_START}[\s\S]*?\.create\("interop-component-reg-failed",\{reason:${ID}\}\)\}\}\}${ID}\(\);`;

export function fill(template, v) {
  return template.replace(/@@(\w+)@@/g, (_, k) => {
    if (v[k] === undefined) throw new Error(`template variable @@${k}@@ not captured`);
    return v[k];
  });
}

// Every match of `re` in `text` must capture the same name in group 1 (or the
// named group); no match, or two different names, refuses the edit.
export function need(text, re, what, group = 1) {
  const g = new RegExp(re.source, re.flags.includes("g") ? re.flags : re.flags + "g");
  const names = new Set();
  for (const m of text.matchAll(g)) names.add(typeof group === "string" ? m.groups[group] : m[group]);
  if (!names.size) throw new Error(`cannot find ${what}`);
  if (names.size > 1) throw new Error(`${what} is ambiguous (${[...names].slice(0, 5).join(", ")})`);
  return [...names][0];
}

// Last match of the global regex `re` that starts before `pos`.
export function lastBefore(text, re, pos, what) {
  let last = null;
  for (const m of text.slice(0, pos).matchAll(re)) last = m;
  if (!last) throw new Error(`cannot find ${what} before the anchor`);
  return last;
}

const label = (key, from, to) => ({
  marker: null,
  applied: new RegExp(`"${key}":"${to}"`),
  find: new RegExp(`"${key}":"${from}"`, "g"),
  replace: () => `"${key}":"${to}"`,
});

export const patches = [
  // 1 ---------------------------------------------------------------------
  {
    id: "1",
    title: "Chamber sensor card (sensor, no heater: K1C)",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    edits: [
      {
        marker: "/*PA:1*/",
        applied: /\/\*PA:1\*\/|type:"boxSensor",curTemp:/,
        // the card needs advisorChamberTemp (patch 6) and the "chamber" field of the native push
        find: new RegExp(
          String.raw`\{name:(?<n>${ID})\("Chamber Temp"\),type:"box",[^{}]*?maxTemp:(?<K>${ID})===1\?[^{}]*?isExist:(?<j>${ID})\.machine_ptc_exist===1\}`,
          "g"),
        replace: (m, v) => m[0] + fill(String.raw`,/*PA:1*/{name:@@n@@("Chamber Temp"),type:"boxSensor",curTemp:@@j@@.data&&@@j@@.data.boxTemp!==void 0?parseFloat(@@j@@.data.boxTemp)||0:advisorChamberTemp(@@j@@.address),targetTemp:0,maxTemp:0,unit:"℃",isExist:@@j@@.machine_ptc_exist!==1&&@@K@@!==1&&@@j@@.data!=null&&(@@j@@.data.boxTemp!==void 0||Number(@@j@@.data.maxBoxTemp)>0)}`, v),
      },
    ],
  },
  // 2 ---------------------------------------------------------------------
  {
    id: "2",
    title: "Case fan card when the printer reports caseFanPct",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    edits: [
      {
        marker: "/*PA:2*/",
        applied: /\/\*PA:2\*\/|\.data\.caseFanPct!==void 0\)/,
        find: new RegExp(String.raw`type:"caseFan",isExist:(?<j>${ID})\.support_air_filtration===1(?=\})`, "g"),
        replace: (m, v) => m[0] + fill(String.raw`/*PA:2*/||(@@j@@.data!=null&&@@j@@.data.caseFanPct!==void 0)`, v),
      },
    ],
  },
  // 2b --------------------------------------------------------------------
  {
    id: "2b",
    title: "Side fan card when the printer reports auxiliaryFanPct",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    edits: [
      {
        marker: "/*PA:2b*/",
        applied: /\/\*PA:2b\*\/|\.data\.auxiliaryFanPct!==void 0\)/,
        find: new RegExp(String.raw`type:"sideFan",isExist:(?<j>${ID})\.auxiliary_fan===1(?=\})`, "g"),
        replace: (m, v) => m[0] + fill(String.raw`/*PA:2b*/||(@@j@@.data!=null&&@@j@@.data.auxiliaryFanPct!==void 0)`, v),
      },
    ],
  },
  // 3 ---------------------------------------------------------------------
  {
    id: "3",
    title: "Italian labels of the device cards",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    edits: [
      label("Nozzle Temp", "Temperatura dell'ugello", "Temp ugello"),
      label("Model fan", "Modello di ventilatore", "Ventola ugello"),
      label("Case fan", "Ventilatore caso", "Ventola estrazione"),
      label("Side fan", "Ventilatore laterale", "Ventola laterale"),
      label("Wind Speed", "Velocità del vento", "Velocità"),
    ],
  },
  // 4 ---------------------------------------------------------------------
  {
    id: "4",
    title: "print_control accepts set_bed_temp (preheat_chamber)",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    edits: [
      {
        marker: "/*PA:4*/",
        applied: /\/\*PA:4\*\/|"stop","set_bed_temp"\]/,
        find: new RegExp(
          String.raw`\["pause","resume","stop"\]\.includes\((?<i>${ID})\)\)throw new Error\("Invalid print control action"\);const (?<y>${ID})=(?<resolve>${ID})\((?<r>${ID})\);if\(!\k<y>\)throw new Error\("Printer not found"\);await (?<send>${ID})\(\k<y>,\k<i>\),`,
          "g"),
        vars: (text) => ({
          DI: need(text, new RegExp(String.raw`(${ID})\.SetTargetBedTempTemp\(${ID}\.address,0,`), "the DeviceInterface class (SetTargetBedTempTemp)"),
        }),
        locals: ["t", "bm", "lim"],
        replace: (m, v) => fill(String.raw`/*PA:4*/["pause","resume","stop","set_bed_temp"].includes(@@i@@))throw new Error("Invalid print control action");const @@y@@=@@resolve@@(@@r@@);if(!@@y@@)throw new Error("Printer not found");if(@@i@@==="set_bed_temp"){const t=Number(@@r@@.value),bm=parseFloat(@@y@@.data&&@@y@@.data.maxBedTemp)||parseFloat(@@y@@.temperature&&@@y@@.temperature.bed&&@@y@@.temperature.bed.max)||0,lim=bm>0?Math.min(bm,130):120;if(!(t>=0&&t<=lim))throw new Error("Invalid bed temperature (max "+lim+")");if(@@y@@.deviceType!==0||@@y@@.oldPrinter)throw new Error("Bed temperature control is available only for LAN printers");@@DI@@.SetTargetBedTempTemp(@@y@@.address,0,t)}else await @@send@@(@@y@@,@@i@@);`, v),
      },
    ],
  },
  // 5 ---------------------------------------------------------------------
  {
    id: "5",
    title: "Send page: model match by printer model (Alpha machineList)",
    dir: SEND,
    fileContains: ".getModelMatch(",
    edits: [
      {
        marker: "/*PA:5*/",
        applied: new RegExp(String.raw`\/\*PA:5\*\/|else if\(!${ID}\.presetName\)Object\.getOwnPropertyNames`),
        find: new RegExp(
          String.raw`(?<pre>(?<N>${ID})\.getModelMatch\(${ID},${ID},(?<t>${ID})\.printerModel,${ID}\)\}else )(?<orig>Object\.getOwnPropertyNames\(\k<t>\.modelMap\)\.length>0&&\((?<e>${ID})\.isModelMatch=1\);)`,
          "g"),
        locals: ["mm"],
        replace: (m, v) => v.pre + fill(String.raw`/*PA:5*/if(!@@t@@.presetName)@@orig@@else{const mm=String(@@e@@.selectDevice.model||"");@@N@@.getModelMatch(@@t@@.presetName,"",@@t@@.printerModel,mm.startsWith("Creality")||mm.startsWith("SPARKX")?mm:"Creality "+mm)};`, v),
      },
    ],
  },
  // 6 ---------------------------------------------------------------------
  {
    id: "6",
    title: "Advisor helpers: chamber state + camera snapshot handlers",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    edits: [
      {
        marker: "/*PA:6*/",
        applied: /\/\*PA:6\*\/|var advisorChamberStates=/,
        find: /(?=const _hoisted_1[\w$]*=\{id:"rooClassWrapper")/g,
        vars: (text) => ({
          reactive: need(text, new RegExp(String.raw`=useMainStore${ID.replace("+", "*")}\(\),${ID}=(${ID})\(\{visable:!1,address:"",multi_device:!0\}\)`), "reactive (App detail state)"),
          ref: need(text, new RegExp(String.raw`function (ref(?:\$\d+)?)\((${ID})\)\{return ${ID}\(\2,!1\)\}`), "the Vue ref() function"),
          dataCenter: need(text, new RegExp(String.raw`const (${ID})=DataCenterIml[\w$]*\.instance\(\)`), "dataCenter"),
          postResult: need(text, new RegExp(String.raw`(${ID})\(\{command:"print_control_result"`), "postResult (print_control_result)"),
        }),
        // names declared by the inserted code in the functions that use each
        // captured name (reactive/ref are used only at module level, next to
        // the advisor* globals)
        locals: {
          reactive: ["advisorChamberStates", "advisorDetailState", "advisorNow", "advisorNowTimer"],
          ref: ["advisorChamberStates", "advisorDetailState", "advisorNow", "advisorNowTimer"],
          // advisorStateFor + advisorChamberText
          dataCenter: ["address", "S", "h", "k", "p", "q", "now", "st", "state", "printing", "idle", "left"],
          // handleAdvisorSnapshot
          postResult: ["r", "d", "id", "addr", "all", "vis", "det", "detAddr", "matched", "v", "im", "sc", "w", "h", "c", "u", "e", "x"],
        },
        replace: (m, v) => fill(String.raw`/*PA:6*/var advisorChamberStates=@@reactive@@({}),advisorDetailState=null,advisorNow=@@ref@@(Date.now()),advisorNowTimer=null;function advisorParsePayload(r){let d=r;if(typeof d=="string")try{d=JSON.parse(d)}catch{d={}}return d&&typeof d.data=="object"&&d.data!==null?d.data:d||{}}function advisorHost(a){return String(a||"").split(":")[0]}function advisorChamberHandler(r){const d=advisorParsePayload(r),num=v=>typeof v=="number"&&isFinite(v)?v:null,key=typeof d.address=="string"?d.address:"";advisorChamberStates[key]={preheat_started_ms:num(d.preheat_started_ms),preheat_bed:num(d.preheat_bed),preheat_chamber_target:num(d.preheat_chamber_target),preheat_followed:d.preheat_followed===!0,chamber:num(d.chamber),soak_seconds:num(d.soak_seconds),print_start_s:num(d.print_start_s),file:typeof d.file=="string"&&d.file?d.file:null,state:num(d.state)},advisorNow.value=Date.now(),advisorStartTimer()}function advisorStateFor(address){const S=advisorChamberStates;if(S[address])return S[address];const h=advisorHost(address);for(const k in S)if(k&&advisorHost(k)===h)return S[k];try{const p=@@dataCenter@@.getPrinter(address);if(p&&p.identity!=null){const q=@@dataCenter@@.getPrinter(p.identity,!1,0);if(q&&S[q.address])return S[q.address]}}catch{}return S[""]||null}function advisorChamberTemp(address){const st=advisorStateFor(address);return st&&st.chamber!=null?st.chamber:0}function advisorStateNeedsTimer(st){return!!st&&(st.preheat_started_ms!=null||!!(st.soak_seconds&&st.print_start_s!=null&&st.print_start_s+st.soak_seconds>Date.now()/1e3))}function advisorTimerNeeded(){return Object.values(advisorChamberStates).some(advisorStateNeedsTimer)}function advisorStartTimer(){advisorNowTimer||!advisorTimerNeeded()||(advisorNowTimer=setInterval(()=>{if(!advisorTimerNeeded()){clearInterval(advisorNowTimer),advisorNowTimer=null;return}advisorNow.value=Date.now()},1e3))}function advisorMmss(t){t=Math.max(0,Math.floor(t));const m=Math.floor(t/60),sec=t%60;return(m<10?"0":"")+m+":"+(sec<10?"0":"")+sec}function advisorBase(f){return String(f||"").split(/[\\/]/).pop().toLowerCase()}function advisorChamberText(address){const now=advisorNow.value,st=advisorStateFor(address);if(!st)return"—";const p=@@dataCenter@@.getPrinter(address),state=p?Number(p.state):st.state!=null?st.state:-1,printing=state===1,idle=state!==1&&state!==5&&state!==6&&state!==7;if(st.preheat_started_ms!=null&&idle)return"Preriscaldo "+advisorMmss((now-st.preheat_started_ms)/1e3)+(st.preheat_chamber_target!=null?" → "+Math.round(st.preheat_chamber_target)+"°":"");if(printing&&st.soak_seconds&&st.print_start_s!=null&&st.file&&p&&advisorBase(p.printFileName)===advisorBase(st.file)){const left=st.print_start_s+st.soak_seconds-now/1e3;if(left>0)return"Soak -"+advisorMmss(Math.ceil(left))}return"—"}async function handleAdvisorSnapshot(r={}){const d=advisorParsePayload(r),id=d&&d.request_id!=null?d.request_id:"",addr=d&&typeof d.address=="string"?d.address:"";try{const all=Array.from(document.querySelectorAll("video")).filter(x=>x.readyState>=2&&x.videoWidth>0),vis=all.filter(x=>x.offsetParent!==null),det=advisorDetailState,detAddr=det&&det.visable&&det.address?det.address:"",matched=!!addr&&!!detAddr&&advisorHost(detAddr)===advisorHost(addr),v=vis[0]||all[0];if(!v){const im=document.querySelector("img.camera-image");throw new Error(im&&im.src?"No playing camera video (MJPEG stream only)":"No playing camera video")}const sc=Math.min(1,1280/v.videoWidth),w=Math.round(v.videoWidth*sc),h=Math.round(v.videoHeight*sc),c=document.createElement("canvas");c.width=w,c.height=h,c.getContext("2d").drawImage(v,0,0,w,h);const u=c.toDataURL("image/jpeg",.8);@@postResult@@({command:"advisor_snapshot_result",request_id:id,ok:!0,data_url:u,width:w,height:h,address:detAddr||null,address_matched:matched})}catch(e){@@postResult@@({command:"advisor_snapshot_result",request_id:id,ok:!1,error:(e==null?void 0:e.message)||"snapshot failed"})}}`, v),
      },
      {
        marker: "/*PA:6.2*/",
        applied: /\/\*PA:6\.2\*\/|registerHandler\("advisor_chamber",/,
        find: new RegExp(String.raw`(?<MC>${ID})\.registerHandler\("print_control",${ID}\),`, "g"),
        replace: (m, v) => m[0] + fill(String.raw`/*PA:6.2*/@@MC@@.registerHandler("advisor_chamber",advisorChamberHandler),@@MC@@.registerHandler("advisor_snapshot",handleAdvisorSnapshot),`, v),
      },
    ],
  },
  // 7 ---------------------------------------------------------------------
  {
    id: "7",
    title: "Chamber card shows the Advisor state instead of a target field",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    requires: ["1", "6"],
    edits: [
      {
        marker: "/*PA:7*/",
        applied: /\/\*PA:7\*\/|"boxSensor"\?"Advisor":/,
        find: new RegExp(String.raw`(?<pre>\()(?<j>${ID})\.\$t\("Target"\)\),1\)\]\),_:1\}\)\]\),_:1\}\),`, "g"),
        // the PrintParams props name, from the "current temperature" cell that follows
        vars: (text, m) => ({
          n: need(text.slice(m.index, m.index + 2000), new RegExp(String.raw`\(\((${ID})\.curTemp\+""\)\.split\("\."\)`), "the PrintParams props (n.curTemp)"),
        }),
        replace: (m, v) => v.pre + fill(String.raw`/*PA:7*/@@n@@.type==="boxSensor"?"Advisor":`, v) + m[0].slice(v.pre.length),
      },
      {
        marker: "/*PA:7.2*/",
        applied: /\/\*PA:7\.2\*\/|class:"target-temp advisor-chamber"/,
        find: new RegExp(
          String.raw`(?<pre>\(\((?<n>${ID})\.curTemp\+""\)\.split\("\."\)\[0\]\)\+" "\+(?<tds>${ID})\(\k<n>\.unit\),1\)\]\),_:1\}\),${ID}\(${ID},\{span:12\},\{default:${ID}\(\(\)=>\[)(?<cbv>${ID})\("div",${ID},\[`,
          "g"),
        replace: (m, v) => v.pre + fill(String.raw`/*PA:7.2*/@@n@@.type==="boxSensor"?@@cbv@@("div",{class:"target-temp advisor-chamber",style:{"line-height":"32px","white-space":"nowrap"}},@@tds@@(advisorChamberText(@@n@@.address)),1):`, v) + m[0].slice(v.pre.length),
      },
    ],
  },
  // 8 ---------------------------------------------------------------------
  {
    id: "8",
    title: "Open the only printer automatically",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    requires: ["6"],
    edits: [
      {
        marker: "/*PA:8*/",
        applied: /\/\*PA:8\*\/|advisorDetailState=[\w$]+;\{let advAutoKey/,
        find: new RegExp(String.raw`(?<B>${ID})\.viewDetialEvent=(?<ze>${ID}),\k<B>\.viewDeviceList=${ID};`, "g"),
        vars: (text, m) => {
          // App setup: de=useMainStore(),le=reactive({visable:!1,address:"",multi_device:!0})
          const st = lastBefore(text, new RegExp(String.raw`(${ID})=useMainStore[\w$]*\(\),(${ID})=${ID}\(\{visable:!1,address:"",multi_device:!0\}\)`, "g"), m.index, "the App stores (useMainStore / detail state)");
          const ze = m.groups.ze.replace(/\$/g, "\\$");
          // ze=(name,address,...)=>{... ie.value.add(...)}: ie is the tab bar ref
          const tab = lastBefore(text, new RegExp(String.raw`[,;{]${ze}=\([^)]*\)=>\{[^]{0,800}?(${ID})\.value\.add\(`, "g"), m.index, "the tab bar ref (ie.value.add)");
          return { de: st[1], le: st[2], ie: tab[1] };
        },
        locals: ["g", "p", "key", "err", "advAutoKey", "advAutoTicks", "advAutoTimer", "advSeen"],
        replace: (m, v) => m[0] + fill(String.raw`/*PA:8*/advisorDetailState=@@le@@;{let advAutoKey="";const advAutoTimer=setInterval(()=>{if(@@le@@.visable){clearInterval(advAutoTimer);return}if(!@@ie@@.value)return;const advSeen=new Map;for(const g of @@de@@.data.printerList||[])for(const p of g&&g.list||[]){if(!p||!p.address)continue;const key=p.deviceType==1&&p.identity!=null?p.identity:p.address;advSeen.has(key)||advSeen.set(key,p)}if(advSeen.size!==1){advAutoKey="";return}const[[key,p]]=advSeen;if(advAutoKey!==key){advAutoKey=key;return}clearInterval(advAutoTimer);try{@@ze@@(p.name,p.address)}catch(err){console.log("advisor auto-open failed",err)}},1e3)}`, v),
      },
    ],
  },
  // 9 ---------------------------------------------------------------------
  {
    id: "9",
    title: "Send page: select the only online printer",
    dir: SEND,
    fileContains: ".getModelMatch(",
    edits: [
      {
        marker: "/*PA:9*/",
        applied: /\/\*PA:9\*\/|const advOn=new Map/,
        find: new RegExp(
          String.raw`(?<pre>if\(!(?<l>${ID})\.selectDevice&&(?<z>${ID})\.length>0\)\{[^]{0,2000}?break\}\})(?=!\k<l>\.selectDevice&&(?<U>${ID})&&\(\k<l>\.selectDevice=\k<U>,\k<l>\.selectDeviceAddress=(?<w>${ID}),${ID}\("select",\k<U>\)\);)`,
          "g"),
        locals: ["D", "k", "P", "advOn"],
        replace: (m, v) => v.pre + fill(String.raw`/*PA:9*/if(!@@l@@.selectDevice&&!@@U@@){const advOn=new Map;for(const D of @@z@@){if(!D||!D.online)continue;const k=D.mac||D.address,P=advOn.get(k);(!P||P.deviceType!==0&&D.deviceType===0)&&advOn.set(k,D)}advOn.size===1&&(@@U@@=[...advOn.values()][0],@@w@@=@@U@@.address,@@l@@.currentDeviceAddress=@@w@@)}`, v),
      },
    ],
  },
  // 10 --------------------------------------------------------------------
  {
    id: "10",
    title: "Send page: cold-chamber notice for ASA/ABS",
    dir: SEND,
    fileContains: ".getModelMatch(",
    edits: [
      // 10.1 keep the chamber temperatures of update_plate_data in the store
      {
        marker: "/*PA:10*/",
        applied: /\/\*PA:10\*\/|\.advisorChamberTemps=/,
        find: new RegExp(
          String.raw`is_only_gcode_mode:${ID}\}=(?<data>${ID});(?<s>${ID})\.setPresetName\(${ID}\),\k<s>\.setPrinterModel\(${ID}\),\k<s>\.setFilamentTypes\(${ID}\),`,
          "g"),
        replace: (m, v) => m[0] + fill(String.raw`/*PA:10*/@@s@@.advisorChamberTemps=@@data@@.advisor_chamber_temps||{},`, v),
      },
      // 10.2 helper, declared in the const chain of the PrintFile hoisted props
      {
        marker: "/*PA:10.2*/",
        applied: /\/\*PA:10\.2\*\/|advisorColdChamber=\(/,
        find: new RegExp(String.raw`${ID}=\{class:"bottom-container"\},${ID}=\{class:"cmd-btn"\},`, "g"),
        replace: (m) => m[0] + String.raw`/*PA:10.2*/advisorColdChamber=(st,store)=>{const dev=st&&st.selectDevice,pl=st&&st.selectPlate;if(!dev||!pl||!dev.online||dev.deviceType!==0)return"";const types=Array.isArray(store&&store.filamentTypes)?store.filamentTypes:[],ex=Array.isArray(pl.extruderIndexs)?pl.extruderIndexs:[];if(!ex.some(k=>/^(ASA|ABS)/i.test(String(types[k-1]||""))))return"";const temps=store.advisorChamberTemps||{},addr=String(dev.address||""),host=addr.split("(")[0].split(":")[0];let tmp=temps[addr];typeof tmp=="number"||(tmp=temps[host]);return typeof tmp=="number"&&isFinite(tmp)&&tmp<30?String(Math.round(tmp)):""},`,
      },
      // 10.3 the notice, a sibling right before the button row
      {
        marker: "/*PA:10.3*/",
        applied: /\/\*PA:10\.3\*\/|advisor-cold-chamber/,
        find: new RegExp(
          String.raw`(?<i>${ID})\.isModelMatch>0&&!(?<d>${ID})\(${ID}\)\(\k<i>\.selectDevice\)\?\((?<P>${ID})\(\),(?<cb>${ID})\("div",${ID},(?<ne>${ID})\(\k<d>\((?<a>${ID})\)\("The currently selected printer \(\{printer\}\)[^"]*",\{printer:\k<i>\.selectDevice\.modelName,preset:\k<d>\((?<t>${ID})\)\.presetName\}\)\),1\)\):(?<te>${ID})\("",!0\)\]\),(?<O>${ID})\("div",${ID},\[[^]{0,700}?\]\),(?=\k<O>\("div",${ID},\[${ID}\(${ID},\{class:"start-print")`,
          "g"),
        replace: (m, v) => m[0] + fill(String.raw`/*PA:10.3*/advisorColdChamber(@@i@@,@@d@@(@@t@@))?(@@P@@(),@@cb@@("div",{class:"advisor-cold-chamber",style:{color:"#ff7d00","font-size":"12px","line-height":"16px",margin:"6px 15px 0","white-space":"normal"}},@@ne@@(@@d@@(@@a@@)("Advisor cold chamber",{temp:advisorColdChamber(@@i@@,@@d@@(@@t@@))})),1)):@@te@@("",!0),`, v),
      },
      // 10.4 / 10.5 texts (vue-i18n messages of the page; other languages fall back to en_GB)
      {
        marker: null,
        applied: /en_GB:\{All:"All","Start Print":"Start Print","Advisor cold chamber":/,
        find: /en_GB:\{All:"All","Start Print":"Start Print",/g,
        replace: (m) => m[0] + String.raw`"Advisor cold chamber":"Cold chamber ({temp} °C): for ASA/ABS it is better to preheat before starting (Print Advisor: preheat_chamber).",`,
      },
      {
        marker: null,
        applied: /it_IT:\{All:"Tutti","Start Print":"Avvia stampa","Advisor cold chamber":/,
        find: /it_IT:\{All:"Tutti","Start Print":"Avvia stampa",/g,
        replace: (m) => m[0] + String.raw`"Advisor cold chamber":"Camera fredda ({temp} °C): per ASA/ABS conviene preriscaldare prima di avviare (Print Advisor: preheat_chamber).",`,
      },
    ],
  },
  // 11 --------------------------------------------------------------------
  {
    id: "11",
    title: "Wider chamber card for the Advisor state",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    requires: ["7"],
    edits: [
      {
        marker: "/*PA:11*/",
        applied: /\/\*PA:11\*\/|:has\(\.advisor-chamber\)/,
        // end of the PrintParams media query (inside the CSS template literal: no backticks, no ${)
        find: new RegExp(String.raw`@media screen and \(min-width:80px\)and \(max-width:1380px\)\{\.print-params-item\[data-v-(?<h>[0-9a-f]+)\]\{width:130px;min-width:80px\}\[data-v-\k<h>\] \.el-input__wrapper\{padding:1px 3px!important\}\}`, "g"),
        replace: (m, v) => m[0] + fill(String.raw`/*PA:11*/.print-params-item[data-v-@@h@@]:has(.advisor-chamber){flex:1 1 320px;width:auto;min-width:320px}.print-params-item[data-v-@@h@@]:has(.advisor-chamber) .el-row>.el-col:first-child{flex:0 0 30%;max-width:30%}.print-params-item[data-v-@@h@@]:has(.advisor-chamber) .el-row>.el-col:last-child{flex:0 0 70%;max-width:70%}`, v),
      },
    ],
  },
  // 12 --------------------------------------------------------------------
  {
    id: "12",
    title: "Device page: hide the 'Buy filament' link (no eshop handler since F3)",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    edits: [
      {
        marker: "/*PA:12*/",
        applied: /\/\*PA:12\*\/|\.inline-buy-link\[data-v-[0-9a-f]+\]\{display:none!important\}/,
        // the scoped rule of the inline "Buy filament" link (inside the CSS template literal: no backticks, no ${)
        find: new RegExp(String.raw`\.inline-buy-link\[data-v-(?<h>[0-9a-f]+)\]\{display:inline-block;padding:0 2px;line-height:1;position:relative;vertical-align:baseline\}`, "g"),
        replace: (m, v) => m[0] + fill(String.raw`/*PA:12*/.inline-buy-link[data-v-@@h@@]{display:none!important}`, v),
      },
    ],
  },
  // 12b -------------------------------------------------------------------
  {
    id: "12b",
    title: "Send page: hide the 'Buy filament' link (no eshop handler since F3)",
    dir: SEND,
    fileContains: ".getModelMatch(",
    edits: [
      {
        marker: "/*PA:12b*/",
        applied: /\/\*PA:12b\*\/|\.inline-buy-link\[data-v-[0-9a-f]+\]\{display:none!important\}/,
        find: new RegExp(String.raw`\.inline-buy-link\[data-v-(?<h>[0-9a-f]+)\]\{display:inline-block;padding:0 2px;line-height:1;position:relative;vertical-align:baseline\}`, "g"),
        replace: (m, v) => m[0] + fill(String.raw`/*PA:12b*/.inline-buy-link[data-v-@@h@@]{display:none!important}`, v),
      },
    ],
  },
  // 13 --------------------------------------------------------------------
  {
    id: "13",
    title: "Send page: no Creality Cloud token check without an account (F4)",
    dir: SEND,
    fileContains: ".getModelMatch(",
    edits: [
      {
        marker: "/*PA:13*/",
        applied: /\/\*PA:13\*\//,
        find: new RegExp(String.raw`(?<s>${ID})\.setToken\((?<v>${ID})\|\|""\),\k<s>\.checkTokenIsValid\(\),`, "g"),
        replace: (m, v) => fill(String.raw`@@s@@.setToken(@@v@@||""),/*PA:13*/@@v@@&&@@s@@.checkTokenIsValid(),`, v),
      },
    ],
  },
  // 14 --------------------------------------------------------------------
  {
    id: "14",
    title: "Homepage: no Firebase/GA, no Creality tracking, no internet probe (F7)",
    dir: HOME,
    fileContains: "initCxAnalytics(",
    edits: [
      { // Firebase + Google Analytics (root App component, every route)
        marker: "/*PA:14a*/",
        applied: /\/\*PA:14a\*\//,
        find: new RegExp(String.raw`initFirebase\((?<u>${ID})\)\{\k<u>\?initFirebase_dev\(\):initFirebase\(\)\}`, "g"),
        replace: (m, v) => fill(String.raw`initFirebase(@@u@@){/*PA:14a*/}`, v),
      },
      { // Creality data tracking (HomePage component)
        marker: "/*PA:14b*/",
        applied: /\/\*PA:14b\*\//,
        find: new RegExp(String.raw`initCxAnalytics\((?<u>${ID})\)\{initDataTracking\(\k<u>\)\}`, "g"),
        replace: (m, v) => fill(String.raw`initCxAnalytics(@@u@@){/*PA:14b*/}`, v),
      },
      { // GA events: return before getAnalytics()
        marker: "/*PA:14c*/",
        applied: /\/\*PA:14c\*\//,
        find: new RegExp(String.raw`sendGAEvent=\((?<a>${ID}),(?<b>${ID})\)=>\{`, "g"),
        replace: (m, v) => fill(String.raw`sendGAEvent=(@@a@@,@@b@@)=>{/*PA:14c*/return;`, v),
      },
      { // internet probe to api.crealitycloud.*: report offline without fetching
        marker: "/*PA:14d*/",
        applied: /\/\*PA:14d\*\//,
        find: new RegExp(String.raw`function checkInternet\((?<c>${ID})\)\{return (?<x>${ID})\(this,null,function\*\(\)\{`, "g"),
        replace: (m, v) => fill(String.raw`function checkInternet(@@c@@){return @@x@@(this,null,function*(){/*PA:14d*/return!1;`, v),
      },
      { // Creality event tracking (devdata.cxswyjy.com): 14b stops its initialisation, this
        // stub also stops the sender itself (defence in depth, cleanup of 2026-10-24)
        marker: "/*PA:14e*/",
        applied: /\/\*PA:14e\*\//,
        find: new RegExp(String.raw`sendTrackingEvent=\((?<a>${ID}),(?<b>${ID})\)=>(?<x>${ID})\(null,null,function\*\(\)\{`, "g"),
        replace: (m, v) => fill(String.raw`sendTrackingEvent=(@@a@@,@@b@@)=>@@x@@(null,null,function*(){/*PA:14e*/return;`, v),
      },
    ],
  },
  // 15 --------------------------------------------------------------------
  {
    id: "15",
    title: "Homepage: only Projects in the side menu (no account, community, shop) (F7)",
    dir: HOME,
    fileContains: "initCxAnalytics(",
    requires: ["14"],
    edits: [
      { // login/account block at the top of the side menu
        marker: "/*PA:15a*/",
        applied: /\/\*PA:15a\*\//,
        find: /createVNode\(Account,\{style:\{marginBottom:"10px"\}\}\),/g,
        replace: () => `/*PA:15a*/`,
      },
      { // menu items Online Params, Guide Forum, Personal Center (sub-menu), Feedback
        marker: "/*PA:15b*/",
        applied: /\/\*PA:15b\*\//,
        find: new RegExp(String.raw`,\(openBlock\(\),createBlock\((?<d>${ID}),\{key:(?<z>${ID})\["Online Params"\]\}[\s\S]*?\(openBlock\(\),createBlock\(\k<d>,\{key:\k<z>\.Feedback\}[\s\S]*?toDisplayString\$1\((?<a>${ID})\.\$t\("Feedback"\)\),9,${ID}\)\]\)\]\),_:1\}\)\)`, "g"),
        replace: () => `/*PA:15b*/`,
      },
      { // "Creality Mall" link at the bottom of the menu
        marker: "/*PA:15c*/",
        applied: /\/\*PA:15c\*\//,
        find: /createBaseVNode\("div",\{class:"menu-item",onClick:/g,
        replace: () => `createBaseVNode("div",{class:"menu-item",style:{display:"none"}/*PA:15c*/,onClick:`,
      },
    ],
  },
  // 16 --------------------------------------------------------------------
  {
    id: "16",
    title: "Homepage: no official printer list from the Creality cloud (OnlineParams pane) (F7)",
    dir: HOME,
    fileContains: "initCxAnalytics(",
    requires: ["15"],
    edits: [
      { // the OnlineParams pane posts printerList to admin-pre/api.crealitycloud at start-up
        marker: "/*PA:16*/",
        applied: /\/\*PA:16\*\//,
        find: /function getParameterModelList\(\)\{return [\w$]+\(\{url:"\/api\/cxy\/v2\/slice\/profile\/official\/printerList",method:"post",data:\{engineVersion:"[\d.]+"\},headers:\{__CXY_PLATFORM_:\d+\}\}\)\}/g,
        replace: () => `function getParameterModelList(){/*PA:16*/return Promise.resolve({printerList:[],series:[]})}`,
      },
    ],
  },
  // 17 --------------------------------------------------------------------
  {
    id: "17",
    title: "Chamber card: Preriscalda / Stop button (bed preheat through Print Advisor)",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    requires: ["6", "7"],
    edits: [
      { // helpers next to advisorChamberText (patch 6)
        marker: "/*PA:17*/",
        applied: /\/\*PA:17\*\//,
        find: /(?=function advisorChamberText\(address\)\{)/g,
        vars: (text) => ({
          dataCenter: need(text, new RegExp(String.raw`const (${ID})=DataCenterIml[\w$]*\.instance\(\)`), "dataCenter"),
          postResult: need(text, new RegExp(String.raw`(${ID})\(\{command:"print_control_result"`), "postResult (print_control_result)"),
          cbv: need(text, new RegExp(String.raw`function (createBaseVNode[\w$]*)\(`), "createBaseVNode"),
        }),
        locals: ["address", "p", "state", "st", "on", "vis", "h", "s", "e"],
        replace: (m, v) => fill(String.raw`/*PA:17*/var advisorPreheatTargets={};function advisorPreheatVisible(address){const p=@@dataCenter@@.getPrinter(address),state=p?Number(p.state):-1;return state===0||state===2||state===3||state===4}function advisorPreheatActive(address){const st=advisorStateFor(address);return!!st&&(st.preheat_followed||st.preheat_started_ms!=null)&&advisorPreheatVisible(address)}function advisorPreheatTargetText(address){const h=advisorHost(address);if(!(h in advisorPreheatTargets)){let s="";try{s=localStorage.getItem("advisorPreheatTarget:"+h)||""}catch{}advisorPreheatTargets[h]=s}return advisorPreheatTargets[h]}function advisorPreheatClick(address){@@postResult@@(advisorPreheatActive(address)?{command:"advisor_preheat",address:address,start:!1}:{command:"advisor_preheat",address:address,start:!0,target_chamber_temp:String(advisorPreheatTargetText(address)).trim()})}function advisorPreheatTargetInput(address){advisorNow.value;const vis=advisorPreheatVisible(address)&&!advisorPreheatActive(address);return @@cbv@@("input",{class:"advisor-preheat-target",type:"text",inputmode:"decimal",maxlength:4,placeholder:"°C",title:"Obiettivo camera in °C (facoltativo): il preriscaldo finisce quando la camera lo raggiunge. Vuoto: finisce quando la camera smette di salire",value:advisorPreheatTargetText(address),style:{display:vis?"inline-block":"none",width:"44px",height:"24px","margin-left":"8px",padding:"0 4px","box-sizing":"border-box","font-size":"12px","text-align":"center","border-radius":"4px",border:"1px solid #666",background:"transparent",color:"inherit","vertical-align":"middle"},onInput:e=>{const h=advisorHost(address);advisorPreheatTargets[h]=e.target.value;try{localStorage.setItem("advisorPreheatTarget:"+h,e.target.value)}catch{}}},null,16)}function advisorPreheatButton(address){advisorNow.value;const vis=advisorPreheatVisible(address),on=advisorPreheatActive(address);return @@cbv@@("button",{class:"advisor-preheat-btn",type:"button",title:on?"Spegne il piatto e termina il preriscaldo":"Scalda il piatto per preriscaldare la camera; si spegne da solo se la stampa non parte",style:{display:vis?"inline-block":"none","margin-left":"8px",padding:"0 10px",height:"24px","line-height":"22px","font-size":"12px","font-weight":"500","border-radius":"12px",border:"1px solid #3dbd4b",background:on?"#3dbd4b":"transparent",color:on?"#fff":"#3dbd4b",cursor:"pointer","vertical-align":"middle"},onClick:()=>advisorPreheatClick(address)},on?"Stop":"Preriscalda",17)}`, v),
      },
      { // the Advisor cell of the chamber card (patch 7.2) gets the button after the text
        marker: "/*PA:17.2*/",
        applied: /\/\*PA:17\.2\*\//,
        find: new RegExp(String.raw`\{class:"target-temp advisor-chamber",style:\{"line-height":"32px","white-space":"nowrap"\}\},(?<tds>${ID})\(advisorChamberText\((?<n>${ID})\.address\)\),1\)`, "g"),
        vars: (text) => ({
          ctv: need(text, new RegExp(String.raw`function (createTextVNode[\w$]*)\(`), "createTextVNode"),
        }),
        replace: (m, v) => fill(String.raw`{class:"target-temp advisor-chamber",style:{"line-height":"32px","white-space":"nowrap"}},[/*PA:17.2*/@@ctv@@(@@tds@@(advisorChamberText(@@n@@.address)),1),advisorPreheatTargetInput(@@n@@.address),advisorPreheatButton(@@n@@.address)])`, v),
      },
    ],
  },
  // 18 --------------------------------------------------------------------
  {
    id: "18",
    title: "Device page: LED card in the fan row (room for the wider chamber card)",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    requires: ["11"],
    edits: [
      { // the LED PrintControl joins the fan row after the fan list (read from the unpatched text, so this edit runs first)...
        marker: "/*PA:18.2*/",
        applied: /\/\*PA:18\.2\*\//,
        find: new RegExp(String.raw`,null,8,\["name","isOpen","fanSpeed","type","address"\]\)\):createCommentVNode\("",!0\)\],64\)\)\),128\)\)\]\),createBaseVNode\("div",_hoisted_\d+\$?\w*,\[createVNode\$1\(PrintSpeed,`, "g"),
        vars: (text) => {
          const m = text.match(new RegExp(String.raw`(?:,)((${ID})\.value\?\(openBlock\(\),createBlock\(PrintControl,\{key:0,class:"led-light-control",isLightComponent:!0,name:"LED",isOpen:${ID}\.value,address:${ID}\.address,type:"light"\},null,8,\["isOpen","address"\]\)\):createCommentVNode\("",!0\))\]\)`));
          if (!m) throw new Error("cannot find the LED card expression");
          return { led: m[1] };
        },
        replace: (m, v) => `,null,8,["name","isOpen","fanSpeed","type","address"])):createCommentVNode("",!0)],64))),128)),/*PA:18.2*/` + v.led + `]),createBaseVNode("div",` + m[0].slice(m[0].indexOf("createBaseVNode(\"div\",") + "createBaseVNode(\"div\",".length),
      },
      { // ...and leaves the temperature row
        marker: "/*PA:18*/",
        applied: /\/\*PA:18\*\//,
        find: new RegExp(String.raw`,(?<led>(?<k>${ID})\.value\?\(openBlock\(\),createBlock\(PrintControl,\{key:0,class:"led-light-control",isLightComponent:!0,name:"LED",isOpen:${ID}\.value,address:${ID}\.address,type:"light"\},null,8,\["isOpen","address"\]\)\):createCommentVNode\("",!0\))\]\),createBaseVNode\("div",(?<w>_hoisted_\d+\$?\w*),\[\(openBlock\(!0\),createElementBlock\(Fragment,null,renderList\((?<H>${ID})\.value,`, "g"),
        replace: (m, v) => fill(String.raw`/*PA:18*/]),createBaseVNode("div",@@w@@,[(openBlock(!0),createElementBlock(Fragment,null,renderList(@@H@@.value,`, v),
      },
    ],
  },
  // 19 --------------------------------------------------------------------
  {
    id: "19",
    title: "Open the only printer each time the Device tab is selected",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    requires: ["6", "8"],
    edits: [
      { // after the start-up timer of patch 8, the same check as a function (immediate, no
        // two-tick confirmation): exactly one printer and no detail view open -> open it
        marker: "/*PA:19*/",
        applied: /\/\*PA:19\*\//,
        find: /console\.log\("advisor auto-open failed",err\)\}\},1e3\)\}/g,
        vars: (text, m) => {
          const head = lastBefore(text, /advisorDetailState=([\w$]+);\{let advAutoKey="";const advAutoTimer=setInterval\(\(\)=>\{if\(\1\.visable\)\{clearInterval\(advAutoTimer\);return\}if\(!([\w$]+)\.value\)return;const advSeen=new Map;for\(const g of ([\w$]+)\.data\.printerList/g, m.index, "the patch 8 timer");
          const call = text.slice(head.index, m.index + m[0].length).match(/try\{([\w$]+)\(p\.name,p\.address\)\}catch\(err\)/);
          if (!call) throw new Error("cannot find the detail-open call of patch 8");
          return { le: head[1], ie: head[2], de: head[3], ze: call[1] };
        },
        locals: ["seen", "g", "p", "key", "only", "shown", "ms"],
        replace: (m, v) => m[0] + fill(String.raw`/*PA:19*/;window.advisorOpenSingle=()=>{const shown=document.querySelector(".print-params-item");if(shown&&shown.offsetParent!==null||!@@ie@@.value)return;const seen=new Map;for(const g of @@de@@.data.printerList||[])for(const p of g&&g.list||[]){if(!p||!p.address)continue;const key=p.deviceType==1&&p.identity!=null?p.identity:p.address;seen.has(key)||seen.set(key,p)}if(seen.size!==1)return;const[[,only]]=seen;try{@@ze@@(only.name,only.address)}catch(err){console.log("advisor open-single failed",err)}};`, v),
      },
      { // command sent by the native side when the Device tab is selected (after init_device)
        marker: "/*PA:19.2*/",
        applied: /\/\*PA:19\.2\*\//,
        find: new RegExp(String.raw`(?<MC>${ID})\.registerHandler\("advisor_chamber",advisorChamberHandler\),`, "g"),
        replace: (m, v) => m[0] + fill(String.raw`/*PA:19.2*/@@MC@@.registerHandler("advisor_open_single",()=>{for(const ms of[400,1500,3000])setTimeout(()=>{window.advisorOpenSingle&&window.advisorOpenSingle()},ms)}),`, v),
      },
    ],
  },
  // 20 --------------------------------------------------------------------
  {
    id: "20",
    title: "Device page: LAN diagnosis without the internet probe to baidu.com",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    edits: [
      { // checkInternet fetched https://www.baidu.com/ before the LAN diagnosis of an offline
        // printer and stopped it with "Network abnormality" without internet: report online
        marker: "/*PA:20*/",
        applied: /\/\*PA:20\*\//,
        find: /static async checkInternet\(\)\{if\(!navigator\.onLine\)/g,
        replace: () => `static async checkInternet(){/*PA:20*/return!0;if(!navigator.onLine)`,
      },
    ],
  },
  // 21 --------------------------------------------------------------------
  {
    id: "21",
    title: "Homepage first-run guide: no software-mode and login-region steps (F7, F4)",
    dir: HOME,
    fileContains: "initCxAnalytics(",
    edits: [
      { // BeginGuide starts on its last step (connect a printer / add a printer preset):
        // "software mode" only offered the professional edition (easy mode is gone) and
        // "login region" opened the removed Creality login and never moved on
        marker: "/*PA:21a*/",
        applied: /\/\*PA:21a\*\//,
        find: new RegExp(String.raw`(?<Q>${ID})=ref\((?<U>${ID})\.CHOOSE_VERSION\),`, "g"),
        replace: (m, v) => fill(String.raw`@@Q@@=ref(/*PA:21a*/@@U@@.CONNECT_PRINTER),`, v),
      },
      { // the step saved in sessionStorage by an older page may not bring the two steps back
        marker: "/*PA:21b*/",
        applied: /\/\*PA:21b\*\//,
        find: new RegExp(String.raw`\[1,2,3\]\.includes\((?<e>${ID})==null\?void 0:\k<e>\.step\)`, "g"),
        replace: (m, v) => fill(String.raw`[3]/*PA:21b*/.includes(@@e@@==null?void 0:@@e@@.step)`, v),
      },
      { // no "Back" button: it only led to the removed steps
        marker: "/*PA:21c*/",
        applied: /\/\*PA:21c\*\//,
        find: new RegExp(String.raw`${ID}\.value!==${ID}\.CHOOSE_VERSION\?\(openBlock\(\),createElementBlock\("div",`, "g"),
        replace: () => `/*PA:21c*/!1?(openBlock(),createElementBlock("div",`,
      },
    ],
  },
  // 22 --------------------------------------------------------------------
  {
    id: "22",
    title: "Homepage: Firebase SDK and GA event code removed (F7)",
    dir: HOME,
    fileContains: "initCxAnalytics(",
    requires: ["14"],
    edits: [
      { // the bundled Firebase SDK (util ... analytics) and the `isDev` flag after it;
        // after 14a/14c only initFirebase, initFirebase_dev and sendGAEvent used it
        marker: "/*PA:22a*/",
        applied: /\/\*PA:22a\*\//,
        find: new RegExp(String.raw`,getDefaultsFromPostinstall=\(\)=>\{\};${FIREBASE_SDK}let isDev=!1;`, "g"),
        replace: () => `;/*PA:22a*/`,
      },
      { // initFirebase / initFirebase_dev with the Creality Firebase configs (API keys)
        marker: "/*PA:22b*/",
        applied: /\/\*PA:22b\*\//,
        find: /const initFirebase=\(\)=>\{const [\w$]+=\{apiKey:[\s\S]*?\},initFirebase_dev=\(\)=>\{[\s\S]*?\},getAppVersion=/g,
        replace: () => `const /*PA:22b*/getAppVersion=`,
      },
      { // enrichEventData and the dead body of sendGAEvent (14c returned first);
        // sendGAEvent stays as an empty function for its callers
        marker: "/*PA:22c*/",
        applied: /\/\*PA:22c\*\//,
        find: new RegExp(String.raw`,enrichEventData=${ID}=>\{[\s\S]*?\},sendGAEvent=\((?<a>${ID}),(?<b>${ID})\)=>\{\/\*PA:14c\*\/return;[\s\S]*?"\[GA\] sendGAEvent logEvent called",\{event:${ID},isDev\}\)\};`, "g"),
        replace: (m, v) => fill(String.raw`,sendGAEvent=(@@a@@,@@b@@)=>{/*PA:14c*//*PA:22c*/};`, v),
      },
    ],
  },
  // 23 --------------------------------------------------------------------
  {
    id: "23",
    title: "Device page: unused Firebase SDK copies removed (F7)",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    edits: [
      { // two SDK copies (11.4.0 and 11.10.0), never initialized: no code outside them
        // uses their names. The first one is the copy followed by another copy.
        marker: "/*PA:23a*/",
        applied: /\/\*PA:23a\*\//,
        find: new RegExp(String.raw`${FIREBASE_SDK}(?=[\s\S]*?${FIREBASE_SDK_START})`, "g"),
        replace: () => `/*PA:23a*/`,
      },
      {
        marker: "/*PA:23b*/",
        applied: /\/\*PA:23b\*\//,
        find: new RegExp(FIREBASE_SDK, "g"),
        replace: () => `/*PA:23b*/`,
      },
    ],
  },
  // 24 --------------------------------------------------------------------
  {
    id: "24",
    title: "Send dialog: unused Firebase SDK removed (F7)",
    dir: SEND,
    fileContains: ".getModelMatch(",
    edits: [
      { // one SDK copy, never initialized: no code outside it uses its names
        marker: "/*PA:24*/",
        applied: /\/\*PA:24\*\//,
        find: new RegExp(FIREBASE_SDK, "g"),
        replace: () => `/*PA:24*/`,
      },
    ],
  },
  // 25 --------------------------------------------------------------------
  {
    id: "25",
    title: "Homepage: Alibaba OSS SDK removed (cloud upload, unused since F4)",
    dir: HOME,
    fileContains: "initCxAnalytics(",
    edits: [
      { // the bundled ali-oss browser SDK, wrapped as a CommonJS module in `aliyunOssSdk`;
        // its only name is referenced nowhere else (checked below)
        marker: "/*PA:25*/",
        applied: /\/\*PA:25\*\//,
        find: /var aliyunOssSdk=\{exports:\{\}\};\(function\([\w$]+,[\w$]+\)\{[\s\S]*?\}\)\(aliyunOssSdk\);/g,
        vars: (text, m) => {
          const rest = text.slice(0, m.index) + text.slice(m.index + m[0].length);
          if (/\baliyunOssSdk\b/.test(rest)) throw new Error("aliyunOssSdk is referenced outside the SDK");
          return {};
        },
        replace: () => `/*PA:25*/`,
      },
    ],
  },
  // 26 --------------------------------------------------------------------
  {
    id: "26",
    title: "Device page: no \"log in to bind the device\" banner (no account since F4)",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    edits: [
      { // the token is always empty without the Creality login: the LoginView banner was always
        // shown and its button led nowhere
        marker: "/*PA:26*/",
        applied: /\/\*PA:26\*\//,
        find: new RegExp(String.raw`unref\(sysStore\)\(\)\.token\.length===0\?\(openBlock\(\),createBlock\(LoginView,`, "g"),
        replace: () => `/*PA:26*/!1?(openBlock(),createBlock(LoginView,`,
      },
    ],
  },
  // 27 --------------------------------------------------------------------
  {
    id: "27",
    title: "Homepage first-run screen: \"Welcome to MentorSlicer\", no cloud subtitle",
    dir: HOME,
    fileContains: "Add printers and manage print jobs from the cloud",
    edits: [
      { // the title keeps its translation (22 languages) with the app name swapped at render time
        marker: "/*PA:27*/",
        applied: /\/\*PA:27\*\//,
        find: new RegExp(String.raw`(?<t>${ID})\.\$t\("Welcome to Creality Print"\)`, "g"),
        replace: (m, v) => fill(String.raw`/*PA:27*/@@t@@.$t("Welcome to Creality Print").replace(/Creality Print/g,"MentorSlicer")`, v),
      },
      { // "Add printers and manage print jobs from the cloud": there is no cloud in MentorSlicer
        marker: "/*PA:27b*/",
        applied: /\/\*PA:27b\*\//,
        find: new RegExp(String.raw`${ID}\.\$t\("Add printers and manage print jobs from the cloud"\)`, "g"),
        replace: () => `/*PA:27b*/""`,
      },
    ],
  },
];
