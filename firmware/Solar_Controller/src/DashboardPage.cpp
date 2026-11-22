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

void handleDashboard() {
  String page = htmlHeader("Overzicht", true);

  // (UI) Top tabs verwijderd – bottom nav is genoeg


page += "<div class='modern-context'>";
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
  page += "</div>";

  String roleBadge = "<span class='tag";
  if (mcRole == MC_ROLE_STANDALONE) roleBadge += " gray";
  roleBadge += "' id='mc_role_badge'>";
  if (mcRole == MC_ROLE_MASTER) roleBadge += "Master";
  else if (mcRole == MC_ROLE_SLAVE) roleBadge += "Slave";
  else roleBadge += "Standalone";
  roleBadge += "</span>";
  page += "<div class='card fancy'><div class='hd'><div style='display:flex;align-items:center;gap:8px'><b>Live status</b>";
  page += "</div><span class='tag'>";
  page += configMode ? "CONFIG" : "RUN";
  page += "</span></div><div class='bd'>";

  // Mode / state row (fancy)
  const String rawMode = modeLabel();
  const String modeText = dashboardModeLabel(rawMode);
  static const char meterFlowArrow[] PROGMEM =
    "<div id='mf_arrow' class='mfArrow idle'><div class='mfLine'></div><div class='mfHead'></div></div>";
  static const char zeroExportBadge[] PROGMEM =
    "<div id='zero_badge' class='mfBadge warn'>Zero-export UIT</div>";
  page += "<section class='fRow modern-scenario' aria-labelledby='modern-scenario-title'><div class='modern-scenario-heading'>"
          "<h2 class='modern-card-title' id='modern-scenario-title'>" + iconSvg("layers") + "Actief scenario</h2>" + roleBadge + "</div>";
  page += "<ul class='modern-scenario-tiles' aria-label='Scenario’s' data-mode='" + rawMode + "'>";
  const struct {
    const char* mode;
    const char* icon;
    const char* label;
  } tiles[] = {
    {"LEGIONELLA", "leg", "Legio"},
    {"FORCE_HEAT", "bolt", "Force"},
    {"ELEC", "leaf", "ELEC"},
    {"PRICE_BOOST", "coins", "Prijs"},
    {"NORMAL", "normal", "Normal"},
  };
  for (const auto& tile : tiles) {
    page += String("<li data-scenario='") + tile.mode + "'>" + iconSvg(tile.icon) + "<span>" + tile.label + "</span></li>";
  }
  page += "</ul><div class='modern-scenario-current'>" + iconSvg("sun") +
          "<span class='fMode' id='control_mode'>" + modeText + "</span><span class='modern-scenario-target'>Doel <b id='modern-scenario-target'>" +
          String(targetExportW, 0) + " W</b></span></div><div class='modern-scenario-state'>"
          "<div class='modern-scenario-algorithm'><span class='fDot' aria-hidden='true'></span><span class='tiny' id='normal_alg'>" +
          normalAlgLabel() + "</span></div>";
  page += "<div class='modern-scenario-meter-state'><span class='tiny' id='leg_state'>" + legStateLabel() + "</span>";
  page += FPSTR(meterFlowArrow);
  page += "</div><div class='fState'><span>RUN</span></div></div>";
  page += "<div class='advBox' aria-label='Verwarmingsadvies'><div class='advMain'>" + iconSvg("bolt") +
          "<span id='heat_advice'>-</span></div><div class='advSub'>"
          "<dl class='modern-advice-costs'><div><dt>Gas</dt><dd id='heat_gas_ct'>-</dd></div>"
          "<div><dt>Elektra</dt><dd id='heat_elec_ct'>-</dd></div></dl><span class='modern-advice-unit'>ct/kWh warmte</span></div>"
          "<span class='pill modern-advice-pill'>Advies</span></div></section>";

  // Meter and zero-export state
  page += "<div class='meterFlow'><h2 class='mfLbl'>" + iconSvg("bolt") + "Netvermogen (P1)</h2>";
  page += "<div class='modern-net-gauge'><svg class='modern-net-graphic' viewBox='0 0 352 146' aria-hidden='true'>"
          "<defs><linearGradient id='modern-net-gradient'><stop stop-color='#c8fc43'/><stop offset='.48' stop-color='#37ffac'/>"
          "<stop offset='1' stop-color='#02dfff'/></linearGradient>"
          "<linearGradient id='modern-net-wave'><stop stop-color='#25dff9' stop-opacity='0'/><stop offset='.5' stop-color='#25dff9'/>"
          "<stop offset='1' stop-color='#25dff9' stop-opacity='0'/></linearGradient></defs>"
          "<g class='modern-net-art'>"
          "<path class='modern-net-wires' d='M0 73Q18 69 28 51m0 0q19 20 39 22M0 91q17-4 25-22m30 0q9 10 20 13M7 112q31-9 65 0'/>"
          "<path class='modern-net-pylon' d='m28 109 6-40 6-27 6 27 6 40M27 110l20-19-14-11 13-11-10-10m16 51L33 91l14-11-13-11 10-10M28 61v-5l12-7 12 7v5M24 81v-6l16-9 16 9v6M28 56h24M24 75h32M22 110h36'/>"
          "<path class='modern-net-wave-glow' stroke='url(#modern-net-wave)' d='M249 66c25-17 32 15 51 11s26-32 52-23'/>"
          "<path class='modern-net-waves' stroke='url(#modern-net-wave)' d='M249 66c25-17 32 15 51 11s26-32 52-23m-103 25c25-17 34 15 53 9s27-23 50-17'/>"
          "</g><path class='modern-net-track' d='M80 132a96 96 0 0 1 192 0'/>"
          "<path id='modern-net-fill' d='M80 132a96 96 0 0 1 192 0' "
          "pathLength='100' stroke-dasharray='0 100' stroke='url(#modern-net-gradient)'/></svg>";
  page += "<div class='modern-net-readout'><div class='mfVal' id='net_power_w'>" + String(lastNetPowerW) +
          "</div><div id='modern-net-direction'></div></div>";
  page += "<div class='modern-net-axis'><span id='modern-net-min'>−2 kW</span>"
          "<span id='modern-net-max'>+2 kW</span></div></div>";
  page += "<div class='modern-surplus-row'><div class='modern-surplus-summary'><span class='modern-surplus-label'>" + iconSvg("sun") +
          "Regeloverschot</span><strong class='modern-surplus-reading'><span id='overshoot_w'>" +
          String((int)lround(lastOvershootW)) + "</span><small> W</small></strong></div><div class='modern-surplus-status'>";
  page += FPSTR(zeroExportBadge);
  page += "</div></div></div>";


  // KPI + PWM
  page += "<div class='kpiGrid'>";
  page += "<h2 class='kpiTitle'>" + iconSvg("wave") + "Boiler PWM</h2>";
  page += "<div class='kpiBox'>";
  page += "<div class='modern-estimate'>Vermogen (indicatief)<strong><span id='modern-pwm-watts' data-rated='";
  page += String(maxBoilerPowerW, 1) + "'>" + String((long)lround(maxBoilerPowerW * lastDutyPercent / 100.0f)) + "</span> W</strong></div>";
  page += "<div class='bar'><div id='pwm_bar' class='fill' style='width:" + String(lastDutyPercent,1) + "%'></div></div>";
  page += "<div class='modern-limit'>Vermogenslimiet <b>" + String(maxOutputPercentLimit, 1) + "%</b></div>";
  page += "<div class='modern-secondary'>Netvermogen <b class='kpiBig' id='net_power_big'>" + String(lastNetPowerW) + " W</b></div>";
  page += "<div id='heat_exec_active_row' style='display:none;margin-top:10px;font-size:14px;font-weight:800;color:var(--accent2,#22c55e)'>Verwarmen via advies: ACTIEF</div>";
  page += "</div>";
  page += "<div class='pwmRing' id='pwmRing' style='--pwm:" + String(lastDutyPercent,1) + ";'><div class='pct'><span id='duty_percent'>" + String(lastDutyPercent, 1) + "</span>%</div></div>";
  page += "</div>";


  // (step81) Fase-ampères (totaal) — eigen kaders onder Netvermogen, boven Acties
  page += "<div class='phaseGrid' id='phase_row' style='display:none'>";
  page += "<h2 class='modern-card-title'>" + iconSvg("layers") + "Fasestromen</h2>";
  for (uint8_t phase = 1; phase <= 3; ++phase) {
    const String n(phase);
    page += "<div class='phaseBox' id='ph_box_" + n + "'><div class='phaseLbl'>L" + n +
            "</div><div class='phaseVal'><span id='ph_l" + n + "'>-</span><span class='phaseUnit'>A</span></div>";
    page += "<div class='modern-phase-bar' id='modern-phase-" + n + "' aria-hidden='true'></div>";
    page += "</div>";
  }
  page += "<div class='modern-phase-guard' id='modern-phase-guard'></div>";
  page += "</div>";

  // ===== Dashboard acties (compact, eigen kader binnen Live status) =====
  // Patrick wens (step64): Forceer verwarmen + Legionella programma direct onder de live KPI's,
  // binnen hetzelfde grote 'Live status' kader, maar wel in een eigen sub-kader (zelfde stijl).
  page += "<div class='actionCard'>";
  page += "<h2 class='modern-card-title'>" + iconSvg("control") + "Snelle acties</h2>";

  // Forceer verwarmen (toggle)
  page += "<form method='POST' action='/control/toggle_force'>";
  page += "<div class='row nb'><div class='k'>";
  page += iconSvg("bolt");
  page += "Forceer verwarmen</div><div class='v'>";
  page += String("<span class='switch ") + ((controlMode == MODE_FORCE_HEAT) ? "on" : "") + "' onclick='sw(this,true)'>";
  page += String("<input type='checkbox' ") + ((controlMode == MODE_FORCE_HEAT) ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";
  page += "</form>";

  page += "<div class='sep'></div>";

  // Legionella (toggle — zelfde switch UI)
  page += "<div class='row nb'><div class='k'>";
  page += iconSvg("leg");
  page += "Legionella programma</div><div class='v'>";
  page += String("<span class='switch ") + (legRunning ? "on" : "") + "' onclick='legSw(this)'>";
  page += String("<input type='checkbox' ") + (legRunning ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";

  if (relayIsEnabled()) {
    page += "<div class='sep'></div>";
    page += "<div class='row nb'><div class='k'>";
    page += iconSvg("control");
    page += "Relais handmatig aan/uit</div><div class='v'>";
    page += String("<span class='switch ") + (relayGetManualOn() ? "on" : "") + "' onclick='relaySw(this)'>";
    page += String("<input type='checkbox' ") + (relayGetManualOn() ? "checked" : "") + "><span class='slider'></span></span>";
    page += "</div></div>";
  }

  page += "</div>"; // actionCard

  page += "</div></div>";

  page += "<div class='modern-detail-grid'>";



  
// ===== Temperatuur (compact) =====
// Patrick wens: alleen zichtbaar als er minimaal 1 DS18B20 écht aanwezig is (enabled + present).
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
    page += "<div class='card' id='temps_card'><div class='hd'><b>";
    page += iconSvg("leg");
    page += "Temperaturen</b><span class='tag'>DS18B20</span></div>"
            "<div class='bd'><div id='temps_list' class='chips'>";
    if (anyTempsValue) {
      page += tempsChips;
    } else {
      page += "<div class='help'><b>Temperatuursensor(en) gevonden.</b> Wachten op data…</div>";
    }
    page += "</div></div></div>";
  }
  page += "<section class='card' id='modern-controller-card' hidden><div class='hd'><b>" + iconSvg("network") +
          "Multi Controller</b><span class='tag' id='modern-controller-count'></span></div>"
          "<div class='bd'><div class='mcActiveRow' id='mc_active_row' style='display:none'></div></div></section></div>";




  page += "<div class='card'><div class='hd'><b>Live grafiek (60s)</b><span class='tag'>auto</span></div><div class='bd'>";
  page += "<div class='modern-chart-legend'><span>Netvermogen (W)</span><span>Boiler PWM (%)</span></div>"
          "<div class='spark modern-chart'><canvas id='spark_combined' role='img' "
          "aria-label='Livegrafiek met netvermogen in watt op de linker as en Boiler PWM in procent op de rechter as'></canvas></div>";
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

  page += "<b id='dash-api-heading'>API-uitleeslinks</b><ul class='dash-api' aria-labelledby='dash-api-heading'>"
          "<li><a href='/api/status' target='_blank' rel='noopener'>Volledige status<code>/api/status</code></a></li>"
          "<li><a href='/api/system' target='_blank' rel='noopener'>Systeemdiagnose<code>/api/system</code></a></li>"
          "</ul>";
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

  server.sendHeader("Cache-Control", "no-store, max-age=0");
  const String footer = htmlDashboardScripts() + htmlFooter("dash");
  server.setContentLength(page.length() + footer.length());
  server.send(200, "text/html; charset=utf-8", page);
  server.sendContent(footer);
}
