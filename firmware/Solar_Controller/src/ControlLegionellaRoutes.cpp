#include "ControlLegionellaRoutes.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>
#include <time.h>

#include "app_globals.h"
#include "ControllerTemperatureGate.h"
#include "MultiController.h"
#include "RelayControl.h"
#include "ScenarioLogic.h"
#include "StatusLabels.h"
#include "WebRoutes.h"
#include "WebUtils.h"
#include "mqtt.h"
#include "sensors.h"

extern WebServer server;
extern Preferences prefs;

void mqttPublishCoreLiveStates(bool forceNow);

void handleToggleForce() {
  const bool wasForce = (controlMode == MODE_FORCE_HEAT);
  controlMode = wasForce ? MODE_NORMAL : MODE_FORCE_HEAT;
  if (wasForce) {
    // Bij uitschakelen van Force Heat moet PWM direct terug naar 0%, net als bij Legionella stop.
    // Daarna mag de normale regeling weer opnieuw oppakken. Dit voorkomt traag terugregelen
    // via de STEP/NORMAL-logica terwijl de gebruiker juist expliciet Force uitzet.
    setBoilerDutyPercent(0.0f);
  }
  saveConfig();

  if (mqttEnabled && mqttClient.connected()) {
    mqttPublishForceState(true);
    mqttPublishMultiControllerStates(true);
    mqttPublishCoreLiveStates(true);
  mqttPublishStatusJson(true);
  }
  redirectTo("/control");
}

void handleRelayToggle() {
  if (relayIsEnabled()) {
    relayToggleManual();
  } else {
    relaySetManualOn(false);
  }

  if (mqttEnabled && mqttClient.connected()) {
    mqttPublishRelayState(true);
    mqttPublishCoreLiveStates(true);
  mqttPublishStatusJson(true);
  }

  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(204, "text/plain", "");
}

void handleControlSaveLimits() {
  if (server.hasArg("max_output_pct")) maxOutputPercentLimit = server.arg("max_output_pct").toFloat();
  if (server.hasArg("leg_pct"))        legOutputLimitPercent = server.arg("leg_pct").toFloat();
  clampAll();
  saveConfig();

  if (mqttEnabled && mqttClient.connected()) {
    mqttPublishLegionellaConfig(true);
    mqttPublishLimitsStates(true);
    mqttPublishCoreLiveStates(true);
  mqttPublishStatusJson(true);
  }
  redirectTo("/control");
}

void handleControlSaveTemperatureGate() {
  controllerTemperatureGateApplyControlSettings(server);
  clampAll();
  saveConfig();
  controllerTemperatureGateRefresh();

  if (mqttEnabled && mqttClient.connected()) {
    mqttPublishMultiControllerStates(true);
    mqttPublishCoreLiveStates(true);
    mqttPublishStatusJson(true);
  }
  redirectTo("/control");
}

void handleLegionellaStart() {
  if (!legRunning) legStart("MANUAL");
  if (mqttEnabled && mqttClient.connected()) {
    mqttPublishLegionellaState(true);
    mqttPublishCoreLiveStates(true);
  mqttPublishStatusJson(true);
  }
  redirectTo("/legionella");
}

void handleLegionellaCancel() {
  if (legRunning) legStop("CANCEL");
  if (mqttEnabled && mqttClient.connected()) {
    mqttPublishLegionellaState(true);
    mqttPublishCoreLiveStates(true);
  mqttPublishStatusJson(true);
  }
  redirectTo("/legionella");
}

