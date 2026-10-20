#include "WebRoutes.h"

#include <ArduinoJson.h>
#include <WebServer.h>

#include "app_globals.h"

extern WebServer server;
extern bool uiDarkMode;
extern String npArea;
extern int npResMin;
extern String npUpdatedAt;

// Handlers still defined in the main .ino; this module only centralises shared Web/UI glue and route registration.
extern void handleDashboard();
extern void handleControlPage();
extern void handleLegionellaPage();
extern void handleSettingsPage();
extern void handleToggleForce();
extern void handleControlSaveLimits();
extern void handleControlSaveTemperatureGate();
extern void handleLegionellaStart();
extern void handleLegionellaCancel();
extern void handleLegionellaSave();
extern void handleLegionellaResetSuccess();
extern void handleRelayToggle();
extern void handleSettingsSaveReboot();
extern void handleSettingsSave();
extern void handleSettingsSaveSensors();
extern void handleSettingsHaMqttCleanup();

String htmlCss() {
  return String(
    ":root{--bg:#f3f4f6;--card:#ffffff;--text:#0b1220;--muted:rgba(11,18,32,.62);--border:rgba(11,18,32,.08);--accent:#16a34a;--accent2:#22c55e;--danger:#dc2626;}"    "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial;margin:0;background:var(--bg);color:var(--text);-webkit-font-smoothing:antialiased;}"    ".wrap{max-width:520px;margin:0 auto;padding:3px 3px 104px;}"    "h1{margin:4px 0 0;font-size:20px;letter-spacing:-.2px;font-weight:800;text-align:center}"    ".sub{margin:6px 0 10px;color:var(--muted);font-weight:700}"    ".tiny{margin:0 0 10px;color:rgba(11,18,32,.55);font-size:12.5px}"    ".tiny .ok{color:var(--accent);font-weight:900}"    ".tiny .bad{color:var(--danger);font-weight:900}"    ".chips{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0 10px}"    ".chip{display:inline-flex;align-items:center;gap:8px;padding:6px 10px;border-radius:999px;border:1px solid var(--border);background:rgba(255,255,255,.72);backdrop-filter:saturate(160%) blur(8px);font-size:12.5px}"    ".dot{width:8px;height:8px;border-radius:10px;background:var(--accent)}"    ".dot.bad{background:var(--danger)}"    ".card{background:var(--card);border:1px solid var(--border);border-radius:18px;box-shadow:0 10px 26px rgba(0,0,0,.08);overflow:hidden;margin:12px 0;}"    ".hd{padding:14px 16px 10px;border-bottom:1px solid var(--border);display:flex;align-items:center;justify-content:space-between;gap:10px;}"    ".bd{padding:14px 16px 16px;}"    ".tag{display:inline-flex;align-items:center;gap:6px;padding:6px 10px;border-radius:999px;background:rgba(22,163,74,.10);border:1px solid rgba(22,163,74,.18);font-size:12px;font-weight:800;color:rgba(22,163,74,.95)}"    ".tag.gray{background:#f3f4f6;border-color:var(--border);color:rgba(11,18,32,.62)}"    ".row{display:flex;justify-content:space-between;gap:14px;padding:9px 0;border-bottom:1px solid rgba(11,18,32,.07);}"    ".row:last-child{border-bottom:none;}"    ".k{color:var(--muted);font-size:13px}"    ".v{font-weight:900;font-size:13.5px;text-align:right}"    "label{display:block;font-size:13px;color:var(--muted);margin:10px 0 6px}"    "input,select{width:100%;padding:11px 12px;border-radius:14px;border:1px solid var(--border);background:#fff;box-sizing:border-box;font-size:15px;outline:none}"    "input:focus,select:focus{border-color:rgba(22,163,74,.55);box-shadow:0 0 0 3px rgba(22,163,74,.14)}"    ".grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px}"    ".btn{width:100%;box-sizing:border-box;border:none;border-radius:16px;padding:12px 14px;font-weight:900;cursor:pointer;background:linear-gradient(180deg,var(--accent2),var(--accent));color:#fff;font-size:15px}"    ".btn.dark{background:#111827}"    ".btn.red{background:var(--danger)}"    ".btn.ghost{background:#fff;color:var(--text);border:1px solid var(--border)}"    ".help{margin-top:8px;font-size:12.5px;color:var(--muted)}"    ".sep{height:1px;background:var(--border);margin:10px 0}"    ".vnum{font-weight:900}"    ".vnum.pos{color:var(--danger)}"    ".vnum.neg{color:var(--accent)}"    ".bar{margin-top:8px;height:10px;border-radius:999px;background:#eef2ff;overflow:hidden;border:1px solid var(--border)}"    ".bar .fill{height:100%;width:0%;background:linear-gradient(90deg,var(--accent2),var(--accent));border-radius:999px;transition:width .25s ease}"    ".spark{margin-top:8px;height:90px;border-radius:16px;background:#f8fafc;border:1px solid var(--border);overflow:hidden}"    ".spark canvas{width:100%;height:100%;display:block}"    ".spark.priceSpark{height:150px;position:relative}"    ".chartTip{position:absolute;z-index:5;pointer-events:none;opacity:0;transform:translateY(-4px);background:rgba(255,255,255,.92);border:1px solid rgba(0,0,0,.10);border-radius:12px;padding:6px 10px;font-size:12px;color:rgba(17,24,39,.9);box-shadow:0 10px 24px rgba(0,0,0,.18);backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px);transition:opacity .08s ease, transform .08s ease}"    ".topTabs{display:flex;justify-content:center;margin:10px 0 12px}"    ".seg{display:inline-flex;gap:0;background:rgba(255,255,255,.72);border:1px solid var(--border);border-radius:14px;padding:3px;backdrop-filter:saturate(160%) blur(10px)}"    ".seg a{flex:1;min-width:86px;text-align:center;text-decoration:none;color:rgba(11,18,32,.62);font-weight:900;font-size:13px;padding:8px 10px;border-radius:11px}"    ".seg a.active{background:#fff;color:var(--text);box-shadow:0 6px 14px rgba(0,0,0,.10)}"    ".nav{position:fixed;left:0;right:0;bottom:0;padding:10px 12px 16px;background:transparent;pointer-events:none}"    ".navin{pointer-events:auto;max-width:520px;margin:0 auto;background:rgba(255,255,255,.78);border:1px solid var(--border);border-radius:24px;box-shadow:0 12px 30px rgba(0,0,0,.14);display:flex;justify-content:space-around;padding:10px 6px;backdrop-filter:saturate(180%) blur(14px)}"    ".nav a{font-size:11.5px;text-decoration:none;color:rgba(11,18,32,.55);font-weight:800;display:flex;flex-direction:column;align-items:center;gap:5px;min-width:56px}"    ".nav .ico{width:20px;height:20px;display:block}"    ".nav a.active{color:var(--accent)}"    ".nav a.active svg{stroke:var(--accent)}"    ".nav svg{width:20px;height:20px;stroke:rgba(11,18,32,.45);fill:none;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}"    ".switch{position:relative;display:inline-block;width:46px;height:28px;vertical-align:middle}"    ".switch input{display:none}"    ".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:rgba(11,18,32,.15);transition:.2s;border-radius:999px}"    ".slider:before{position:absolute;content:\"\";height:22px;width:22px;left:3px;top:3px;background:white;transition:.2s;border-radius:999px;box-shadow:0 2px 8px rgba(0,0,0,.18)}"    ".switch.on .slider{background:linear-gradient(180deg,var(--accent2),var(--accent))}"    ".switch.on .slider:before{transform:translateX(18px)}"".inrow{display:flex;gap:8px;align-items:center}"".pm{width:44px;min-width:44px;height:42px;border-radius:14px;border:1px solid var(--border);background:rgba(255,255,255,.86);font-weight:1000;font-size:16px;cursor:pointer}"".pm:active{transform:scale(.98)}"".spinbox{display:flex;align-items:stretch;gap:8px}.spinbox input{flex:1}.spinBtns{display:flex;flex-direction:column;gap:6px}.spinBtn{width:44px;min-width:44px;height:18px;border-radius:10px;border:1px solid var(--border);background:rgba(255,255,255,.86);font-weight:1000;cursor:pointer;line-height:1}.spinBtn:active{transform:scale(.98)}"    ".acc{background:var(--card);border:1px solid var(--border);border-radius:18px;box-shadow:0 10px 26px rgba(0,0,0,.08);overflow:hidden;margin:12px 0;}"    " .acc>summary{list-style:none;cursor:pointer;padding:14px 16px 10px;border-bottom:1px solid var(--border);}"    " .acc>summary::-webkit-details-marker{display:none;}"    " .acc[open]>summary{background:rgba(255,255,255,.6);}"    " #np_settings>summary{display:flex;align-items:center;justify-content:space-between;gap:12px;}"    " .sum{display:flex;align-items:center;justify-content:space-between;gap:10px;}"    " .accbd{padding:14px 16px 16px;}"    "body.dark{--bg:radial-gradient(1200px 600px at 20% -10%, rgba(34,197,94,.22), rgba(15,20,17,0) 55%),radial-gradient(900px 500px at 110% 20%, rgba(22,163,74,.16), rgba(15,20,17,0) 60%),#0f1411;--card:rgba(26,34,29,.86);--text:#e8f1ec;--muted:rgba(232,241,236,.74);--border:rgba(232,241,236,.16);}"    "body.dark h1{color:var(--text);text-shadow:0 2px 18px rgba(34,197,94,.10);}"    "body.dark .card,body.dark .acc{backdrop-filter:saturate(170%) blur(14px);box-shadow:0 16px 44px rgba(0,0,0,.46);}"
    "body.dark .chip{background:rgba(26,34,29,.55);border-color:rgba(232,241,236,.12);}"    "body.dark .tag.gray{background:rgba(232,241,236,.08);color:rgba(232,241,236,.70);}"    "body.dark .row{border-bottom:1px solid rgba(232,241,236,.08);}"    "body.dark input,body.dark select{background:#0f1411;color:var(--text);border-color:rgba(232,241,236,.12);}"    "body.dark input:focus,body.dark select:focus{border-color:rgba(34,197,94,.55);box-shadow:0 0 0 3px rgba(34,197,94,.18);}"    "body.dark .btn.ghost{background:rgba(26,34,29,.92);color:var(--text);border-color:rgba(232,241,236,.12);}"    "body.dark .pm,body.dark .spinBtn{background:rgba(26,34,29,.92);color:var(--text);border-color:rgba(232,241,236,.12);}"    "body.dark .navin{background:rgba(26,34,29,.60);border-color:rgba(232,241,236,.12);box-shadow:0 18px 50px rgba(0,0,0,.55);}"    "body.dark .nav svg{stroke:rgba(232,241,236,.55);}"    "body.dark .seg{background:rgba(26,34,29,.60);border-color:rgba(232,241,236,.12);}"    "body.dark .seg a.active{background:rgba(26,34,29,.92);box-shadow:0 10px 26px rgba(0,0,0,.35);}"    "body.dark .acc[open]>summary{background:rgba(26,34,29,.72);}"    "body.dark .spark{background:rgba(26,34,29,.72);border-color:rgba(232,241,236,.14);}"    "body.dark .chartTip{background:rgba(15,20,17,.92);border-color:rgba(232,241,236,.14);color:rgba(232,241,236,.92);}"    "body.dark .tiny{color:var(--muted)}"    "body.dark .tiny .ok{color:var(--accent)}"    "body.dark .tiny .bad{color:var(--danger)}"    "body.dark .nav a{color:rgba(232,241,236,.72)}"    "body.dark .nav a.active{color:var(--accent)}"    "body.dark .nav svg{stroke:rgba(232,241,236,.60)}"    "body.dark .seg a{color:rgba(232,241,236,.70)}"    "body.dark .seg a.active{color:var(--text)}"    "body.dark .slider{background:rgba(232,241,236,.22)}"    "body.dark .switch.on .slider{background:linear-gradient(180deg,var(--accent2),var(--accent))}"    "body.dark .acc>summary{color:var(--text)}"    "body.dark .btnLink{color:rgba(232,241,236,.78)}"
    "/* ===== Fancy dashboard additions (step51) ===== */.card{border-radius:20px;}.card.fancy{padding:14px;}.fRow{display:flex;align-items:center;gap:10px;padding:12px 12px;border:1px solid var(--border);border-radius:18px;background:linear-gradient(180deg,rgba(0,0,0,.04),rgba(0,0,0,.00));}body.dark .fRow{background:linear-gradient(180deg,rgba(255,255,255,.04),rgba(255,255,255,.00));}.fDot{width:10px;height:10px;border-radius:50%;background:var(--accent2);box-shadow:0 0 0 3px rgba(34,197,94,.12);}.fMode{font-weight:800;letter-spacing:.2px;}.fState{margin-left:auto;font-weight:800;opacity:.9;display:flex;align-items:center;gap:8px;}.fState .runDot{width:8px;height:8px;border-radius:50%;background:var(--accent2);box-shadow:0 0 16px rgba(34,197,94,.45);}.meterFlow{margin-top:12px;padding:14px;border-radius:20px;border:1px solid var(--border);background:linear-gradient(180deg,rgba(0,0,0,.05),rgba(0,0,0,.00));overflow:hidden;}body.dark .meterFlow{background:linear-gradient(180deg,rgba(255,255,255,.05),rgba(255,255,255,.00));}.mfTop{display:grid;grid-template-columns:1fr auto auto;gap:10px;align-items:center;}.mfLbl{font-size:13px;color:var(--muted);}.mfDelta{font-size:18px;font-weight:900;}.mfMid{display:grid;grid-template-columns:auto 1fr auto;gap:12px;align-items:center;margin-top:10px;}.mfVal{font-size:28px;font-weight:900;letter-spacing:.3px;min-width:112px;}.mfVal.neg{color:var(--danger);text-shadow:0 0 18px rgba(220,38,38,.18);}.mfVal.pos{color:var(--accent2);text-shadow:0 0 18px rgba(34,197,94,.18);}.mfArrow{position:relative;height:10px;}.mfLine{position:absolute;left:0;right:16px;top:50%;transform:translateY(-50%);height:2px;border-radius:999px;opacity:.9;}.mfHead{position:absolute;right:0;top:50%;transform:translateY(-50%);width:0;height:0;border-left:10px solid #ffb44b;border-top:6px solid transparent;border-bottom:6px solid transparent;filter:drop-shadow(0 0 10px rgba(255,180,75,.35));}.mfArrow.idle .mfLine{background:repeating-linear-gradient(90deg,rgba(255,180,75,.70) 0 10px, rgba(255,180,75,.12) 10px 18px);}.mfArrow.active .mfLine{background:linear-gradient(90deg,rgba(34,197,94,.25),rgba(34,197,94,.85));}.mfArrow.active .mfHead{border-left-color:rgba(34,197,94,.95);filter:drop-shadow(0 0 10px rgba(34,197,94,.35));}.mfArrow.max .mfLine{background:linear-gradient(90deg,rgba(255,180,75,.20),rgba(255,180,75,.90));}.mfArrow.max .mfHead{border-left-color:#ffb44b;filter:drop-shadow(0 0 10px rgba(255,180,75,.45));}.mfBadge{font-size:14px;font-weight:900;padding:10px 14px;border-radius:999px;border:1px solid var(--border);white-space:nowrap;}.mfBadge.warn{color:#ffb44b;background:rgba(255,180,75,.10);box-shadow:0 0 0 3px rgba(255,180,75,.07),0 0 22px rgba(255,180,75,.18);}.mfBadge.good{color:var(--accent2);background:rgba(34,197,94,.10);box-shadow:0 0 0 3px rgba(34,197,94,.07),0 0 22px rgba(34,197,94,.16);}.mfBadge.bad{color:var(--danger);background:rgba(220,38,38,.08);box-shadow:0 0 0 3px rgba(220,38,38,.06),0 0 22px rgba(220,38,38,.14);}.mfBottom{display:flex;gap:10px;flex-wrap:wrap;margin-top:10px;}.mfChip{font-size:13px;padding:8px 12px;border-radius:999px;border:1px solid var(--border);background:rgba(255,255,255,.04);color:var(--muted);}body.light .mfChip{background:rgba(0,0,0,.03);}.kpiGrid{display:grid;grid-template-columns:1fr 140px;gap:12px;margin-top:12px;}.mcActiveRow{display:none;flex-wrap:wrap;gap:10px;margin-top:10px;}.mcActiveChip{display:flex;align-items:center;gap:8px;padding:10px 14px;border-radius:999px;border:1px solid var(--border);background:linear-gradient(180deg,rgba(0,0,0,.04),rgba(0,0,0,.00));font-size:14px;font-weight:900;line-height:1;}.mcActiveChip .mcName{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:180px;}.mcActiveChip .mcSep{opacity:.45;}.mcActiveChip .mcPct{white-space:nowrap;}.mcActiveChip.isOn{border-color:var(--accent2);box-shadow:0 0 0 3px color-mix(in srgb,var(--accent2) 16%, transparent),0 0 22px color-mix(in srgb,var(--accent2) 18%, transparent);background:linear-gradient(180deg,color-mix(in srgb,var(--accent2) 10%, transparent),rgba(0,0,0,0));}body.dark .mcActiveChip{background:linear-gradient(180deg,rgba(255,255,255,.04),rgba(255,255,255,.00));}body.dark .mcActiveChip.isOn{background:linear-gradient(180deg,color-mix(in srgb,var(--accent2) 14%, transparent),rgba(255,255,255,0));}.phaseGrid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-top:10px;}.phaseBox{padding:12px;border-radius:18px;border:1px solid var(--border);background:linear-gradient(180deg,rgba(0,0,0,.04),rgba(0,0,0,.00));display:flex;align-items:center;justify-content:space-between;}body.dark .phaseBox{background:linear-gradient(180deg,rgba(255,255,255,.04),rgba(255,255,255,.00));}.phaseBox.isSel{border-color:var(--accent2);box-shadow:0 0 0 3px color-mix(in srgb,var(--accent2) 18%, transparent),0 0 22px color-mix(in srgb,var(--accent2) 22%, transparent);background:linear-gradient(180deg,color-mix(in srgb,var(--accent2) 12%, transparent),rgba(0,0,0,0));}body.dark .phaseBox.isSel{background:linear-gradient(180deg,color-mix(in srgb,var(--accent2) 16%, transparent),rgba(255,255,255,0));}.phaseLbl{font-size:12px;color:var(--muted);font-weight:800;letter-spacing:.2px;}.phaseVal{font-size:16px;font-weight:900;display:flex;align-items:baseline;gap:6px;}.phaseUnit{font-size:12px;color:var(--muted);font-weight:800;}.kpiBox{padding:14px;border-radius:20px;border:1px solid var(--border);background:linear-gradient(180deg,rgba(0,0,0,.04),rgba(0,0,0,.00));}body.dark .kpiBox{background:linear-gradient(180deg,rgba(255,255,255,.04),rgba(255,255,255,.00));}.kpiTitle{font-size:13px;color:var(--muted);margin-bottom:6px;}.kpiBig{font-size:22px;font-weight:900;}.kpiBig.neg{color:var(--danger);}.kpiBig.pos{color:var(--accent2);}.pwmRing{--pwm:0;--track:rgba(0,0,0,.10);--inner:rgba(255,255,255,.92);width:140px;height:140px;border-radius:50%;border:1px solid var(--border);display:grid;place-items:center;position:relative;background:conic-gradient(var(--accent2) calc(var(--pwm)*1%), var(--track) 0);box-shadow:inset 0 0 0 1px rgba(0,0,0,.12);}body.dark .pwmRing{--track:rgba(255,255,255,.12);--inner:rgba(18,26,22,.92);box-shadow:inset 0 0 0 1px rgba(255,255,255,.05);}.pwmRing::before{content:'';position:absolute;inset:14px;border-radius:50%;background:var(--inner);border:1px solid var(--border);}.pwmRing::after{content:'';position:absolute;inset:-2px;border-radius:50%;pointer-events:none;box-shadow:0 0 28px rgba(34,197,94,.55),0 0 70px rgba(34,197,94,.18);opacity:0;transition:opacity .25s ease;}.pwmRing>*{position:relative;z-index:2;}.pwmRing.isActive::after{opacity:.55;}.pwmRing.isMax::after{opacity:.90;}.pwmRing .pct{font-size:34px;font-weight:900;}.pwmRing .sub{position:absolute;bottom:12px;font-size:12px;color:var(--muted);}.advBox{margin-top:12px;padding:14px;border-radius:20px;border:1px solid var(--border);background:linear-gradient(180deg,rgba(0,0,0,.04),rgba(0,0,0,.00));}body.dark .advBox{background:linear-gradient(180deg,rgba(255,255,255,.04),rgba(255,255,255,.00));}.advMain{font-size:22px;font-weight:900;display:flex;align-items:center;gap:10px;}.advMain .pill{margin-left:auto;font-size:12px;font-weight:900;padding:7px 10px;border-radius:999px;border:1px solid var(--border);color:#ffb44b;background:rgba(255,180,75,.10);}.advSub{margin-top:6px;color:var(--muted);}/* Actions sub-card */.actionCard{margin-top:12px;padding:14px;border-radius:20px;border:1px solid var(--border);background:linear-gradient(180deg,rgba(0,0,0,.04),rgba(0,0,0,.00));}body.dark .actionCard{background:linear-gradient(180deg,rgba(255,255,255,.04),rgba(255,255,255,.00));}.row.nb{border-bottom:none;}"    "@media(max-width:420px){.hd{padding:12px 8px 9px}.bd{padding:12px 8px 12px}.acc>summary{padding:12px 8px 9px}}"
  );
}

