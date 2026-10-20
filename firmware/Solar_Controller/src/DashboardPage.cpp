#include "DashboardPage.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>

#include "WebRoutes.h"
#include "WebUtils.h"
#include "app_globals.h"
#include "collector.h"
#include "HealthMonitor.h"
#include "MeterManager.h"
#include "MultiController.h"
#include "RelayControl.h"
#include "StatusLabels.h"
#include "sensors.h"
#include "TimeSchedule.h"

extern WebServer server;
extern long lastNetPowerW;
extern bool dashShowSunWindowLabel;

static String dashboardModeLabel(const String& raw) {
  if (raw == "NORMAL_OFF") return "Zonneregeling uit";
  if (raw == "NORMAL_BLOCKED") return "Wacht op tijdvenster";
  if (raw == "NORMAL") return "Zonregeling";
  if (raw == "FORCE_HEAT") return "Handmatig verwarmen";
  if (raw == "PRICE_BOOST") return "Prijs-boost";
  if (raw == "LEGIONELLA") return "Legionella actief";
  return raw;
}

static const char DASH_LIVE_SCRIPT[] PROGMEM = R"JS(
<script>
(() => {
  const el = (id) => document.getElementById(id);

const minToHm = (min) => {
  if (min === null || min === undefined) return '';
  let v = parseInt(min, 10);
  if (isNaN(v)) return '';
  v = ((v % 1440) + 1440) % 1440;
  const h = Math.floor(v / 60);
  const m = v % 60;
  return String(h).padStart(2,'0') + ':' + String(m).padStart(2,'0');
};


  // ===== Live KPI's (licht) =====
  const netEl   = el('net_power_w');
  const dutyEl  = el('duty_percent');
  const errEl   = el('error_w');
  const ovrEl   = el('overshoot_w');
  const pwmBar  = el('pwm_bar');
  const pwmRingEl = el('pwmRing');
  const heatExecActiveRow = el('heat_exec_active_row');
  // Fancy dashboard elements (optional)
  const zeroBadge = el('zero_badge');
  const mfArrow   = el('mf_arrow');
  const netBigEl  = el('net_power_big');
  const mcRoleBadgeEl = el('mc_role_badge');

  // (step80) Fase-ampères (totaal) onder Netvermogen (alleen zichtbaar bij P1 meter)
  const phaseRow = el('phase_row');
  const phL1 = el('ph_l1');
  const phL2 = el('ph_l2');
  const phL3 = el('ph_l3');
  const phBox1 = el('ph_box_1');
  const phBox2 = el('ph_box_2');
  const phBox3 = el('ph_box_3');
  const mcActiveRow = el('mc_active_row');


  // (step52) UI state cache (set by status tick)
  let lastLegState = '';
  let lastHeatAdvice = '';
  let lastControlMode = '';

  function displayModeLabel(v){
    const s = String(v || '');
    if (s === 'NORMAL_OFF') return 'Zonneregeling uit';
    if (s === 'NORMAL_BLOCKED') return 'Wacht op tijdvenster';
    if (s === 'NORMAL') return 'Zonregeling';
    if (s === 'FORCE_HEAT') return 'Handmatig verwarmen';
    if (s === 'PRICE_BOOST') return 'Prijs-boost';
    if (s === 'LEGIONELLA') return 'Legionella actief';
    return s;
  }


  // ===== Overige status (zwaarder) =====
  const meterEl = el('meter_ok');
  const modeEl  = el('control_mode');
  const algEl   = el('normal_alg');
  const legEl   = el('leg_state');
  const sunDlEl = el('sun_dl');
  const sunWinEl= el('sun_win');

  // Temperaturen (compact)
  const tempsCard = el('temps_card');
  const tempsList = el('temps_list');

  const dotWiFi = el('dot_wifi');
  const txtWiFi = el('txt_wifi');
  const dotMQTT = el('dot_mqtt');
  const txtMQTT = el('txt_mqtt');
  const dotMeter= el('dot_meter');
  const txtMeter= el('txt_meter');

  const updEl   = el('last_update');
  const updBadge= el('last_update_badge');

  // Refresh UI controls (Dashboard verversing)
  const selLive  = el('sel_live_ms');
  const selStat  = el('sel_status_ms');
  const chkPause = el('chk_pause_hidden');

  // Sparklines (browser-only; ESP32 load komt vooral door fetch frequentie)
  const cNet = el('spark_net');
  const cPwm = el('spark_pwm');

  const BUF = 60;
  const netSeries = [];
  const pwmSeries = [];

  let liveTimer = null;
  let statTimer = null;

  // LocalStorage keys (client-side, snel en geen NVS nodig)
  const LS_LIVE  = 'sc_dash_live_ms';
  const LS_STAT  = 'sc_dash_status_ms';
  const LS_PAUSE = 'sc_dash_pause_hidden';

  const setDot = (dotEl, ok) => dotEl && dotEl.classList.toggle('bad', !ok);

  function nowTxt(){
    try { return new Date().toLocaleTimeString('nl-NL'); }
    catch(e){ return new Date().toLocaleTimeString(); }
  }
  function clamp(x, a, b){ return Math.max(a, Math.min(b, x)); }

  function pushSeries(arr, v){
    arr.push(v);
    if (arr.length > BUF) arr.shift();
  }

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

  function cssVar(name, fallback){
    try{
      var v = "";
      if(document.body){
        v = getComputedStyle(document.body).getPropertyValue(name);
      }
      if(!v || !v.trim()){
        v = getComputedStyle(document.documentElement).getPropertyValue(name);
      }
      return (v && v.trim()) ? v.trim() : fallback;
    }catch(e){ return fallback; }
  }

  function drawSpark(c, arr, opt){
    if(!c) return;
    const ctx = c.getContext('2d');
    const w = c.width, h = c.height;
    ctx.clearRect(0,0,w,h);
    if(arr.length < 2) return;

    // Theme kleuren (UI-only, geen ESP impact)
    const colAccent  = cssVar('--accent',  '#3b82f6');
    const colAccent2 = cssVar('--accent2', '#22c55e');
    const colMuted   = cssVar('--muted',   'rgba(255,255,255,0.65)');
    const colBorder  = cssVar('--border',  'rgba(255,255,255,0.10)');

    // Plot padding: ruimte voor Y-as labels links
    const leftPad   = 44;
    const rightPad  = 10;
    const topPad    = 10;
    const bottomPad = 14;

    const pw = Math.max(10, w - leftPad - rightPad);
    const ph = Math.max(10, h - topPad - bottomPad);

    // Schaal (liefst vast voor leesbare Y-as)
    let min = (opt && typeof opt.fixedMin === 'number') ? opt.fixedMin : Math.min(...arr);
    let max = (opt && typeof opt.fixedMax === 'number') ? opt.fixedMax : Math.max(...arr);

    // ===== Auto symmetric Y-range (browser-side, zero impact on ESP) =====
    // Intended for net-power graphs so the range follows the data but doesn't jitter.
    if(opt && opt.autoSymRange){
      let maxAbs = 0;
      for(let i=0;i<arr.length;i++){
        const v = arr[i];
        const a = Math.abs((typeof v === 'number') ? v : 0);
        if(a > maxAbs) maxAbs = a;
      }
      const headroom = (typeof opt.headroom === 'number') ? opt.headroom : 1.15;
      const minScale = (typeof opt.minScale === 'number') ? opt.minScale : 500;

      function niceCeil(x){
        if(!(x > 0)) return minScale;
        const exp = Math.floor(Math.log10(x));
        const f = x / Math.pow(10, exp);
        let nf;
        if(f <= 1) nf = 1;
        else if(f <= 2) nf = 2;
        else if(f <= 5) nf = 5;
        else nf = 10;
        return nf * Math.pow(10, exp);
      }

      const target = Math.max(minScale, maxAbs * headroom);
      const nice = niceCeil(target);

      // Sticky scaling: grow fast, shrink only after cooldown and with meaningful drop
      const cooldownFrames = (typeof opt.cooldownFrames === 'number') ? opt.cooldownFrames : 12;
      if(typeof opt._scale !== 'number'){
        opt._scale = nice;
        opt._cooldown = cooldownFrames;
      }

      if(nice > opt._scale){
        opt._scale = nice;
        opt._cooldown = cooldownFrames;
      } else {
        if((opt._cooldown|0) > 0){
          opt._cooldown = (opt._cooldown|0) - 1;
        } else if(nice <= opt._scale * 0.7){
          opt._scale = nice;
          opt._cooldown = cooldownFrames;
        }
      }

      min = -opt._scale;
      max =  opt._scale;

      // Auto ticks if none provided (keeps UI clean + informative)
      if(!Array.isArray(opt.yTicks) || !opt.yTicks.length){
        opt._autoTicks = [min, min/2, 0, max/2, max].map(v => Math.round(v));
      } else {
        opt._autoTicks = null;
      }
    } else if(opt) {
      opt._autoTicks = null;
    }
    if (min === max){ min -= 1; max += 1; }

    // Als geen vaste schaal: klein beetje padding zodat het niet "plakt"
    if(!(opt && typeof opt.fixedMin === 'number' && typeof opt.fixedMax === 'number')){
      const pad = (max - min) * 0.10;
      min -= pad; max += pad;
    }

    const toY = (v) => {
      const t = (v - min) / (max - min);
      const tn = clamp(t, 0, 1);
      return topPad + (1 - tn) * ph;
    };

    // ===== Grid + Y ticks =====
    const ticks = (opt && (Array.isArray(opt.yTicks) && opt.yTicks.length ? opt.yTicks : (Array.isArray(opt._autoTicks) && opt._autoTicks.length ? opt._autoTicks : null))) || null;

    ctx.save();
    ctx.lineWidth = 1;
    ctx.strokeStyle = colBorder;
    if(ticks){
      for(const tv of ticks){
        const y = toY(tv);
        // gridline in plot area
        ctx.beginPath();
        ctx.moveTo(leftPad, y);
        ctx.lineTo(leftPad + pw, y);
        ctx.stroke();
      }
    }else{
      // fallback: 3 gridlines
      for(let i=1;i<=3;i++){
        const y = topPad + (ph * i/4);
        ctx.beginPath();
        ctx.moveTo(leftPad, y);
        ctx.lineTo(leftPad + pw, y);
        ctx.stroke();
      }
    }
    ctx.restore();

    // ===== Zero line (Netvermogen) =====
    if(opt && opt.zeroLine && min < 0 && max > 0){
      const y0 = toY(0);
      ctx.save();
      ctx.lineWidth = 1.5;
      ctx.strokeStyle = colMuted;
      ctx.globalAlpha = 0.55;
      ctx.beginPath();
      ctx.moveTo(leftPad, y0);
      ctx.lineTo(leftPad + pw, y0);
      ctx.stroke();
      ctx.restore();
    }

    // ===== Y-as labels =====
    if(ticks){
      ctx.save();
      const cssW = (c && c.clientWidth) ? c.clientWidth : (c && c.parentElement ? c.parentElement.clientWidth : 360);
      const fontPx = (cssW <= 420) ? 14 : 12;
      ctx.font = fontPx + "px system-ui, -apple-system, Segoe UI, Roboto, sans-serif";
      ctx.fillStyle = colMuted;
      ctx.textAlign = "right";
      ctx.textBaseline = "middle";
      for(const tv of ticks){
        const y = toY(tv);
        ctx.fillText(String(tv), leftPad - 8, y);
        // klein tick-streepje
        ctx.globalAlpha = 0.6;
        ctx.strokeStyle = colMuted;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(leftPad - 4, y);
        ctx.lineTo(leftPad, y);
        ctx.stroke();
        ctx.globalAlpha = 1;
      }
      ctx.restore();
    }

    // ===== Data path =====
    const n = arr.length;
    const step = pw / (n - 1);

    // Lijn gradient (modern)
    const grad = ctx.createLinearGradient(leftPad, 0, leftPad + pw, 0);
    grad.addColorStop(0, colAccent);
    grad.addColorStop(1, colAccent2);

    // Area fill gradient (subtiel)
    const fillGrad = ctx.createLinearGradient(0, topPad, 0, topPad + ph);
    fillGrad.addColorStop(0, colAccent);
    fillGrad.addColorStop(1, 'rgba(0,0,0,0)');

    // Path maken
    ctx.save();
    ctx.beginPath();
    for(let i=0;i<n;i++){
      const x = leftPad + i * step;
      const y = toY(arr[i]);
      if(i===0) ctx.moveTo(x,y);
      else ctx.lineTo(x,y);
    }

    // Fill onder de lijn (premium look)
    ctx.lineTo(leftPad + (n-1)*step, topPad + ph);
    ctx.lineTo(leftPad, topPad + ph);
    ctx.closePath();
    ctx.globalAlpha = 0.14;
    ctx.fillStyle = fillGrad;
    ctx.fill();
    ctx.restore();

    // Lijn zelf
    ctx.save();
    ctx.beginPath();
    for(let i=0;i<n;i++){
      const x = leftPad + i * step;
      const y = toY(arr[i]);
      if(i===0) ctx.moveTo(x,y);
      else ctx.lineTo(x,y);
    }
    ctx.lineWidth = 2.2;
    ctx.strokeStyle = grad;
    ctx.lineJoin = "round";
    ctx.lineCap  = "round";
    // lichte glow
    ctx.shadowBlur = 8;
    ctx.shadowColor = colAccent;
    ctx.globalAlpha = 0.95;
    ctx.stroke();
    ctx.restore();

    // Border rond plotgebied (subtiel)
    ctx.save();
    ctx.globalAlpha = 0.35;
    ctx.strokeStyle = colBorder;
    ctx.lineWidth = 1;
    ctx.strokeRect(leftPad + 0.5, topPad + 0.5, pw - 1, ph - 1);
    ctx.restore();
  }

  function redraw(){
    resizeCanvas(cNet);
    resizeCanvas(cPwm);
    drawSpark(cNet, netSeries, { zeroLine:true, autoSymRange:true, headroom:1.15, minScale:500, cooldownFrames:12 });
    drawSpark(cPwm, pwmSeries, { zeroLine:false, fixedMin:0, fixedMax:100, yTicks:[0,25,50,75,100] });
  }
  window.addEventListener('resize', redraw);

  function setNetColor(v){
    if(!netEl) return;
    netEl.classList.remove('pos','neg');
    if (v < 0) netEl.classList.add('neg');
    else if (v > 0) netEl.classList.add('pos');
  }

  function mcRoleLabel(role){
    const v = String(role || '').toLowerCase();
    if (v === 'master') return 'Master';
    if (v === 'slave') return 'Slave';
    return 'Standalone';
  }

  function syncMcRoleBadge(role){
    if (!mcRoleBadgeEl) return;
    const v = String(role || '').toLowerCase();
    mcRoleBadgeEl.textContent = mcRoleLabel(v);
    mcRoleBadgeEl.classList.toggle('gray', v !== 'master' && v !== 'slave');
  }

  // ===============================
  // LIVE tick (licht): /api/live
  // ===============================
  function renderMcActiveSlots(j) {
    if (!mcActiveRow) return;
    const mcOn = (j && j.mc_enabled === true);
    const arr = (j && Array.isArray(j.mc_dashboard_slots)) ? j.mc_dashboard_slots : ((j && Array.isArray(j.mc_local_pwm)) ? j.mc_local_pwm : []);
    if (!mcOn || !arr.length) {
      mcActiveRow.style.display = 'none';
      mcActiveRow.innerHTML = '';
      return;
    }
    mcActiveRow.innerHTML = arr.map((s) => {
      const nm = String((s && s.name) ? s.name : ('Controller ' + String(Number(s && s.slot) || 0))).replace(/[&<>"']/g, (c) => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
      const pct = Number(s && s.duty_percent);
      const onCls = pct > 0.05 ? ' isOn' : '';
      return '<div class="mcActiveChip' + onCls + '"><span class="mcName">' + nm + '</span><span class="mcSep">·</span><span class="mcPct">' + pct.toFixed(1) + '%</span></div>';
    }).join('');
    mcActiveRow.style.display = 'flex';
  }

  async function tickLive() {
    try {
      const r = await fetch('/api/live', { cache: 'no-store' });
      if (!r.ok) throw new Error('HTTP ' + r.status);
      const j = await r.json();

      if (netEl && j.net_power_w !== undefined) {
        const nv = Number(j.net_power_w);
        netEl.textContent = j.net_power_w;
        setNetColor(nv);
        pushSeries(netSeries, nv);
      }

      if (dutyEl && j.duty_percent !== undefined) {
        const d = Number(j.duty_percent);
        dutyEl.textContent = d.toFixed(1);
        if (pwmBar) pwmBar.style.width = clamp(d,0,100).toFixed(1) + '%';
        if (pwmRingEl) {
          const v = clamp(d,0,100);
          pwmRingEl.style.setProperty('--pwm', v);
          pwmRingEl.classList.toggle('isActive', v > 0.2);
          pwmRingEl.classList.toggle('isMax', v >= 99.8);
        }
        pushSeries(pwmSeries, d);
      }

      if (errEl && j.error_w !== undefined) errEl.textContent = Number(j.error_w).toFixed(0);
      if (ovrEl && j.overshoot_w !== undefined) ovrEl.textContent = Number(j.overshoot_w).toFixed(0);

      // Fancy UI sync (non-breaking if elements not present)
      if (netBigEl && j.net_power_w !== undefined) {
        netBigEl.textContent = Number(j.net_power_w).toFixed(0) + ' W';
        netBigEl.classList.toggle('neg', Number(j.net_power_w) > 0);
        netBigEl.classList.toggle('pos', Number(j.net_power_w) < 0);
      }

      renderMcActiveSlots(j);

      // (step161) Multi Controller-slots tonen nu stabiel alle actieve lokale slots, ook bij 0.0% PWM.
      // Deze chips staan direct boven de faseblokken.
      if (phaseRow) {
        const ok = (j.phases_ok === true);
        phaseRow.style.display = ok ? '' : 'none';
        if (ok) {
          if (phL1 && j.i1_a !== undefined) phL1.textContent = Number(j.i1_a).toFixed(1);
          if (phL2 && j.i2_a !== undefined) phL2.textContent = Number(j.i2_a).toFixed(1);
          if (phL3 && j.i3_a !== undefined) phL3.textContent = Number(j.i3_a).toFixed(1);

          // (step88) Highlight gekozen fase (thema-kleur) als fase-beveiliging aan staat
          const hl = (j.pg_en === true);
          const ph = Number(j.pg_ph || 0);
          if (phBox1) phBox1.classList.toggle('isSel', hl && ph === 1);
          if (phBox2) phBox2.classList.toggle('isSel', hl && ph === 2);
          if (phBox3) phBox3.classList.toggle('isSel', hl && ph === 3);
        }
      }


      if (zeroBadge && mfArrow) {
        const d = (j.duty_percent !== undefined) ? Number(j.duty_percent) : 0;
        const e = (j.error_w !== undefined) ? Number(j.error_w) : 0;
        const near = Math.abs(e) <= 50;          // "perfect" band
        const pwmOn = d > 0.05;
        const pwmMax = d >= 99.5;

        // Legionella detection (from status tick)
        const ls = String(lastLegState || '').toUpperCase();
        const legActive = (ls && ls !== 'IDLE' && ls !== '-' && !ls.includes('OFF'));

        // Arrow state (idle / active / max)
        mfArrow.classList.toggle('idle', !pwmOn);
        mfArrow.classList.toggle('active', pwmOn && !pwmMax);
        mfArrow.classList.toggle('max', pwmOn && pwmMax);

        // Badge color classes
        zeroBadge.classList.remove('good','warn','bad');

        // 1) Legionella overrides everything
        if (legActive) {
          zeroBadge.textContent = 'Legionella actief';
          zeroBadge.classList.add('bad');
        }
        else if (lastControlMode === 'NORMAL_OFF') {
          zeroBadge.textContent = 'Zonneregeling uit';
          zeroBadge.classList.add('warn');
        }
        else if (lastControlMode === 'NORMAL_BLOCKED') {
          zeroBadge.textContent = 'Wacht op tijdvenster';
          zeroBadge.classList.add('warn');
        }
        // 2) PWM off: zero-export not running (show simple truth)
        else if (!pwmOn) {
          // If you're already near perfect without heating, show it as a positive
          if (near) {
            zeroBadge.textContent = 'Perfect uitgebalanceerd';
            zeroBadge.classList.add('good');
          } else {
            zeroBadge.textContent = 'Zero-export UIT';
            zeroBadge.classList.add('warn');
          }
        }
        // 3) PWM on: we are trying to balance
        else {
          if (near) {
            zeroBadge.textContent = 'Perfect uitgebalanceerd';
            zeroBadge.classList.add('good');
          } else if (pwmMax && e < -50) {
            // Still importing while PWM is maxed out
            zeroBadge.textContent = 'Maximaal bereikbaar';
            zeroBadge.classList.add('bad');
          } else if (e < -50) {
            // Need more (typically: still importing)
            zeroBadge.textContent = 'Nog ' + Math.round(Math.abs(e)) + ' W nodig';
            zeroBadge.classList.add('warn');
          } else {
            // General case: active balancing
            zeroBadge.textContent = 'Zero-export actief';
            zeroBadge.classList.add('good');
          }
        }
      }


      if (updEl) updEl.textContent = 'Laatst bijgewerkt: ' + nowTxt();
      if (updBadge) { updBadge.textContent = 'OK'; updBadge.className = 'ok'; }

      redraw();
    } catch (e) {
      if (updEl) updEl.textContent = 'Laatst bijgewerkt: ' + nowTxt();
      if (updBadge) { updBadge.textContent = 'GEEN VERBINDING'; updBadge.className = 'bad'; }
    }
  }

  // ===================================
  // STATUS tick (licht): /api/status_light
  // ===================================
  async function tickStatus() {
    try {
      const r = await fetch('/api/status_light', { cache: 'no-store' });
      if (!r.ok) throw new Error('HTTP ' + r.status);
      const j = await r.json();

      if (meterEl && j.meter_ok !== undefined) meterEl.textContent = (j.meter_ok ? 'OK' : 'GEEN DATA');
      if (j.control_mode !== undefined) {
        lastControlMode = String(j.control_mode || '');
        if (modeEl) modeEl.textContent = displayModeLabel(lastControlMode);
      }
      if (algEl && j.normal_algorithm !== undefined) algEl.textContent = j.normal_algorithm;
      if (j.mc_role !== undefined) syncMcRoleBadge(j.mc_role);

// Daylight / Cheapest label
if (sunDlEl && j.sun_daylight_start_min !== undefined && j.sun_daylight_end_min !== undefined) {
  sunDlEl.textContent = minToHm(j.sun_daylight_start_min) + "–" + minToHm(j.sun_daylight_end_min);
}
if (sunWinEl && j.sun_sched_mode !== undefined) {
  if (parseInt(j.sun_sched_mode,10) === 2 && j.sun_cheapest_start_min !== undefined && j.sun_cheapest_end_min !== undefined) {
    sunWinEl.textContent = "Cheapest " + minToHm(j.sun_cheapest_start_min) + "–" + minToHm(j.sun_cheapest_end_min);
  } else {
    // In handmatige modus laten we gewoon de server-waarde staan (Window HH:MM–HH:MM)
  }
}

      if (legEl && j.legionella_state !== undefined) legEl.textContent = j.legionella_state;
      if (j.legionella_state !== undefined) lastLegState = String(j.legionella_state||'');

      // Advies Gas vs Elektra (dashboard)
      const heatAdviceEl = document.getElementById('heat_advice');
      const heatGasEl    = document.getElementById('heat_gas_ct');
      const heatElecEl   = document.getElementById('heat_elec_ct');
      if (heatAdviceEl && j.heat_recommend !== undefined){
        const raw = String(j.heat_recommend||'').toUpperCase();
        const v = (raw === 'UNKNOWN') ? 'ONBEKEND' : raw;
        heatAdviceEl.textContent = v || '-';
        lastHeatAdvice = v || '';
      }
      if (heatGasEl && j.heat_gas_ct_kwh_heat !== undefined){
        const n = Number(j.heat_gas_ct_kwh_heat);
        heatGasEl.textContent = (j.heat_gas_ct_kwh_heat === null || !Number.isFinite(n)) ? '-' : n.toFixed(2);
      }
      if (heatElecEl && j.heat_elec_ct_kwh_heat !== undefined){
        const n = Number(j.heat_elec_ct_kwh_heat);
        heatElecEl.textContent = (j.heat_elec_ct_kwh_heat === null || !Number.isFinite(n)) ? '-' : n.toFixed(2);
      }
      if (heatExecActiveRow){
        heatExecActiveRow.style.display = (j.heat_exec_active ? 'block' : 'none');
      }

      const wifiOk = !!j.wifi_connected;
      setDot(dotWiFi, wifiOk);
      if (txtWiFi) {
        const rssi = (j.wifi_rssi_dbm !== undefined) ? ` • ${j.wifi_rssi_dbm} dBm` : '';
        txtWiFi.textContent = wifiOk ? ('WiFi OK' + rssi) : 'WiFi OFF';
      }

      const me = !!j.mqtt_enabled;
      const mc = !!j.mqtt_connected;
      setDot(dotMQTT, (me && mc));
      if (txtMQTT) txtMQTT.textContent = me ? (mc ? 'MQTT ON' : 'MQTT OFF') : 'MQTT UIT';

      if (dotMeter) setDot(dotMeter, !!j.meter_ok);
      if (txtMeter && j.meter_label) txtMeter.textContent = 'Meter: ' + j.meter_label;


      // Temperaturen (compact)
      if (tempsCard && tempsList) {
        const arr = j.temps;
        if (Array.isArray(arr) && arr.length) {
          tempsCard.style.display = '';
          // rebuild chips
          while (tempsList.firstChild) tempsList.removeChild(tempsList.firstChild);
          for (const it of arr) {
            if (!it) continue;
            const name = (it.name !== undefined && it.name !== null && String(it.name).length) ? String(it.name) : 'Temp';
            const tval = (it.t !== undefined && it.t !== null && !isNaN(parseFloat(it.t))) ? (parseFloat(it.t).toFixed(1) + ' °C') : '—';
            const c = document.createElement('div');
            c.className = 'chip';
            const s1 = document.createElement('span');
            s1.textContent = name + ': ';
            const s2 = document.createElement('b');
            s2.textContent = tval;
            c.appendChild(s1);
            c.appendChild(s2);
            tempsList.appendChild(c);
          }
        } else {
          // Laat kaart staan (server-rendered fallback); toon alleen een hint als er nog niets staat.
          if (!tempsList.firstChild) {
            tempsCard.style.display = '';
            const h = document.createElement('div');
            h.className = 'help';
            h.innerHTML = '<b>Geen temperatuurdata.</b> (nog) niet beschikbaar.';
            tempsList.appendChild(h);
          }
        }
      }

    } catch (e) {
      // status is "nice to have"; bij fout laten we de laatste waarden staan
    }
  }

  function readInt(v, fallback){
    const n = parseInt(v, 10);
    return Number.isFinite(n) ? n : fallback;
  }

  function stopTimers(){
    if (liveTimer) { clearInterval(liveTimer); liveTimer = null; }
    if (statTimer) { clearInterval(statTimer); statTimer = null; }
  }

  function shouldPause(){
    return !!(chkPause && chkPause.checked && document.hidden);
  }

  function startTimers(){
    stopTimers();
    if (shouldPause()) return;

    const liveMs  = selLive ? readInt(selLive.value, 1000) : 1000;
    const statMs  = selStat ? readInt(selStat.value, 5000) : 5000;

    // Live is altijd actief (behalve wanneer gepauzeerd)
    tickLive();
    liveTimer = setInterval(tickLive, liveMs);

    // Status kan uitgezet worden
    if (statMs > 0) {
      tickStatus();
      statTimer = setInterval(tickStatus, statMs);
    }
  }

  function loadUiSettings(){
    // Defaults: Live=1000ms, Status=5000ms, Pause=true
    const liveMs  = readInt(localStorage.getItem(LS_LIVE), 1000);
    const statMs  = readInt(localStorage.getItem(LS_STAT), 5000);
    const pauseOn = (localStorage.getItem(LS_PAUSE) === null) ? true : (localStorage.getItem(LS_PAUSE) === '1');

    if (selLive)  selLive.value = String(liveMs);
    if (selLive && ![...selLive.options].some(o => o.value === selLive.value)) selLive.value = '1000';
    if (selStat)  selStat.value = String(statMs);
    if (chkPause) chkPause.checked = !!pauseOn;
  }

  function saveUiSettings(){
    if (selLive) localStorage.setItem(LS_LIVE, String(selLive.value));
    if (selStat) localStorage.setItem(LS_STAT, String(selStat.value));
    if (chkPause) localStorage.setItem(LS_PAUSE, chkPause.checked ? '1' : '0');
  }

  // Wire UI events
  if (selLive) selLive.addEventListener('change', () => { saveUiSettings(); startTimers(); });
  if (selStat) selStat.addEventListener('change', () => { saveUiSettings(); startTimers(); });
  if (chkPause) chkPause.addEventListener('change', () => { saveUiSettings(); startTimers(); });

  document.addEventListener('visibilitychange', () => startTimers());

  // Boot
  loadUiSettings();
  startTimers();
})();
</script>
)JS";


void handleDashboard() {
  String page = htmlHeader("Overzicht");

  // (UI) Top tabs verwijderd – bottom nav is genoeg


  // page += topTabs("dash");
page += "<div class='tiny'><span id='last_update'>Laatst bijgewerkt: -</span> • <span id='last_update_badge' class='ok'>OK</span></div>";

  page += "<div class='chips'>";

  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  page += "<div class='chip'><span class='dot";
  if (!wifiOk) page += " bad";
  page += "' id='dot_wifi'></span><span id='txt_wifi'>";
  page += String("WiFi ") + (wifiOk ? "OK" : "OFF");
  if (wifiOk) page += String(" • ") + String(WiFi.RSSI()) + " dBm";
  page += "</span></div>";

  bool mqttOk = (!mqttEnabled) ? true : mqttClient.connected();
  page += "<div class='chip'><span class='dot";
  if (!mqttOk) page += " bad";
  page += "' id='dot_mqtt'></span><span id='txt_mqtt'>";
  if (!mqttEnabled) page += "MQTT UIT";
  else page += (mqttClient.connected() ? "MQTT ON" : "MQTT OFF");
  page += "</span></div>";

  page += "<div class='chip'><span class='dot' id='dot_meter'></span><span id='txt_meter'>";
  page += "Meter: " + meterLabel();
  page += "</span></div>";


// Daylight / Cheapest label (optioneel)
if (dashShowSunWindowLabel && (sunSchedMode == 1 || sunSchedMode == 2)) {
  String dl = fmtHmMin(sunDaylightStartMin) + "–" + fmtHmMin(sunDaylightEndMin);
  String win;
  if (sunSchedMode == 2) win = String("Cheapest ") + fmtHmMin(sunCheapestStartMin) + "–" + fmtHmMin(sunCheapestEndMin);
  else                   win = String("Window ")   + fmtHmMin(sunWinStartMin)       + "–" + fmtHmMin(sunWinEndMin);
  page += "<div class='tiny' id='sun_window_line'>Daylight <span id='sun_dl'>" + dl + "</span> / <span id='sun_win'>" + win + "</span></div>";
}

  page += "</div>";

  page += "<div class='card fancy'><div class='hd'><div style='display:flex;align-items:center;gap:8px'><b>Live status</b><span class='tag";
  if (mcRole == MC_ROLE_STANDALONE) page += " gray";
  page += "' id='mc_role_badge'>";
  if (mcRole == MC_ROLE_MASTER) page += "Master";
  else if (mcRole == MC_ROLE_SLAVE) page += "Slave";
  else page += "Standalone";
  page += "</span></div><span class='tag'>";
  page += configMode ? "CONFIG" : "RUN";
  page += "</span></div><div class='bd'>";

  // Mode / state row (fancy)
  page += "<div class='fRow'><span class='fDot'></span><div class='fMode' id='control_mode'>";
  page += dashboardModeLabel(modeLabel());
  page += "</div><div class='tiny' style='margin-left:6px' id='normal_alg'>";
  page += normalAlgLabel();
  page += "</div><div class='fState'><span class='runDot'></span><span>RUN</span></div></div>";

  // Meter -> Zero-export flow (full width)
  page += "<div class='meterFlow'>";
  page += "<div class='mfTop'><div class='mfLbl'>Meter</div><div class='mfDelta'><span id='error_w'>" + String((int)lround(lastErrorW)) + "</span> W</div><div class='tiny' id='leg_state'>" + legStateLabel() + "</div></div>";
  page += "<div class='mfMid'>";
  page += "<div class='mfVal' id='net_power_w'>" + String(lastNetPowerW) + "</div>";
  page += "<div id='mf_arrow' class='mfArrow idle'><div class='mfLine'></div><div class='mfHead'></div></div>";
  page += "<div id='zero_badge' class='mfBadge warn'>Zero-export UIT</div>";
  page += "</div>";
  page += "<div class='mfBottom'>";
  page += "<div class='mfChip'>Regeloverschot: <span id='overshoot_w'>" + String((int)lround(lastOvershootW)) + "</span> W</div>";
  page += "<div class='mfChip'>Meter: <span id='meter_ok'>" + String(lastMeterOk ? "OK" : "GEEN DATA") + "</span></div>";
  page += "</div></div>";

  // Advice (gas vs elec)
  page += "<div class='advBox'>";
  page += "<div class='advMain'><span id='heat_advice'>-</span><span class='pill'>advies</span></div>";
  page += "<div class='advSub'>Gas: <span id='heat_gas_ct'>-</span> ct/kWh warmte • Elektra: <span id='heat_elec_ct'>-</span> ct/kWh warmte</div>";
  page += "</div>";

  // KPI + PWM
  page += "<div class='kpiGrid'>";
  page += "<div class='kpiBox'>";
  page += "<div class='kpiTitle'>Netvermogen</div><div class='kpiBig' id='net_power_big'>" + String(lastNetPowerW) + " W</div>";
  page += "<div style='height:10px'></div>";
  page += "<div class='kpiTitle'>Boiler PWM</div>";
  page += "<div class='bar'><div id='pwm_bar' class='fill' style='width:" + String(lastDutyPercent,1) + "%'></div></div>";
  page += "<div id='heat_exec_active_row' style='display:none;margin-top:10px;font-size:14px;font-weight:800;color:var(--accent2,#22c55e)'>Verwarmen via advies: ACTIEF</div>";
  page += "</div>";
  page += "<div class='pwmRing' id='pwmRing' style='--pwm:" + String(lastDutyPercent,1) + ";'><div class='pct'><span id='duty_percent'>" + String(lastDutyPercent, 1) + "</span>%</div></div>";
  page += "</div>";

  // (step161) Actieve lokale controllers — zichtbaar als Multi Controller aan staat, ook bij 0.0% op actieve lokale slots.
  page += "<div class='mcActiveRow' id='mc_active_row' style='display:none'></div>";

  // (step81) Fase-ampères (totaal) — eigen kaders onder Netvermogen, boven Acties
  page += "<div class='phaseGrid' id='phase_row' style='display:none'>";
  page += "<div class='phaseBox' id='ph_box_1'><div class='phaseLbl'>L1</div><div class='phaseVal'><span id='ph_l1'>-</span><span class='phaseUnit'>A</span></div></div>";
  page += "<div class='phaseBox' id='ph_box_2'><div class='phaseLbl'>L2</div><div class='phaseVal'><span id='ph_l2'>-</span><span class='phaseUnit'>A</span></div></div>";
  page += "<div class='phaseBox' id='ph_box_3'><div class='phaseLbl'>L3</div><div class='phaseVal'><span id='ph_l3'>-</span><span class='phaseUnit'>A</span></div></div>";
  page += "</div>";

  // ===== Dashboard acties (compact, eigen kader binnen Live status) =====
  // Indeling (step64): Forceer verwarmen + Legionella programma direct onder de live KPI's,
  // binnen hetzelfde grote 'Live status' kader, maar wel in een eigen sub-kader (zelfde stijl).
  page += "<div class='actionCard'>";

  // Forceer verwarmen (toggle)
  page += "<form method='POST' action='/control/toggle_force'>";
  page += "<div class='row nb'><div class='k'>Forceer verwarmen</div><div class='v'>";
  page += String("<span class='switch ") + ((controlMode == MODE_FORCE_HEAT) ? "on" : "") + "' onclick='sw(this,true)'>";
  page += String("<input type='checkbox' ") + ((controlMode == MODE_FORCE_HEAT) ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";
  page += "</form>";

  page += "<div class='sep'></div>";

  // Legionella (toggle — zelfde switch UI)
  page += "<div class='row nb'><div class='k'>Legionella programma</div><div class='v'>";
  page += String("<span class='switch ") + (legRunning ? "on" : "") + "' onclick='legSw(this)'>";
  page += String("<input type='checkbox' ") + (legRunning ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";

  if (relayIsEnabled()) {
    page += "<div class='sep'></div>";
    page += "<div class='row nb'><div class='k'>Relais handmatig aan/uit</div><div class='v'>";
    page += String("<span class='switch ") + (relayGetManualOn() ? "on" : "") + "' onclick='relaySw(this)'>";
    page += String("<input type='checkbox' ") + (relayGetManualOn() ? "checked" : "") + "><span class='slider'></span></span>";
    page += "</div></div>";
  }

  page += "</div>"; // actionCard

  page += "</div></div>";



  
// ===== Temperatuur (compact) =====
// Zichtbaarheid: alleen zichtbaar als er minimaal 1 DS18B20 écht aanwezig is (enabled + present).
// Als er wel sensoren zijn maar nog geen geldige meetwaarde, tonen we een korte hint.
  bool showTempsCard = false;
  bool anyTempsValue = false;
  String tempsChips;
  if (tempSensorCount > 0) {
    for (int i = 0; i < tempSensorCount; i++) {
      if (!tempSensors[i].enabled) continue;
      if (!tempSensors[i].present) continue;
      showTempsCard = true;
      if (isnan(tempSensors[i].tempC)) continue;
      String nm = tempSensors[i].name.length() ? tempSensors[i].name : tempSensors[i].addrStr;
      tempsChips += "<div class='chip'>" + nm + ": <b>" + String(tempSensors[i].tempC, 1) + " °C</b></div>";
      anyTempsValue = true;
    }
  }
  if (showTempsCard) {
    page += "<div class='card' id='temps_card'>"
            "<div class='hd'><b>Temperaturen</b><span class='tag'>DS18B20</span></div>"
            "<div class='bd'><div id='temps_list' class='chips'>";
    if (anyTempsValue) {
      page += tempsChips;
    } else {
      page += "<div class='help'><b>Temperatuursensor(en) gevonden.</b> Wachten op data…</div>";
    }
    page += "</div></div></div>";
  }




    page += "<div class='card'><div class='hd'><b>Live grafiek (60s)</b><span class='tag'>auto</span></div><div class='bd'>";
  page += "<div class='k'>Netvermogen (W)</div>";
  page += "<div class='spark'><canvas id='spark_net'></canvas></div>";
  page += "<div style='height:12px'></div>";
  page += "<div class='k'>Boiler PWM (%)</div>";
  page += "<div class='spark'><canvas id='spark_pwm'></canvas></div>";
  page += "<div class='help'>Laatste 60 punten (1 per live-verversing). Voor tuning ga naar <b>Settings</b>.</div>";
  page += "</div></div>";

  // Dashboard acties zijn nu onderdeel van Live status (actionCard) — losse kaart verwijderd.

// (UI) Dashboard: technische info netjes inklapbaar houden (step62)
  page += "<details class='acc'><summary><div class='sum'><b>Informatie</b><span class='tag'>read-only</span></div></summary><div class='accbd'>";
  page += rowHtml("Firmware", FW_VERSION);
  page += rowHtml("Serial", collectorGetSerial());
  page += rowHtml("IP adres", (WiFi.status()==WL_CONNECTED) ? WiFi.localIP().toString() : String("-"));
  page += rowHtml("WiFi RSSI", String((WiFi.status()==WL_CONNECTED) ? WiFi.RSSI() : 0) + " dBm");
  page += rowHtml("Uptime", fmtUptimeDHMM((uint32_t)(millis()/1000UL)));
  page += rowHtml("Tijd (NTP)", timeIsValid() ? "OK" : "Nog niet gesynct");
  page += "<div class='help'>JSON status: <code>/api/status</code><br>System: <code>/api/system</code></div>";
			  page += "</div></details>";

	page += "<details class='acc' id='dash_refresh_card'><summary><div class='sum'><b>Dashboard verversing</b><span class='tag'>weergave</span></div></summary><div class='accbd'>";

  page += "<div class='row'><div class='k'>Live (PWM/Net/Fout/Overschot)</div><div class='v'>";
  page += "<select id='sel_live_ms'>";
  page += "<option value='1000'>1s</option>";
  page += "<option value='5000'>5s</option>";
  page += "<option value='10000'>10s</option>";
  page += "</select>";
		  page += "</div></div>";
  page += "<div class='help'>Te laag (1s) = meeste belasting op ESP32/WiFi. Dit maakt alleen de weergave sneller; de echte PWM-regeling blijft altijd live.</div>";

  page += "<div style='height:10px'></div>";

  page += "<div class='row'><div class='k'>Rest (volledige status)</div><div class='v'>";
  page += "<select id='sel_status_ms'>";
  page += "<option value='0'>Uit</option>";
  page += "<option value='5000'>5s</option>";
  page += "<option value='10000'>10s</option>";
  page += "<option value='30000'>30s</option>";
  page += "<option value='60000'>60s</option>";
  page += "</select>";
		  page += "</div></div>";
  page += "<div class='help'>Deze call is zwaarder (grotere JSON). Te laag instellen kan de webUI en ESP32 onnodig druk maken.</div>";

  page += "<div style='height:10px'></div>";

  page += "<div class='row'><div class='k'>Pauzeer bij verborgen tab</div><div class='v'>";
  page += "<span class='switch on' id='sw_pause_hidden'>";
  page += "<input id='chk_pause_hidden' type='checkbox' checked><span class='slider'></span></span>";
  page += "</div></div>";
  page += "<div class='help'>Aanbevolen: bespaart load wanneer je dashboard niet zichtbaar is (andere browser-tab, scherm uit, etc.).</div>";

	  page += "</div></details>";

  page += FPSTR(DASH_LIVE_SCRIPT);
  page += htmlFooter("dash");

  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "text/html; charset=utf-8", page);
}