void handleLegionellaSave() {
  // leg_en: toggle (checkbox). Unchecked checkboxes are not submitted.
  // Since this form is specific to Legionella, missing = OFF. Also support explicit 0/1 and *_chk.
  if (server.hasArg("leg_en")) {
    String v = server.arg("leg_en");
    v.trim(); v.toLowerCase();
    legScheduleEnabled = (v == "1" || v == "on" || v == "true" || v == "yes");
  } else if (server.hasArg("leg_en_chk")) {
    legScheduleEnabled = true;
  } else {
    legScheduleEnabled = false;
  }

  if (server.hasArg("leg_dow")) legDow = server.arg("leg_dow").toInt();
  if (server.hasArg("leg_hr"))  legStartHour = server.arg("leg_hr").toInt();
  if (server.hasArg("leg_min")) legStartMinute = server.arg("leg_min").toInt();
  if (server.hasArg("leg_dur")) legDurationMin = server.arg("leg_dur").toInt();

  // Relais automatisch bij Legionella. Handmatig schakelen blijft mogelijk; automatische start/stopmomenten blijven leidend.
  if (server.hasArg("leg_rel_auto")) {
    String v = server.arg("leg_rel_auto");
    v.trim(); v.toLowerCase();
    legRelayAutoEnabled = (v == "1" || v == "on" || v == "true" || v == "yes");
  } else {
    legRelayAutoEnabled = false;
  }
  if (server.hasArg("leg_rel_start")) legRelayStartDelayMin = server.arg("leg_rel_start").toInt();
  if (server.hasArg("leg_rel_delay")) legRelayOffDelayMin = server.arg("leg_rel_delay").toInt();
  const String oldLegTempAddrForHeatCheap = legTempAddrStr;
  const float oldLegTargetTempForHeatCheap = legTargetTempC;

  // leg_ts: checkbox toggle (aanwezig = 1, afwezig = 0). We accepteren ook oudere forms met '0/1'.
  if (server.hasArg("leg_ts")) {
    String v = server.arg("leg_ts");
    v.trim();
    legUseTempSensor = (v == "1" || v == "on" || v == "true");
  } else {
    legUseTempSensor = false;
  }

  // Optioneel: gekozen DS18B20 (addrStr) voor legionella temperatuurbron
  if (server.hasArg("leg_taddr")) {
    legTempAddrStr = server.arg("leg_taddr");
    legTempAddrStr.trim();
    legTempAddrStr.toLowerCase();
  }
  if (server.hasArg("leg_tc")) legTargetTempC = server.arg("leg_tc").toFloat();
  if (server.hasArg("leg_hold")) legHoldMin = server.arg("leg_hold").toInt();

  clampAll();
  if (oldLegTempAddrForHeatCheap != legTempAddrStr || fabsf(oldLegTargetTempForHeatCheap - legTargetTempC) > 0.00001f) {
    heatCheapPlanInvalidate();
  }
  saveConfig();

  if (mqttEnabled && mqttClient.connected()) {
    mqttPublishLegionellaConfig(true);
    mqttPublishLegionellaPlanStates(true);
    mqttPublishLegionellaState(true);
    mqttPublishCoreLiveStates(true);
  mqttPublishStatusJson(true);
  }
  redirectTo("/legionella");
}

void handleLegionellaResetSuccess() {
  // (Stap 5) Reset alleen de "laatste succesvolle cyclus" (target+hold).
  // Dit verandert GEEN instellingen (planning, doeltemp, hold, etc.).
  legLastSuccessEpoch = 0;

  prefs.begin("boilerbox", false);
  prefs.putUInt("leg_lsuc", 0);
  prefs.end();

  // Push direct naar MQTT/status (als actief) zodat HA/Homey meteen mee is.
  mqttPublishCoreLiveStates(true);
  mqttPublishStatusJson(true);

  redirectTo("/legionella");
}


// ---- Section-scoped boolean reader -----------------------------------------
// In this UI, toggles are styled checkboxes.
// Unchecked checkboxes are NOT submitted by browsers.
// We only want "missing = OFF" when saving within the same settings section,
// otherwise other settings pages would unintentionally reset unrelated toggles.