String htmlHeader(const String& pageTitle) {
  String s;
  s.reserve(2000);
  s += "<!doctype html><html><head><meta charset='utf-8'>";
  s += "<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1'>";
  s += String("<title>") + DEVICE_NAME + "</title><style>";
  s += htmlCss();
  s += "</style>";
  s += R"JS(<script>
// --- Toggle submit normalizer (prevents 'unchecked checkbox not submitted') ---
function prepTogglesOnSubmit(form){
  try{
    const cbs = form.querySelectorAll("input[type='checkbox'][name]");
    cbs.forEach(cb=>{
      const n = cb.getAttribute("name");
      if(!n) return;

      let hid = form.querySelector("input[type='hidden'][data-for='"+n+"']");
      if(!hid){
        hid = document.createElement("input");
        hid.type = "hidden";
        hid.setAttribute("data-for", n);
        hid.name = n;
        form.appendChild(hid);
      }
      hid.value = cb.checked ? "1" : "0";

      cb.setAttribute("data-orig-name", n);
      cb.name = n + "_chk";
    });

    setTimeout(()=>{
      cbs.forEach(cb=>{
        const on = cb.getAttribute("data-orig-name");
        if(on){ cb.name = on; cb.removeAttribute("data-orig-name"); }
      });
    }, 500);
  }catch(e){}
}
window.addEventListener("submit", function(e){
  const f = e.target;
  if(f && f.tagName==="FORM") {
    try{
      var sub = e.submitter || null;
      if(sub){
        var sec = sub.getAttribute('data-section') || '';
        if(!sec && sub.name === '_section') sec = sub.value || '';
        if(sec){
          var hs = f.querySelector("input[type='hidden'][data-role='section']");
          if(!hs){
            hs = document.createElement('input');
            hs.type = 'hidden';
            hs.name = '_section';
            hs.setAttribute('data-role','section');
            f.appendChild(hs);
          }
          hs.value = sec;
        }
      }
    }catch(err){}
    prepTogglesOnSubmit(f);
  }
}, true);
// --- end toggle submit normalizer ---

function postUrl(u){
  try{
    fetch(u,{
      method:"POST",
      cache:"no-store",
      headers:{"Content-Type":"application/x-www-form-urlencoded"},
      body:""
    }).catch(()=>{});
  }catch(e){}
}

// Legionella quick-action button (Dashboard): keep user on Dashboard
function setLegBtn(btn, running, labelOverride){
  try{
    if(!btn) return;
    btn.dataset.running = running ? "1" : "0";
    btn.classList.toggle('red', !!running);
    btn.classList.toggle('dark', !running);
    btn.textContent = labelOverride ? labelOverride : (running ? 'Stop legionella' : 'Start legionella nu');
  }catch(e){}
}

function legToggle(btn){
  try{
    var running = (btn && btn.dataset && btn.dataset.running === '1');
    // optimistic UI (snappy), then sync from ESP
    setLegBtn(btn, !running, (!running) ? 'Stop legionella' : 'Start legionella nu');
    postUrl(running ? '/legionella/cancel' : '/legionella/start');

    setTimeout(function(){
      fetch('/api/status_light', {cache:'no-store'})
        .then(r => r.json())
        .then(j => {
          if(j && typeof j.legionella_running !== 'undefined') {
            setLegBtn(btn, !!j.legionella_running);
          }
          // also refresh the text line on Dashboard
          var ls = document.getElementById('leg_state');
          if(ls && j && j.legionella_state) ls.textContent = j.legionella_state;
        })
        .catch(()=>{});
    }, 600);
  }catch(e){}
}

function setLegSwitch(el, running){
  try{
    if(!el) return;
    if(running) el.classList.add('on'); else el.classList.remove('on');
    var cb = el.querySelector('input[type=checkbox]');
    if(cb) cb.checked = !!running;
  }catch(e){}
}

function setRelaySwitch(el, on){
  try{
    if(!el) return;
    if(on) el.classList.add('on'); else el.classList.remove('on');
    var cb = el.querySelector('input[type=checkbox]');
    if(cb) cb.checked = !!on;
  }catch(e){}
}

function relaySw(el){
  try{
    var wasOn = el.classList.contains('on');
    sw(el,false);
    postUrl('/relay/toggle');

    setTimeout(function(){
      fetch('/api/status_light', {cache:'no-store'})
        .then(r => r.json())
        .then(j => {
          if(j && typeof j.relay_manual_on !== 'undefined'){
            setRelaySwitch(el, !!j.relay_manual_on);
          } else {
            setRelaySwitch(el, !wasOn);
          }
        })
        .catch(()=>{ setRelaySwitch(el, !wasOn); });
    }, 400);
  }catch(e){}
}

function legSw(el){
  try{
    var wasOn = el.classList.contains('on');
    // optimistic UI
    sw(el,false);
    postUrl(wasOn ? '/legionella/cancel' : '/legionella/start');

    setTimeout(function(){
      fetch('/api/status_light', {cache:'no-store'})
        .then(r => r.json())
        .then(j => {
          if(j && typeof j.legionella_running !== 'undefined'){
            setLegSwitch(el, !!j.legionella_running);
          }
        })
        .catch(()=>{});
    }, 600);
  }catch(e){}
}


// Toggle helper for the custom switch element (keeps user on Dashboard; no full reload)
function sw(el, doPost){
  try{
    var cb = el.querySelector('input[type=checkbox]');
    if(cb){ cb.checked = !cb.checked; try{ cb.dispatchEvent(new Event('change',{bubbles:true})); }catch(e){} }
    el.classList.toggle('on');
if(!doPost) return;
    var f = el.closest('form');
    if(!f) return;
    var u = f.getAttribute('action') || "";
    if(!u) return;

    fetch(u,{
      method:"POST",
      cache:"no-store",
      headers:{"Content-Type":"application/x-www-form-urlencoded"},
      body:""
    }).catch(()=>{});
  }catch(e){}
}
</script>)JS";
  // Theme: apply dark-mode class to <body> when enabled (stored in NVS)
  s += String("</head><body") + (uiDarkMode ? " class='dark'" : "") + "><div class='wrap'>";
  s += String("<h1>") + DEVICE_NAME + "</h1>";
  s += "<div class='sub'>" + pageTitle + "</div>";
  return s;
}


String iconSvg(const String& id) {
  // Lightweight inline SVG icons (no external assets) — keeps UI fast.
  if (id == "dash") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M3 11.5 12 4l9 7.5'/><path d='M6 10.5V20h12v-9.5'/></svg>";
  if (id == "prices") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M4 19V5'/><path d='M4 19h16'/><path d='M7 15l3-3 3 2 4-6'/></svg>";
  if (id == "control") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M4 7h10'/><path d='M4 17h16'/><path d='M14 7v6'/><path d='M10 17V9'/></svg>";
  if (id == "leg") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M10 14.5a4 4 0 1 0 4 0V5a2 2 0 0 0-4 0v9.5'/><path d='M12 6v8'/></svg>";
  if (id == "set") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M12 15.5a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7Z'/><path d='M19.4 15a7.8 7.8 0 0 0 .1-6l2-1.2-2-3.4-2.3.8a7.9 7.9 0 0 0-5.2-2.2L11.4 1H8.6L8 3.8A7.9 7.9 0 0 0 2.8 6l-2.3-.8-2 3.4 2 1.2a7.8 7.8 0 0 0 .1 6l-2 1.2 2 3.4 2.3-.8a7.9 7.9 0 0 0 5.2 2.2L8.6 23h2.8l.6-2.8a7.9 7.9 0 0 0 5.2-2.2l2.3.8 2-3.4-2-1.2Z'/></svg>";
  return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M4 12h16'/></svg>";
}