void handleControlPage() {
  controllerTemperatureGateRefresh();
  String page = htmlHeader("Bediening");

  // (UI) Top tabs verwijderd – bottom nav is genoeg


  // page += topTabs("control");
page += "<div class='card'><div class='hd'><b>Modus</b><span class='tag'>bediening</span></div><div class='bd'>";
  page += rowHtml("Huidige modus", modeLabel());

  page += "<form method='POST' action='/control/toggle_force'>";
  page += "<button class='btn' type='submit'>";
  page += (controlMode == MODE_FORCE_HEAT) ? "Forceer verwarmen: AAN → UIT" : "Forceer verwarmen: UIT → AAN";
  page += "</button></form>";

  page += "<div class='help'>NORMAL = regeling op target/net. FORCE_HEAT = altijd verwarmen.</div>";
  page += "</div></div>";

  page += "<div class='card'><div class='hd'><b>Vermogen limieten</b><span class='tag'>safety</span></div><div class='bd'>";
  page += "<form method='POST' action='/control/save_limits'>";

  page += "<label>Max output naar Kemo [%] (globale safety)</label>";
  page += "<input type='number' min='0' max='100' step='1' name='max_output_pct' value='" + String(maxOutputPercentLimit,1) + "'>";

  page += "<label>Legionella output limiet [%]</label>";
  page += "<input type='number' min='0' max='100' step='1' name='leg_pct' value='" + String(legOutputLimitPercent,1) + "'>";

  page += "<button class='btn' type='submit'>Opslaan (zonder reboot)</button>";
  page += "</form>";
  page += "</div></div>";


  page += "<details class='acc' open><summary><div class='sum'><b>" + controllerTemperatureGateFeatureName() + "</b><span class='tag gray'>control</span></div></summary><div class='accbd'>";
  page += "<form method='POST' action='/control/save_temp_gate'>";
  const uint8_t ctgVisibleCount = (mcEnabled && mcRole == MC_ROLE_MASTER) ? mcControllerCount : 1;
  if (ctgVisibleCount <= 1) {
    page += "<div class='help'>Deze functie bepaalt simpel of controller 1 wel of niet mag meedoen op basis van temperatuur. Controller 1 blijft de vaste lokale uitgang op GPIO25.</div>";
  } else {
    page += "<div class='help'>Per controller kun je bepalen of temperatuurvrijgave actief is. Controllers die geblokkeerd zijn, doen niet mee in de regeling.</div>";
  }

  for (uint8_t i = 0; i < ctgVisibleCount && i < MC_MAX_SLOTS; i++) {
    const ControllerTemperatureGateConfig& cfg = controllerTemperatureGateGetConfig(i);
    const ControllerTemperatureGateRuntime& rt = controllerTemperatureGateGetRuntime(i);
    String ctrlName = mcSlots[i].name.length() ? mcSlots[i].name : (String("Controller ") + String(i + 1));
    const String prefix = String("ctg_s") + String(i + 1) + String("_");

    if (i > 0) page += "<div class='sep'></div>";
    const bool localTempConfig = controllerTemperatureGateUsesLocalSensorConfig(mcRole, i);
    page += "<div class='help'><b>" + htmlAttrEscape(ctrlName) + "</b>";
    if (controllerTemperatureGateControllerUsesFixedGpio25(mcRole, i)) {
      page += " — vaste lokale uitgang op GPIO25";
    } else if (mcSlots[i].slotType == MC_SLOT_LOCAL_PWM) {
      page += " — lokale uitgang op GPIO" + String(mcSlots[i].pwmPin);
    } else {
      page += " — netwerkcontroller";
    }
    page += "</div>";

    if (!localTempConfig) {
      page += "<div class='help'>Deze netwerkcontroller gebruikt zijn <b>eigen</b> lokale temperatuursensor op de slave. De master kent die sensor niet en toont hier daarom geen sensor- of temperatuurinstellingen.</div>";
      page += "<div class='help'>De master neemt deze controller alleen mee als de slave zichzelf als <b>vrijgegeven</b> meldt.</div>";
      if (rt.reason.length()) page += "<div class='help'>Info: " + htmlAttrEscape(rt.reason) + "</div>";
      continue;
    }

    page += "<div class='row'><div class='k'>Rekening houden met temperatuur</div><div class='v'>";
    page += String("<span class='switch ") + (cfg.enabled ? "on" : "") + "' onclick='sw(this,false)'>";
    page += String("<input type='checkbox' name='") + prefix + "en' value='1' " + (cfg.enabled ? "checked" : "") + "><span class='slider'></span></span>";
    page += "</div></div>";

    String stateText = rt.released ? "Vrijgegeven" : "Geblokkeerd";
    page += "<div class='help'>Status: <b>" + stateText + "</b>";
    if (rt.sensorName.length()) page += " — sensor <b>" + htmlAttrEscape(rt.sensorName) + "</b>";
    if (rt.sensorValid) page += " — " + String(rt.sensorTempC, 1) + " °C";
    page += "</div>";

    if (!cfg.enabled) continue;

    page += "<label>Temperatuursensor</label>";
    page += String("<select name='") + prefix + "addr'>";
    page += "<option value=''>— kies sensor —</option>";
    bool selectedPresent = false;
    for (int s = 0; s < tempSensorCount; s++) {
      if (!tempSensors[s].enabled && tempSensors[s].addrStr != cfg.sensorAddr) continue;
      String nm = tempSensors[s].name.length() ? tempSensors[s].name : (String("DS18B20 ") + String(s + 1));
      page += "<option value='" + tempSensors[s].addrStr + "'";
      if (cfg.sensorAddr == tempSensors[s].addrStr) {
        page += " selected";
        selectedPresent = true;
      }
      page += ">" + htmlAttrEscape(nm) + "</option>";
    }
    if (!selectedPresent && cfg.sensorAddr.length() == 16) {
      page += "<option value='" + cfg.sensorAddr + "' selected>Huidige sensor niet gevonden (" + htmlAttrEscape(cfg.sensorAddr) + ")</option>";
    }
    page += "</select>";

    page += "<label>Stoptemperatuur [°C]</label>";
    page += "<input type='number' step='0.1' min='0' max='100' name='" + prefix + "stop' value='" + fmtDot(cfg.stopTempC,1) + "'>";

    page += "<label>Starttemperatuur [°C]</label>";
    page += "<input type='number' step='0.1' min='0' max='100' name='" + prefix + "start' value='" + fmtDot(cfg.startTempC,1) + "'>";

    if (rt.reason.length()) page += "<div class='help'>" + htmlAttrEscape(rt.reason) + "</div>";
  }

  page += "<div class='row' style='margin-top:14px'><button class='btn' type='submit'>Opslaan</button></div>";
  page += "</form></div></details>";

  page += htmlFooter("control");
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "text/html; charset=utf-8", page);
}