// Top segmented tabs (like the mockup). Bottom navigation remains for one-hand use.
String topTabs(const String& active) {
  auto tab = [&](const String& href, const String& name, const String& id) -> String {
    String a = "<a href='" + href + "'";
    if (active == id) a += " class='active'";
    a += ">" + name + "</a>";
    return a;
  };

  String s;
  s += "<div class='topTabs'><div class='seg'>";
  s += tab("/", "Dashboard", "dash");
  s += tab("/control", "Control", "control");
  s += tab("/legionella", "Legionella", "leg");
  s += tab("/settings", "Settings", "set");
  s += "</div></div>";
  return s;
}

String navHtml(const String& active) {
  auto tab = [&](const String& href, const String& name, const String& id) -> String {
    String a = "<a href='" + href + "'";
    if (active == id) a += " class='active'";
    a += "><span class='ico'>" + iconSvg(id) + "</span><span>" + name + "</span></a>";
    return a;
  };

  String s;
  s += "<div class='nav'><div class='navin'>";
  s += tab("/", "Dashboard", "dash");
  s += tab("/prices", "Prijzen", "prices");
  s += tab("/control", "Control", "control");
  s += tab("/legionella", "Legionella", "leg");
  s += tab("/settings", "Settings", "set");
  s += "</div></div>";
  return s;
}


String htmlFooter(const String& activeNav) {
  String s;
  s += "</div>";
  s += navHtml(activeNav);
  s += "</body></html>";
  return s;
}

String rowHtml(const String& k, const String& v) {
  return "<div class='row'><div class='k'>" + k + "</div><div class='v'>" + v + "</div></div>";
}

String htmlAutoReturnAfterReboot(const String& title, const String& msg, const String& targetUrl) {
  String p;
  p.reserve(2600);

  p += "<!doctype html><html><head><meta charset='utf-8'>";
  p += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  p += "<title>" + title + "</title>";
  p += "<style>";
  // Use the same theme CSS as the rest of the UI (keeps firmware/update pages consistent)
  p += htmlCss();
  // Minimal overrides: no bottom-nav on these pages, and a slightly tighter wrap.
  p += ".wrap{padding-bottom:18px}";
  p += ".btnLink{display:inline-block;margin-top:10px;color:var(--muted);font-weight:900;text-decoration:none}";
  p += "</style></head><body";
  p += (uiDarkMode ? " class='dark'" : "");
  p += ">";
  p += "<div class='wrap'>";
  p += String("<h1>") + DEVICE_NAME + "</h1>";
  p += "<div class='sub'>" + title + "</div>";
  p += "<div class='card'>";
  p += "<div class='hd'><b>" + title + "</b><span class='tag'>Systeem</span></div>";
  p += "<div class='bd'>";
  p += "<div class='help' id='msg' style='font-size:14px'>" + msg + "</div>";
  p += String("<div class='help' style='margin-top:10px' id='hint'>Wachten tot ") + DEVICE_NAME + " opnieuw online is…</div>";
  p += "<a class='btn' href='" + targetUrl + "' style='display:block;text-align:center;text-decoration:none'>Ga naar Dashboard</a>";
  p += "</div></div></div>";

  // Pas redirecten NA een echte offline moment (reboot)
  p += "<script>";
  p += "(()=>{";
  p += "const target='" + targetUrl + "';";
  p += "let seenFail=false;";
  p += "let tries=0;";
  p += "const t0=Date.now();";
  p += "const timeoutMs=120000;";
  p += "const hint=document.getElementById('hint');";
  p += "async function ping(){";
  p += "tries++;";
  p += "try{";
  p += "const r=await fetch('/api/status?ts='+Date.now(),{cache:'no-store'});";
  p += "if(r.ok){";
  p += "if(seenFail){ window.location.replace(target); return; }";
  p += "} else { seenFail=true; }";
  p += "}catch(e){ seenFail=true; }";
  p += "if(Date.now()-t0>timeoutMs){";
  p += "hint.textContent='Nog geen verbinding. Check IP (DHCP) of open handmatig het dashboard.';";
  p += "clearInterval(intv);";
  p += "} else {";
  p += "hint.textContent='Wachten… ('+tries+')';";
  p += "}";
  p += "}";
  p += "const intv=setInterval(ping,1000);";
  p += "setTimeout(ping,1200);";
  p += "})();";
  p += "</script>";

  p += "</body></html>";
  return p;
}