void handleLegionellaPage() {
  String page = htmlHeader("Legionella");

  // (UI) Top tabs verwijderd – bottom nav is genoeg


  // page += topTabs("leg");
page += "<div class='card'><div class='hd'><b>Run</b><span class='tag'>";
  page += legStateLabel();
  page += "</span></div><div class='bd'>";

  if (!legRunning) {
    page += "<form method='POST' action='/legionella/start'><button class='btn dark' type='submit'>Start nu</button></form>";
  } else {
    page += "<form method='POST' action='/legionella/cancel'><button class='btn red' type='submit'>Stop</button></form>";
  }
  if (multiControllerIsSlaveRole() && multiControllerHasNetworkPeersConfigured() && !multiControllerSlaveLegionellaOverrideEnabled()) {
    page += "<div class='help'>Slave-modus: master blijft leidend. Zet in Multi Controller de optie <b>Lokale legionella mag master overrulen</b> op Ja om dit programma lokaal te laten draaien.</div>";
  }

  time_t nextRun = computeNextLegionellaEpoch();
  if (nextRun > 0) page += "<div class='help'>Volgende planning: <b>" + fmtTimeLocal(nextRun) + "</b></div>";
  else page += "<div class='help'>Volgende planning: -</div>";

  if (legLastRunEpoch > 0) page += "<div class='help'>Laatste run: <b>" + fmtTimeLocal((time_t)legLastRunEpoch) + "</b></div>";
  if (multiControllerSlaveLegionellaOverrideActive()) {
    page += "<div class='help'>Master override actief: <b>Legionella</b></div>";
  } else if (multiControllerSlaveLegionellaOverrideEnabled()) {
    page += "<div class='help'>Master override bij legionella: <b>toegestaan</b></div>";
  }

  page += "</div></div>";

  page += "<form method='POST' action='/legionella/save'>";
  page += "<div class='card'><div class='hd'><b>Planning</b><span class='tag'>auto</span></div><div class='bd'>";

  page += "<div class='row'><div class='k'>Automatisch uitvoeren</div><div class='v'>";
page += String("<span class='switch ") + (legScheduleEnabled ? "on" : "") + "' onclick='sw(this,false)'>";
page += String("<input type='checkbox' name='leg_en' value='1' ") + (legScheduleEnabled ? "checked" : "") + "><span class='slider'></span></span>";
page += "</div></div>";
page += "<label>Dag van de week</label><select name='leg_dow'>";
  for (int d=0; d<=6; d++) {
    page += "<option value='" + String(d) + "'";
    if (legDow == d) page += " selected";
    page += ">" + dowName(d) + "</option>";
  }
  page += "</select>";

  page += "<label>Starttijd</label><div class='grid2'>";
  page += "<input type='number' min='0' max='23' name='leg_hr' value='" + String(legStartHour) + "'>";
  page += "<input type='number' min='0' max='59' name='leg_min' value='" + String(legStartMinute) + "'>";
  page += "</div>";

  page += "<label>Duur (min) (tijd-modus / max runtime bij temp-modus)</label>";
  page += "<input type='number' min='1' max='360' name='leg_dur' value='" + String(legDurationMin) + "'>";

  page += "<div class='sep'></div>";

  page += "<div class='row'><div class='k'>Temperatuur-modus gebruiken</div><div class='v'>";
  page += String("<span class='switch ") + (legUseTempSensor ? "on" : "") + "' onclick='sw(this,false)'>";
  page += String("<input type='checkbox' name='leg_ts' value='1' ") + (legUseTempSensor ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";

  page += "<label>Legionella sensor</label>";
  page += "<select name='leg_taddr'>";
  page += "<option value=''>— (gebruik CONTROL / eerste)</option>";
  for (int i = 0; i < tempSensorCount; i++) {
    if (!tempSensors[i].enabled) continue;
    String nm = (tempSensors[i].name.length() ? tempSensors[i].name : (String("DS18B20 ") + String(i+1)));
    page += "<option value='" + tempSensors[i].addrStr + "'";
    if (legTempAddrStr == tempSensors[i].addrStr) page += " selected";
    page += ">" + htmlAttrEscape(nm) + "</option>";
  }
  page += "</select>";
  page += "<div class='help'>Kies welke temperatuursensor gebruikt wordt voor legionella temperatuur-modus. Naam blijft behouden.</div>";

  page += "<label>Doeltemperatuur [°C]</label>";
  page += "<input type='number' step='0.1' min='40' max='75' name='leg_tc' value='" + String(legTargetTempC,1) + "'>";

  page += "<label>Hold tijd op doeltemperatuur [min]</label>";
  page += "<input type='number' min='0' max='120' name='leg_hold' value='" + String(legHoldMin) + "'>";

    // Sensor status: toon gekozen legionella-sensor (of fallback)
  String useAddr = (legTempAddrStr.length() == 16) ? legTempAddrStr : tempControlAddrStr;
  String useName = "";
  bool   usePresent = false;
  float  useTemp = NAN;
  for (int i = 0; i < tempSensorCount; i++) {
    if (tempSensors[i].addrStr == useAddr) {
      useName = tempSensors[i].name.length() ? tempSensors[i].name : (String("DS18B20 ") + String(i+1));
      usePresent = tempSensors[i].present;
      useTemp = tempSensors[i].tempC;
      break;
    }
  }
  String sst = (ds18Available && tempSensorCount>0 && usePresent) ? "gevonden" : "niet gevonden";
  if (useName.length()) sst = useName + " — " + sst;
  if (tempIsValid(useTemp)) sst += " • " + String(useTemp,1) + " °C";
  page += "<div class='help'>Sensor status: <b>" + sst + "</b></div>";

  // ---------------- Legionella status (Stap 1) ----------------
  // Definitie:
  //  - SUCCESS: target bereikt én hold-tijd afgerond (legStop("DONE"))
  //  - OK: SUCCESS binnen 7 dagen (168 uur)
  //  - DUE: geen SUCCESS in laatste 7 dagen (of nog nooit)
  //  - RUNNING: run is actief
  //  - NO_SENSOR: temperatuur-modus aan, maar sensor/waarde ontbreekt
  String legStatus = "DISABLED";
  String legInfo = "";
  String lastSuccessStr = "—";
  uint32_t hoursSince = 0;
  uint32_t hoursUntilDue = 0;

  if (legRunning) {
    legStatus = "RUNNING";
  } else if (!legUseTempSensor) {
    legStatus = "DISABLED";
    legInfo = "Temperatuur-modus staat uit: geen weekbeoordeling.";
  } else {
    bool sensorOk = (ds18Available && tempSensorCount > 0 && usePresent && tempIsValid(useTemp));
    if (!sensorOk) {
      legStatus = "NO_SENSOR";
      legInfo = "Temperatuur-modus staat aan, maar de gekozen sensor is niet beschikbaar of geeft geen geldige waarde.";
    } else if (!timeIsValid()) {
      legStatus = "UNKNOWN";
      legInfo = "Geen geldige tijd (NTP): kan weekstatus niet betrouwbaar bepalen.";
    } else {
      time_t now = time(nullptr);
      const uint32_t WINDOW_SEC = 7UL * 24UL * 3600UL; // 168 uur
      if (legLastSuccessEpoch == 0) {
        legStatus = "DUE";
        legInfo = "Nog geen succesvolle cyclus geregistreerd (target + hold).";
      } else {
        uint32_t age = (uint32_t)now - legLastSuccessEpoch;
        if (age < WINDOW_SEC) {
          legStatus = "OK";
        } else {
          legStatus = "DUE";
        }
        lastSuccessStr = dmyDateLocal((time_t)legLastSuccessEpoch);
        hoursSince = age / 3600UL;
        hoursUntilDue = (age >= WINDOW_SEC) ? 0 : ((WINDOW_SEC - age) / 3600UL);
      }
    }
  }

  page += "<div class='sep'></div>";
  page += "<div class='help'><b>Legionella status</b></div>";
  page += "<div class='help'>Status: <b>" + legStatus + "</b></div>";
  if (lastSuccessStr != "—") {
    page += "<div class='help'>Laatste succes (target+hold): <b>" + lastSuccessStr + "</b></div>";
    page += "<div class='help'>Uren sinds succes: <b>" + String(hoursSince) + "</b></div>";
    page += "<div class='help'>Uren tot 'due' (7 dagen): <b>" + String(hoursUntilDue) + "</b></div>";
  } else if (legUseTempSensor) {
    page += "<div class='help'>Laatste succes (target+hold): <b>—</b></div>";
  }
  if (legInfo.length()) page += "<div class='help' style='opacity:.85'>" + legInfo + "</div>";


// (Stap 5) Reset/clear laatste succesvolle cyclus (alleen status; geen overige settings)
page += "<div class='sep'></div>";
page += "<div class='help'><b>Reset (test/onderhoud)</b></div>";
page += "<div class='help' style='opacity:.85'>Hiermee zet je 'Laatste succes' terug naar leeg, zodat de status weer DUE wordt. Dit verandert geen andere instellingen.</div>";
page += "<div style='display:flex;gap:10px;'>";
page += "<button class='btn red' style='flex:1' type='button' onclick='if(confirm(\"Laatste legionella-succes resetten?\")){fetch(\"/legionella/reset_success\",{method:\"POST\"}).then(()=>location.reload());}'>Reset</button>";
page += "<button class='btn' style='flex:1' type='submit'>Opslaan</button>";
page += "</div>";
  page += "</div></div>";

  page += "<div class='card'><div class='hd'><b>Relais</b></div><div class='bd'>";
  page += "<div class='row'><div class='k'>Relais automatisch inschakelen</div><div class='v'>";
  page += String("<span class='switch ") + (legRelayAutoEnabled ? "on" : "") + "' onclick='sw(this,false)'>";
  page += String("<input type='checkbox' name='leg_rel_auto' value='1' ") + (legRelayAutoEnabled ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";
  page += "<label>Inschakelvertraging relais [min]</label>";
  page += "<input type='number' min='0' max='60' name='leg_rel_start' value='" + String(legRelayStartDelayMin) + "'>";
  page += "<div class='help'>0 = direct inschakelen bij start van de run, maximaal 60 minuten.</div>";
  page += "<label>Nalooptijd relais [min]</label>";
  page += "<input type='number' min='0' max='60' name='leg_rel_delay' value='" + String(legRelayOffDelayMin) + "'>";
  page += "<div class='help'>0 = direct uitschakelen na de run, maximaal 60 minuten. Handmatig schakelen via Home of Home Assistant blijft tussendoor mogelijk; bij automatische modus wordt het relais aan het einde van de run (na eventuele nalooptijd) alsnog uitgeschakeld.</div>";
  if (!relayIsEnabled()) {
    page += "<div class='help'>Let op: relais GPIO18 staat momenteel uitgeschakeld bij Instellingen &gt; Relais.</div>";
  }
  page += "<div class='row' style='margin-top:14px'><button class='btn' type='submit'>Opslaan</button></div>";
  page += "</div></div>";
  page += "</form>";

  page += htmlFooter("leg");
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "text/html; charset=utf-8", page);
}