void handlePricesPage() {
  String page = htmlHeader("Nordpool prijzen (ct/kWh)");

  // (UI) Top tabs verwijderd – bottom nav is genoeg

  // ===== Samenvatting =====
  page += "<div class='card'><div class='hd'><b>Nordpool prijzen (ct/kWh)</b><span class='tag'>samenvatting</span></div><div class='bd'>";
  page += rowHtml("Nu", "<span id='sum_now'>-</span> ct/kWh");
  page += rowHtml("Goedkoopste (vandaag)", "<span id='sum_min'>-</span>");
  page += rowHtml("Duurste (vandaag)", "<span id='sum_max'>-</span>");
  page += rowHtml("Morgen beschikbaar", "<span id='sum_tom'>-</span>");
  page += "</div></div>";

  // ===== Vandaag =====
  page += "<div class='card'><div class='hd'><b>Vandaag</b><span class='tag' id='tag_today'>-</span></div><div class='bd'>";
  page += "<div class='spark priceSpark'><div class='chartTip' id='tip_today'></div><canvas id='c_today'></canvas></div>";
  page += "<div class='help' id='h_today'>-</div>";
  page += "<details class='acc'><summary>details</summary><div class='bd'><div class='tiny' id='d_today'>-</div></div></details>";
  page += "</div></div>";

  // ===== Morgen =====
  page += "<div class='card' id='card_tom'><div class='hd'><b>Morgen</b><span class='tag' id='tag_tom'>-</span></div><div class='bd'>";
  page += "<div class='spark priceSpark'><div class='chartTip' id='tip_tom'></div><canvas id='c_tom'></canvas></div>";
  page += "<div class='help' id='h_tom'>-</div>";
  page += "<details class='acc'><summary>details</summary><div class='bd'><div class='tiny' id='d_tom'>-</div></div></details>";
  page += "</div></div>";

  // ===== Gas (laatste 15 dagen incl. vandaag) =====
  page += "<div class='card' id='card_gas'><div class='hd'><b>Gas prijzen</b><span class='tag'>EUR/m³</span></div><div class='bd'>";
  page += "<div class='tiny'>Laatste update: <span id='gas_upd'>-</span></div>";
  page += "<div class='spark priceSpark' style='margin-top:10px'><div class='chartTip' id='tip_gas'></div><canvas id='c_gas15'></canvas></div>";
  page += "<div class='help' id='h_gas'>-</div>";
  page += "<details class='acc'><summary>details</summary><div class='bd'><div class='tiny' id='d_gas'>-</div></div></details>";
  page += "</div></div>";
  // ===== Instellingen (verplaatst, inklapbaar) =====
  page += "<details class='acc card' id='np_settings'><summary><span>Instellingen</span><span class='tag'>Nordpool</span></summary><div class='bd'>";
  page += rowHtml("Gebied", "<span id='np_area'>" + npArea + "</span>");
  page += rowHtml("Resolutie", "<span id='np_res'>" + String(npResMin) + "</span> min");
  page += rowHtml("Laatste update", "<span id='np_upd'>" + npUpdatedAt + "</span>");
  page += rowHtml("Status", "<span id='np_stat'>-</span>");
  page += "</div></details>";


  // eenvoudige bar chart (zonder extra libs)
  page += R"JS(
<script>
(() => {
  const el = (id)=>document.getElementById(id);
  const cToday = el('c_today');
  const cTom   = el('c_tom');
  const cGas   = el('c_gas15');
  const tipToday = el('tip_today');
  const tipTom   = el('tip_tom');
  const tipGas   = el('tip_gas');

  function resizeCanvas(c){
    if(!c) return;
    const dpr = window.devicePixelRatio || 1;
    const r = c.getBoundingClientRect();
    const w = Math.max(10, Math.round(r.width * dpr));
    const h = Math.max(10, Math.round(r.height * dpr));
    if (c.width !== w || c.height !== h){
      c.width = w; c.height = h;
      c._dpr = dpr;
    }
  }

  function fmtTime(mins){
    mins = Math.max(0, mins|0);
    const hh = Math.floor(mins/60) % 24;
    const mm = mins % 60;
    return String(hh).padStart(2,'0') + ':' + String(mm).padStart(2,'0');
  }

  function calcStats(arr){
    if(!arr || arr.length === 0) return null;
    let min = arr[0], max = arr[0], sum = 0, minIdx = 0, maxIdx = 0;
    for(let i=0;i<arr.length;i++){
      const v = Number(arr[i]);
      sum += v;
      if(v < min){ min = v; minIdx = i; }
      if(v > max){ max = v; maxIdx = i; }
    }
    return {min, max, avg: (sum/arr.length), minIdx, maxIdx};
  }

  function lineMinAvgMax(arr){
    const st = calcStats(arr);
    if(!st) return '-';
    return `Min. ${st.min.toFixed(2)} ct/kWh — Gem. ${st.avg.toFixed(2)} ct/kWh — Max. ${st.max.toFixed(2)} ct/kWh`;
  }


  function lineMinAvgMaxUnit(arr, unit, dec){
    const st = calcStats(arr);
    if(!st) return '-';
    dec = (dec==null)?2:dec;
    unit = unit || '';
    return `Min. ${st.min.toFixed(dec)} ${unit} — Gem. ${st.avg.toFixed(dec)} ${unit} — Max. ${st.max.toFixed(dec)} ${unit}`;
  }

  function detailsBlock(arr, interval){
    if(!arr || arr.length === 0) return '-';
    return `punten=${arr.length} • interval=${interval}min`;
  }

  function drawBars(c, arr, opts){
    if(!c) return;
    opts = opts || {};
    resizeCanvas(c);
    const ctx = c.getContext('2d');
    const w = c.width, h = c.height;
    ctx.clearRect(0,0,w,h);

    const dpr = c._dpr || 1;

    if(!arr || arr.length === 0){
      ctx.fillStyle='rgba(17,24,39,.45)';
      ctx.font = (12*dpr)+'px system-ui';
      ctx.fillText('Geen data', 10*dpr, 20*dpr);
      c._meta = null;
      return;
    }

    let min = Math.min(...arr);
    let max = Math.max(...arr);
    const zeroLineMode = (opts.zeroLine === true);

    // Alleen stroomprijsgrafieken rond de 0-lijn tekenen.
    // Gas gebruikt bewust de oude schaal/weergave.
    if(zeroLineMode){
      min = Math.min(min, 0);
      max = Math.max(max, 0);
    }

    let span = (max - min);
    if (!isFinite(span) || span <= 0.0000001){
      if(zeroLineMode){
        min = -0.05;
        max = 0.05;
      } else {
        const base = isFinite(min) ? min : 0;
        const pad = Math.max(0.05, Math.abs(base) * 0.05);
        min = base - pad;
        max = base + pad;
      }
      span = (max - min);
    } else {
      const pad = span * 0.05;
      min -= pad;
      max += pad;
      span = (max - min);
    }

    const padTop = 10*dpr;
    const padBot = 22*dpr;
    const padL = 10*dpr;
    const padR = 10*dpr;

    const n = arr.length;
    const innerW = w - padL - padR;
    const innerH = h - padTop - padBot;
    const barW = innerW / n;

    const hi = (opts.highlightIdx ?? -1);
    const interval = (opts.interval ?? 60);
    const yForValue = (v)=> padTop + innerH - (((v - min) / span) * innerH);
    const zeroY = Math.max(padTop, Math.min(padTop + innerH, yForValue(0)));

    // grid baseline / 0-lijn
    ctx.strokeStyle = zeroLineMode ? 'rgba(148,163,184,.22)' : 'rgba(148,163,184,.18)';
    ctx.lineWidth = 1*dpr;
    ctx.beginPath();
    ctx.moveTo(padL, zeroLineMode ? zeroY : padTop + innerH);
    ctx.lineTo(padL+innerW, zeroLineMode ? zeroY : padTop + innerH);
    ctx.stroke();

    // bars
    for(let i=0;i<n;i++){
      const v = Number(arr[i]);
      const x = padL + i*barW;
      const isHi = (i === hi);

      if(zeroLineMode){
        const isNeg = v < 0;
        const valueY = Math.max(padTop, Math.min(padTop + innerH, yForValue(v)));
        const y = isNeg ? zeroY : valueY;
        const bh = Math.max(2*dpr, Math.abs(zeroY - valueY));

        if(isNeg){
          ctx.fillStyle = isHi ? 'rgba(34,197,94,.95)' : 'rgba(34,197,94,.80)';
        } else {
          ctx.fillStyle = isHi ? 'rgba(59,130,246,.95)' : 'rgba(59,130,246,.80)';
        }
        ctx.fillRect(x+1*dpr, y, Math.max(1*dpr, barW-2*dpr), bh);
      } else {
        const t = (v - min) / span;
        const bh = Math.max(2*dpr, Math.round(t * innerH));
        const y = padTop + innerH - bh;

        ctx.fillStyle = isHi ? 'rgba(34,197,94,.95)' : 'rgba(59,130,246,.80)';
        ctx.fillRect(x+1*dpr, y, Math.max(1*dpr, barW-2*dpr), bh);
      }
    }

    // meta for tooltip
    // labels[] is optional (e.g. gas can pass ISO dates). If absent, tooltip falls back to time/day.
    c._meta = {padL, padTop, innerW, innerH, barW, min, max, interval, arr,
              unit:(opts.unit||'ct/kWh'), decimals:opts.decimals, labels:(opts.labels||null), _dpr:(c._dpr||1)};
  }

  function attachTooltip(canvas, tipEl){
    if(!canvas || !tipEl) return;
    const show = (e)=>{
      const m = canvas._meta;
      if(!m) return;
      const r = canvas.getBoundingClientRect();
      const x = (e.touches ? e.touches[0].clientX : e.clientX) - r.left;
      const idx = Math.max(0, Math.min(m.arr.length-1, Math.floor((x - (m.padL/(m._dpr||1))) / (m.barW/ (m._dpr||1)))));
      const v = Number(m.arr[idx]);
      const mins = idx * m.interval;
      const unit = (m.unit || 'ct/kWh');
      const dec = (m.decimals==null?2:m.decimals);
      const fmtIso = (iso)=> (iso && String(iso).length>=10) ? `${String(iso).slice(8,10)}-${String(iso).slice(5,7)}-${String(iso).slice(0,4)}` : String(iso||'');
      const label = (m.labels && m.labels[idx]) ? fmtIso(m.labels[idx]) : ((m.interval >= 1440) ? (`Dag ${idx+1}`) : fmtTime(mins));
      tipEl.textContent = `${label} • ${v.toFixed(dec)} ${unit}`;
      tipEl.style.opacity = 1;
    };
    const hide = ()=>{ tipEl.style.opacity = 0; };
    canvas.addEventListener('mousemove', show);
    canvas.addEventListener('mouseleave', hide);
    canvas.addEventListener('touchstart', show, {passive:true});
    canvas.addEventListener('touchmove', show, {passive:true});
    canvas.addEventListener('touchend', hide);
  }

  attachTooltip(cToday, tipToday);
  attachTooltip(cTom, tipTom);
  attachTooltip(cGas, tipGas);

  async function tick(){
    try{
      const r = await fetch('/api/prices?ts='+Date.now(), {cache:'no-store'});
      if(!r.ok) throw new Error('HTTP '+r.status);
      const j = await r.json();

      el('np_area').textContent = j.area || '-';
      el('np_res').textContent  = (j.want_res_min ?? '-') ;
      el('np_upd').textContent  = j.last_update || j.updatedAt || '-';
      el('np_stat').textContent = j.status_text || '-';

      // TODAY
      const tArr = j.today_ct_kwh || [];
      const tInt = Number(j.today_interval_min || 60);
      el('tag_today').textContent = j.today_date_dmy || j.today_date || '-';

      // highlight current slot (best effort)
      const now = new Date();
      const nowMin = now.getHours()*60 + now.getMinutes();
      let hi = Math.floor(nowMin / tInt);
      if(hi < 0) hi = 0;
      if(hi >= tArr.length) hi = tArr.length - 1;

      drawBars(cToday, tArr, {interval:tInt, highlightIdx:hi, zeroLine:true});
      el('h_today').textContent = lineMinAvgMax(tArr);
      el('d_today').textContent = detailsBlock(tArr, tInt);

      // summary based on today
      const st = calcStats(tArr);
      if(st){
        el('sum_now').textContent = (tArr[hi] != null ? Number(tArr[hi]).toFixed(2) : '-');
        el('sum_min').textContent = `${st.min.toFixed(2)} ct/kWh om ${fmtTime(st.minIdx * tInt)}`;
        el('sum_max').textContent = `${st.max.toFixed(2)} ct/kWh om ${fmtTime(st.maxIdx * tInt)}`;
      } else {
        el('sum_now').textContent = '-';
        el('sum_min').textContent = '-';
        el('sum_max').textContent = '-';
      }

      // TOMORROW
      const tomArr = j.tomorrow_ct_kwh || [];
      const hasTom = tomArr.length > 0;
      const tomInt = Number(j.tomorrow_interval_min || tInt || 60);
      el('tag_tom').textContent = j.tomorrow_date_dmy || j.tomorrow_date || '-';

      el('sum_tom').textContent = hasTom ? 'ja' : 'nee';

      const cardTom = el('card_tom');
      if(!hasTom){
        if(cardTom) cardTom.style.opacity = 0.55;
        drawBars(cTom, [], {interval:tomInt, highlightIdx:-1, zeroLine:true});
        el('h_tom').textContent = 'Nog geen morgenprijzen beschikbaar (meestal na 14:00).';
        el('d_tom').textContent = '-';
      } else {
        if(cardTom) cardTom.style.opacity = 1;
        drawBars(cTom, tomArr, {interval:tomInt, highlightIdx:-1, zeroLine:true});
        el('h_tom').textContent = lineMinAvgMax(tomArr);
        el('d_tom').textContent = detailsBlock(tomArr, tomInt);
      }

      // GAS – fetch max 1× per 10 minuten
      if(!window._gasFetchMs) window._gasFetchMs = 0;
      const nowMs = Date.now();
      if((nowMs - window._gasFetchMs) > 10*60*1000){
        window._gasFetchMs = nowMs;
        try{
          const rg = await fetch('/api/gas_history?ts='+nowMs, {cache:'no-store'});
          if(rg.ok){
            const g = await rg.json();

            const datesIso = g.date || [];
            const spot = g.spot_eur_m3 || [];

            // Gas-dag loopt 06:00–06:00 → label hoort bij startdatum (06:00) en mag dus niet "voorlopen".
            // API date[] is effectief de kalenderdatum van de *volgende* ochtend → label = date - 1 dag.
            const gasLabelsIso = (datesIso || []).map((iso)=>{
              try{
                if(!iso || String(iso).length < 10) return iso;
                const d = new Date(String(iso).slice(0,10) + 'T00:00:00');
                d.setDate(d.getDate() - 1);
                const y = d.getFullYear();
                const m = String(d.getMonth()+1).padStart(2,'0');
                const dd = String(d.getDate()).padStart(2,'0');
                return `${y}-${m}-${dd}`;
              }catch(e){ return iso; }
            });


            // Bouw "prijzen" reeks (EUR/m³)
            let prices = [];
            const src = Number(g.price_source ?? 0); // 0=spot, 1=vast
            if(src === 1){
              const fx = Number(g.fixed_eur_m3 ?? 0);
              prices = datesIso.map(()=>fx);
            } else {
              if (Boolean(g.total_default)) {
                const tax = Number(g.energy_tax_eur ?? 0) + Number(g.inkoop_eur ?? 0);
                const vat = 1 + (Number(g.btw_pct ?? 0) / 100);
                prices = spot.map(v => (Number(v) + tax) * vat);
              } else {
                prices = spot.map(v => Number(v));
              }
            }

            el('gas_upd').textContent = g.updatedAt || '-';

            // chart
            drawBars(cGas, prices, {interval:1440, highlightIdx:prices.length-1, unit:'EUR/m³', decimals:3, labels:gasLabelsIso});

            // info
            el('h_gas').textContent = lineMinAvgMaxUnit(prices, 'EUR/m³', 3);
            el('d_gas').textContent = detailsBlock(prices, 1440);
          }
        }catch(e){
          // ignore
        }
      }

    }catch(e){
      el('np_stat').textContent = 'Fout';
    }
  }

  tick();
  setInterval(tick, 30*1000);
  window.addEventListener('resize', tick);
})();
</script>
)JS";

  page += htmlFooter("prices");
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "text/html; charset=utf-8", page);
}


void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void registerWebRoutes() {
  server.on("/", HTTP_GET, handleDashboard);
  server.on("/control", HTTP_GET, handleControlPage);
  server.on("/legionella", HTTP_GET, handleLegionellaPage);
  server.on("/settings", HTTP_GET, handleSettingsPage);
  server.on("/prices", HTTP_GET, handlePricesPage);

  server.on("/control/toggle_force", HTTP_POST, handleToggleForce);
  server.on("/control/save_limits", HTTP_POST, handleControlSaveLimits);
  server.on("/control/save_temp_gate", HTTP_POST, handleControlSaveTemperatureGate);

  server.on("/legionella/start", HTTP_POST, handleLegionellaStart);
  server.on("/legionella/cancel", HTTP_POST, handleLegionellaCancel);
  server.on("/legionella/save", HTTP_POST, handleLegionellaSave);
  server.on("/legionella/reset_success", HTTP_POST, handleLegionellaResetSuccess);

  server.on("/relay/toggle", HTTP_POST, handleRelayToggle);

  server.on("/settings/save_reboot", HTTP_POST, handleSettingsSaveReboot);
  server.on("/settings/save", HTTP_POST, handleSettingsSave);
  server.on("/settings/save_sensors", HTTP_POST, handleSettingsSaveSensors);
  server.on("/settings/ha_mqtt_cleanup", HTTP_POST, handleSettingsHaMqttCleanup);
  server.on("/settings/save", HTTP_POST, handleSettingsSave);

  server.onNotFound(handleNotFound);
}
