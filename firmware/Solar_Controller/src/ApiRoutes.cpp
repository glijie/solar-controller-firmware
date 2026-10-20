#include "ApiRoutes.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <math.h>

#include "app_globals.h"
#include "ConfigStore.h"
#include "mqtt.h"
#include "RelayControl.h"
#include "ScenarioLogic.h"
#include "PhaseGuard.h"
#include "MultiController.h"
#include "sensors.h"

extern WebServer server;

extern String wifiSsid;
extern String wifiPass;
extern String deviceName;
extern String meterType;
extern String meterIp;

extern float maxBoilerPowerW;
extern float targetExportW;
extern float minStepPercent;
extern unsigned long minUpdateIntervalMs;
extern float maxOutputPercentLimit;

extern uint8_t normalAlg;
extern float stepFactor;
extern float deadbandW;
extern float maxStepPctPerUpdatePerUpdate;

extern uint8_t sunSchedMode;
extern int     sunWinStartMin;
extern int     sunWinEndMin;
extern int     sunCheapNBlocks;
extern int     sunDaylightMarginMin;
extern float   sunLat;
extern float   sunLon;
extern bool    dashShowSunWindowLabel;
extern String  sunWinDate;
extern int     sunDaylightStartMin;
extern int     sunDaylightEndMin;
extern int     sunCheapestStartMin;
extern int     sunCheapestEndMin;

extern uint8_t elecPriceSource;
extern float   elecFixedCtKwhAllIn;

extern bool    heatCheapBlocksEnabled;
extern int     heatCheapNBlocks;

extern bool    gasTotalEnabled;
extern uint8_t gasPriceSource;
extern float   gasFixedEurM3;
extern float   gasEnergyTaxEur;
extern float   gasInkoopEur;
extern float   gasBtwPct;

extern bool sysPublishEnabled;
extern int  sysPublishIntervalSec;
extern int  statusPublishIntervalSec;

extern String tempSensorMapJson;
extern String tempControlAddrStr;
extern bool   tempPublishNamesToHA;

extern bool fwAutoUpdateEnabled;

bool mqttEnsureConnected();

namespace {

void apiSendJson(int codeHttp, const String& json) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(codeHttp, "application/json; charset=utf-8", json);
}

bool apiGetJsonBody(DynamicJsonDocument& doc, String* errOut = nullptr) {
  if (!server.hasArg("plain")) {
    if (errOut) *errOut = "missing_body";
    return false;
  }
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    if (errOut) *errOut = "bad_json";
    return false;
  }
  return true;
}

bool apiIsMaskedPassword(const String& s) {
  if (s.length() == 0) return false;
  if (s == "********") return true;
  if (s == "******") return true;
  if (s == "*****") return true;
  return false;
}

void apiPublishAllRelevantState() {
  mqttPublishStatusJson(true);

  mqttPublishForceState(true);
  mqttPublishRelayState(true);
  mqttPublishMultiControllerStates(true);
  mqttPublishLegionellaConfig(true);
  mqttPublishLegionellaPlanStates(true);
  mqttPublishLegionellaState(true);

  mqttPublishTuningStates(true);
  mqttPublishNordpoolStates(true);
  mqttPublishHeatCheapConfigStates(true);
  mqttPublishLimitsStates(true);
  mqttPublishNormalConfigStates(true);
  mqttPublishSunStrategyStates(true);
  mqttPublishElecPriceStates(true);

  mqttPublishSystemJson(true);
}

void apiMaybeReconnectMqtt(bool mqttParamsChanged) {
  if (!mqttParamsChanged) return;

  if (mqttClient.connected()) {
    mqttClient.disconnect();
  }
  lastMqttReconnectAttempt = 0;
  if (!configMode && mqttEnabled && mqttHost.length() > 0) {
    mqttEnsureConnected();
  }
}

void apiRelaySetAndRespond(bool on, bool allowEnableCheckError = true) {
  if (!relayIsEnabled()) {
    relaySetManualOn(false);
    if (mqttEnabled && mqttClient.connected()) {
      mqttPublishRelayState(true);
      mqttPublishStatusJson(true);
    }

    DynamicJsonDocument resp(256);
    resp["ok"] = !on;
    resp["relay_enabled"] = false;
    resp["relay_manual_on"] = false;
    if (on && allowEnableCheckError) {
      resp["error"] = "relay_disabled";
      String out; serializeJson(resp, out);
      apiSendJson(409, out);
      return;
    }

    String out; serializeJson(resp, out);
    apiSendJson(200, out);
    return;
  }

  relaySetManualOn(on);

  if (mqttEnabled && mqttClient.connected()) {
    mqttPublishRelayState(true);
    mqttPublishStatusJson(true);
  }

  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  resp["relay_enabled"] = true;
  resp["relay_manual_on"] = relayGetManualOn();
  String out; serializeJson(resp, out);
  apiSendJson(200, out);
}

} // namespace

bool apiApplyConfigFromJson(const JsonDocument& in, bool& rebootRequired, bool& mqttParamsChanged) {
  rebootRequired = false;
  mqttParamsChanged = false;
  bool changed = false;
  bool heatCheapConfigChanged = false;

  if (in.containsKey("wifi_ssid")) { wifiSsid = in["wifi_ssid"].as<String>(); changed = true; rebootRequired = true; }
  if (in.containsKey("wifi_pass")) {
    String v = in["wifi_pass"].as<String>();
    if (!apiIsMaskedPassword(v)) { wifiPass = v; changed = true; rebootRequired = true; }
  }
  if (in.containsKey("device_name")) { deviceName = in["device_name"].as<String>(); deviceName.trim(); changed = true; rebootRequired = true; }

  if (in.containsKey("meter_type")) { meterType = in["meter_type"].as<String>(); changed = true; }
  if (in.containsKey("meter_ip"))   { meterIp   = in["meter_ip"].as<String>(); changed = true; }
  if (in.containsKey("p1_ip")) {
    String v = in["p1_ip"].as<String>();
    if (meterIp.length() == 0) meterIp = v;
    changed = true;
  }

  if (in.containsKey("ctrl_mode")) {
    uint8_t m = (uint8_t)in["ctrl_mode"].as<int>();
    controlMode = (m == 1) ? (ControlMode)1 : (ControlMode)0;
    changed = true;
  }

  if (in.containsKey("max_power"))     { maxBoilerPowerW = in["max_power"].as<float>(); changed = true; }
  if (in.containsKey("target_export")) { targetExportW   = in["target_export"].as<long>(); changed = true; }
  if (in.containsKey("min_step"))      { minStepPercent  = in["min_step"].as<float>(); changed = true; }
  if (in.containsKey("min_interval"))  { minUpdateIntervalMs = (unsigned long)in["min_interval"].as<unsigned long>(); changed = true; }
  if (in.containsKey("max_out_pct"))   { maxOutputPercentLimit = in["max_out_pct"].as<float>(); changed = true; }

  if (in.containsKey("n_alg")) { normalAlg = (uint8_t)in["n_alg"].as<int>(); changed = true; }
  if (in.containsKey("n_sf"))  { stepFactor = in["n_sf"].as<float>(); changed = true; }
  if (in.containsKey("n_dbw")) { deadbandW = in["n_dbw"].as<float>(); changed = true; }
  if (in.containsKey("n_msp")) { maxStepPctPerUpdatePerUpdate = in["n_msp"].as<float>(); changed = true; }

  bool sunChanged = false;
  if (in.containsKey("sun_s_mode")) { sunSchedMode = (uint8_t)in["sun_s_mode"].as<int>(); changed = true; sunChanged = true; }
  if (in.containsKey("sun_w_s"))    { sunWinStartMin = in["sun_w_s"].as<int>(); changed = true; sunChanged = true; }
  if (in.containsKey("sun_w_e"))    { sunWinEndMin = in["sun_w_e"].as<int>(); changed = true; sunChanged = true; }
  if (in.containsKey("sun_c_n"))    { sunCheapNBlocks = in["sun_c_n"].as<int>(); changed = true; sunChanged = true; }
  if (in.containsKey("sun_marg"))   { sunDaylightMarginMin = in["sun_marg"].as<int>(); changed = true; sunChanged = true; }
  if (in.containsKey("sun_lat"))    { sunLat = in["sun_lat"].as<float>(); changed = true; sunChanged = true; }
  if (in.containsKey("sun_lon"))    { sunLon = in["sun_lon"].as<float>(); changed = true; sunChanged = true; }
  if (in.containsKey("dash_sun_lbl")) { dashShowSunWindowLabel = in["dash_sun_lbl"].as<bool>(); changed = true; sunChanged = true; }

  if (sunChanged) {
    sunWinDate = "";
    sunDaylightStartMin = sunDaylightEndMin = -1;
    sunCheapestStartMin = sunCheapestEndMin = -1;
  }

  if (in.containsKey("mqtt_enabled")) {
    bool v = in["mqtt_enabled"].as<bool>();
    if (v != mqttEnabled) mqttParamsChanged = true;
    mqttEnabled = v; changed = true;
  }
  if (in.containsKey("mqtt_host")) {
    String v = in["mqtt_host"].as<String>();
    if (v != mqttHost) mqttParamsChanged = true;
    mqttHost = v; changed = true;
  }
  if (in.containsKey("mqtt_port")) {
    uint16_t v = (uint16_t)in["mqtt_port"].as<int>();
    if (v != mqttPort) mqttParamsChanged = true;
    mqttPort = v; changed = true;
  }
  if (in.containsKey("mqtt_user")) {
    String v = in["mqtt_user"].as<String>();
    if (v != mqttUser) mqttParamsChanged = true;
    mqttUser = v; changed = true;
  }
  if (in.containsKey("mqtt_pass")) {
    String v = in["mqtt_pass"].as<String>();
    if (!apiIsMaskedPassword(v)) {
      if (v != mqttPassStr) mqttParamsChanged = true;
      mqttPassStr = v; changed = true;
    }
  }
  if (in.containsKey("mqtt_topic")) {
    String v = in["mqtt_topic"].as<String>();
    if (v != mqttBaseTopic) mqttParamsChanged = true;
    mqttBaseTopic = v; changed = true;
  }

  if (in.containsKey("ha_disc")) { haDiscoveryEnabled = in["ha_disc"].as<bool>(); changed = true; }

  if (in.containsKey("elec_src"))    { elecPriceSource = (uint8_t)in["elec_src"].as<int>(); changed = true; }
  if (in.containsKey("elec_fix_ct")) { elecFixedCtKwhAllIn = in["elec_fix_ct"].as<float>(); changed = true; }


  // Gas vs Elektra / slimme ELEC-blokken. Wijzigingen die de economische
  // beslissing of planning raken, maken het huidige goedkope-blokkenplan ongeldig.
  if (in.containsKey("heat_auto")) {
    const bool v = in["heat_auto"].as<bool>();
    if (v != heatAutoEnabled) heatCheapConfigChanged = true;
    heatAutoEnabled = v;
    changed = true;
  }
  if (in.containsKey("heat_exec_el")) {
    const bool v = in["heat_exec_el"].as<bool>();
    if (v != heatExecElecWhenCheaper) heatCheapConfigChanged = true;
    heatExecElecWhenCheaper = v;
    changed = true;
  }
  if (in.containsKey("heat_ch_en")) {
    const bool v = in["heat_ch_en"].as<bool>();
    if (v != heatCheapBlocksEnabled) heatCheapConfigChanged = true;
    heatCheapBlocksEnabled = v;
    changed = true;
  }
  if (in.containsKey("heat_ch_bt")) {
    const bool v = in["heat_ch_bt"].as<bool>();
    if (v != heatCheapUseBoilerTarget) heatCheapConfigChanged = true;
    heatCheapUseBoilerTarget = v;
    changed = true;
  }
  if (in.containsKey("heat_ch_n")) {
    const int v = in["heat_ch_n"].as<int>();
    if (v != heatCheapNBlocks) heatCheapConfigChanged = true;
    heatCheapNBlocks = v;
    changed = true;
  }
  if (in.containsKey("heat_kwhm3")) {
    const float v = in["heat_kwhm3"].as<float>();
    if (fabsf(v - gasKwhPerM3) > 0.00001f) heatCheapConfigChanged = true;
    gasKwhPerM3 = v;
    changed = true;
  }
  if (in.containsKey("heat_geff")) {
    const float v = constrain(in["heat_geff"].as<float>(), 0.0f, 1.0f);
    if (fabsf(v - gasBoilerEff) > 0.00001f) heatCheapConfigChanged = true;
    gasBoilerEff = v;
    changed = true;
  }
  if (in.containsKey("heat_eeff")) {
    const float v = constrain(in["heat_eeff"].as<float>(), 0.0f, 10.0f);
    if (fabsf(v - elecBoilerEff) > 0.00001f) heatCheapConfigChanged = true;
    elecBoilerEff = v;
    changed = true;
  }
  if (in.containsKey("heat_hyst")) {
    const float v = constrain(in["heat_hyst"].as<float>(), 0.0f, 99.0f);
    if (fabsf(v - heatElecMarginCt) > 0.00001f) heatCheapConfigChanged = true;
    heatElecMarginCt = v;
    changed = true;
  }
  if (in.containsKey("heat_minsw")) {
    const int v = max(0, in["heat_minsw"].as<int>());
    if (v != heatMinSwitchMin) heatCheapConfigChanged = true;
    heatMinSwitchMin = v;
    changed = true;
  }

  // Fasebewaking en soft-start: via REST beschikbaar voor Homey/integraties.
  // Dit zijn configuratie-/veiligheidsinstellingen; de bestaande runtime-logica blijft leidend.
  if (in.containsKey("pg_en")) { phaseGuardEnabled = in["pg_en"].as<bool>(); changed = true; }
  if (in.containsKey("pg_ph")) { phaseGuardPhase = (uint8_t)in["pg_ph"].as<int>(); changed = true; }
  if (in.containsKey("pg_maxa")) {
    const float v = in["pg_maxa"].as<float>();
    if (phaseGuardPhase == 1) phaseGuardMaxA_L1 = v;
    else if (phaseGuardPhase == 2) phaseGuardMaxA_L2 = v;
    else if (phaseGuardPhase == 3) phaseGuardMaxA_L3 = v;
    changed = true;
  }
  if (in.containsKey("pg_maxa_l1")) { phaseGuardMaxA_L1 = in["pg_maxa_l1"].as<float>(); changed = true; }
  if (in.containsKey("pg_maxa_l2")) { phaseGuardMaxA_L2 = in["pg_maxa_l2"].as<float>(); changed = true; }
  if (in.containsKey("pg_maxa_l3")) { phaseGuardMaxA_L3 = in["pg_maxa_l3"].as<float>(); changed = true; }
  if (in.containsKey("ss_en"))      { softStartEnabled = in["ss_en"].as<bool>(); changed = true; }
  if (in.containsKey("ss_dur"))     { softStartDurationSec = (uint16_t)in["ss_dur"].as<int>(); changed = true; }
  if (in.containsKey("ss_start"))   { softStartStartPct = in["ss_start"].as<float>(); changed = true; }

  // Legionella-relaisautomatisering via generieke REST-config.
  if (in.containsKey("leg_rel_auto"))  { legRelayAutoEnabled = in["leg_rel_auto"].as<bool>(); changed = true; }
  if (in.containsKey("leg_rel_start")) { legRelayStartDelayMin = in["leg_rel_start"].as<int>(); changed = true; }
  if (in.containsKey("leg_rel_delay")) { legRelayOffDelayMin = in["leg_rel_delay"].as<int>(); changed = true; }

  if (in.containsKey("np_en"))   { npEnabled = in["np_en"].as<bool>(); changed = true; }
  if (in.containsKey("np_area")) { npArea = in["np_area"].as<String>(); changed = true; }
  if (in.containsKey("np_res"))  { npResMin = in["np_res"].as<int>(); changed = true; }
  if (in.containsKey("np_tax_en"))  { npInclTaxEnabled = in["np_tax_en"].as<bool>(); changed = true; }
  if (in.containsKey("np_tax_eb"))  { npEnergyTaxCt = in["np_tax_eb"].as<float>(); changed = true; }
  if (in.containsKey("np_tax_in"))  { npInkoopCt = in["np_tax_in"].as<float>(); changed = true; }
  if (in.containsKey("np_tax_btw")) { npBtwPct = in["np_tax_btw"].as<float>(); changed = true; }
  if (in.containsKey("np_boost_en"))  { npBoostEnabled = in["np_boost_en"].as<bool>(); changed = true; }
  if (in.containsKey("np_boost_src")) { npBoostPriceSource = (uint8_t)in["np_boost_src"].as<int>(); changed = true; }
  if (in.containsKey("np_boost_thr")) { npBoostThresholdCt = in["np_boost_thr"].as<float>(); changed = true; }
  if (in.containsKey("np_boost_n"))   { npBoostNBlocks = in["np_boost_n"].as<int>(); changed = true; }

  if (in.containsKey("gas_tot_en"))  { gasTotalEnabled = in["gas_tot_en"].as<bool>(); changed = true; }
  if (in.containsKey("gas_src"))     { gasPriceSource = (uint8_t)in["gas_src"].as<int>(); changed = true; }
  if (in.containsKey("gas_fix"))     { gasFixedEurM3 = in["gas_fix"].as<float>(); changed = true; }
  if (in.containsKey("gas_tax_eb"))  { gasEnergyTaxEur = in["gas_tax_eb"].as<float>(); changed = true; }
  if (in.containsKey("gas_tax_in"))  { gasInkoopEur = in["gas_tax_in"].as<float>(); changed = true; }
  if (in.containsKey("gas_tax_btw")) { gasBtwPct = in["gas_tax_btw"].as<float>(); changed = true; }

  if (in.containsKey("sys_pub")) { sysPublishEnabled = in["sys_pub"].as<bool>(); changed = true; }
  if (in.containsKey("sys_int")) { sysPublishIntervalSec = in["sys_int"].as<int>(); changed = true; }
  if (in.containsKey("st_int"))  { statusPublishIntervalSec = in["st_int"].as<int>(); changed = true; }

  if (in.containsKey("leg_en"))   { legScheduleEnabled = in["leg_en"].as<bool>(); changed = true; }
  if (in.containsKey("leg_dow"))  { legDow = (uint8_t)in["leg_dow"].as<int>(); changed = true; }
  if (in.containsKey("leg_hr"))   { legStartHour = (uint8_t)in["leg_hr"].as<int>(); changed = true; }
  if (in.containsKey("leg_min"))  { legStartMinute = (uint8_t)in["leg_min"].as<int>(); changed = true; }
  if (in.containsKey("leg_dur"))  { legDurationMin = (uint16_t)in["leg_dur"].as<int>(); changed = true; }
  if (in.containsKey("leg_pct"))  { legOutputLimitPercent = in["leg_pct"].as<float>(); changed = true; }
  if (in.containsKey("leg_ts"))   { legUseTempSensor = in["leg_ts"].as<bool>(); changed = true; }
  if (in.containsKey("leg_taddr")) {
    String v = in["leg_taddr"].as<String>();
    v.trim(); v.toLowerCase();
    if (v != legTempAddrStr) heatCheapConfigChanged = true;
    legTempAddrStr = v; changed = true;
  }
  if (in.containsKey("leg_tc")) {
    const float v = in["leg_tc"].as<float>();
    if (fabsf(v - legTargetTempC) > 0.00001f) heatCheapConfigChanged = true;
    legTargetTempC = v; changed = true;
  }
  if (in.containsKey("leg_hold")) { legHoldMin = in["leg_hold"].as<int>(); changed = true; }
  if (in.containsKey("leg_lrdi")) { legLastRunDateInt = (uint32_t)in["leg_lrdi"].as<unsigned long>(); changed = true; }
  if (in.containsKey("leg_lrep")) { legLastRunEpoch = (uint32_t)in["leg_lrep"].as<unsigned long>(); changed = true; }

  if (in.containsKey("ts_map"))  { tempSensorMapJson = in["ts_map"].as<String>(); changed = true; }
  if (in.containsKey("ts_ctrl")) { tempControlAddrStr = in["ts_ctrl"].as<String>(); changed = true; }
  if (in.containsKey("ts_pub"))  { tempPublishNamesToHA = in["ts_pub"].as<bool>(); changed = true; }

  if (in.containsKey("fw_auto")) { fwAutoUpdateEnabled = in["fw_auto"].as<bool>(); changed = true; }

  if (!changed) return true;

  clampAll();
  if (heatCheapConfigChanged) heatCheapPlanInvalidate();
  saveConfig();
  return true;
}

void handleApiPing() {
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}

void handleApiRelayPost() {
  bool hasOn = false;
  bool on = false;

  if (server.hasArg("plain") && server.arg("plain").length() > 0) {
    DynamicJsonDocument doc(256);
    String err;
    if (!apiGetJsonBody(doc, &err)) {
      apiSendJson(400, String("{\"ok\":false,\"error\":\"") + err + "\"}");
      return;
    }
    if (doc.containsKey("on")) {
      hasOn = true;
      on = doc["on"].as<bool>();
    }
  }

  if (!hasOn && server.hasArg("on")) {
    String v = server.arg("on");
    v.trim();
    v.toLowerCase();
    hasOn = true;
    on = (v == "1" || v == "true" || v == "on" || v == "yes");
  }

  if (!hasOn) {
    apiSendJson(400, "{\"ok\":false,\"error\":\"missing_on\"}");
    return;
  }

  apiRelaySetAndRespond(on, true);
}

void handleApiRelayOnPost() {
  apiRelaySetAndRespond(true, true);
}

void handleApiRelayOffPost() {
  apiRelaySetAndRespond(false, false);
}

void handleApiConfigGet() {
  const bool reveal = server.hasArg("reveal") && server.arg("reveal") == "1";

  DynamicJsonDocument doc(8192);

  doc["wifi_ssid"] = wifiSsid;
  doc["wifi_pass"] = (reveal ? wifiPass : (wifiPass.length() ? "********" : ""));
  doc["device_name"] = deviceName;

  doc["meter_type"] = meterType;
  doc["meter_ip"] = meterIp;

  doc["ctrl_mode"] = (uint8_t)(((uint8_t)controlMode == 1) ? 1 : 0);

  doc["max_power"] = maxBoilerPowerW;
  doc["target_export"] = targetExportW;
  doc["min_step"] = minStepPercent;
  doc["min_interval"] = minUpdateIntervalMs;
  doc["max_out_pct"] = maxOutputPercentLimit;

  doc["n_alg"] = normalAlg;
  doc["n_sf"] = stepFactor;
  doc["n_dbw"] = deadbandW;
  doc["n_msp"] = maxStepPctPerUpdatePerUpdate;

  doc["sun_s_mode"] = sunSchedMode;
  doc["sun_w_s"] = sunWinStartMin;
  doc["sun_w_e"] = sunWinEndMin;
  doc["sun_c_n"] = sunCheapNBlocks;
  doc["sun_marg"] = sunDaylightMarginMin;
  doc["sun_lat"] = sunLat;
  doc["sun_lon"] = sunLon;
  doc["dash_sun_lbl"] = dashShowSunWindowLabel;

  doc["mqtt_enabled"] = mqttEnabled;
  doc["mqtt_host"] = mqttHost;
  doc["mqtt_port"] = mqttPort;
  doc["mqtt_user"] = mqttUser;
  doc["mqtt_pass"] = (reveal ? mqttPassStr : (mqttPassStr.length() ? "********" : ""));
  doc["mqtt_topic"] = mqttBaseTopic;

  doc["ha_disc"] = haDiscoveryEnabled;

  doc["elec_src"] = elecPriceSource;
  doc["elec_fix_ct"] = elecFixedCtKwhAllIn;

  doc["heat_auto"] = heatAutoEnabled;
  doc["heat_exec_el"] = heatExecElecWhenCheaper;
  doc["heat_ch_en"] = heatCheapBlocksEnabled;
  doc["heat_ch_bt"] = heatCheapUseBoilerTarget;
  doc["heat_ch_n"] = heatCheapNBlocks;
  doc["heat_kwhm3"] = gasKwhPerM3;
  doc["heat_geff"] = gasBoilerEff;
  doc["heat_eeff"] = elecBoilerEff;
  doc["heat_hyst"] = heatElecMarginCt;
  doc["heat_minsw"] = heatMinSwitchMin;

  doc["pg_en"] = phaseGuardEnabled;
  doc["pg_ph"] = phaseGuardPhase;
  doc["pg_maxa"] = getConfiguredPhaseGuardMaxA(phaseGuardPhase);
  doc["pg_maxa_l1"] = phaseGuardMaxA_L1;
  doc["pg_maxa_l2"] = phaseGuardMaxA_L2;
  doc["pg_maxa_l3"] = phaseGuardMaxA_L3;
  doc["ss_en"] = softStartEnabled;
  doc["ss_dur"] = softStartDurationSec;
  doc["ss_start"] = softStartStartPct;

  doc["np_en"] = npEnabled;
  doc["np_area"] = npArea;
  doc["np_res"] = npResMin;
  doc["np_tax_en"] = npInclTaxEnabled;
  doc["np_tax_eb"] = npEnergyTaxCt;
  doc["np_tax_in"] = npInkoopCt;
  doc["np_tax_btw"] = npBtwPct;
  doc["np_boost_en"] = npBoostEnabled;
  doc["np_boost_src"] = npBoostPriceSource;
  doc["np_boost_thr"] = npBoostThresholdCt;
  doc["np_boost_n"] = npBoostNBlocks;

  doc["gas_tot_en"] = gasTotalEnabled;
  doc["gas_src"] = gasPriceSource;
  doc["gas_fix"] = gasFixedEurM3;
  doc["gas_tax_eb"] = gasEnergyTaxEur;
  doc["gas_tax_in"] = gasInkoopEur;
  doc["gas_tax_btw"] = gasBtwPct;

  doc["sys_pub"] = sysPublishEnabled;
  doc["sys_int"] = sysPublishIntervalSec;
  doc["st_int"] = statusPublishIntervalSec;

  doc["leg_en"] = legScheduleEnabled;
  doc["leg_dow"] = legDow;
  doc["leg_hr"] = legStartHour;
  doc["leg_min"] = legStartMinute;
  doc["leg_dur"] = legDurationMin;
  doc["leg_pct"] = legOutputLimitPercent;
  doc["leg_ts"] = legUseTempSensor;
  doc["leg_taddr"] = legTempAddrStr;
  doc["leg_tc"] = legTargetTempC;
  doc["leg_hold"] = legHoldMin;
  doc["leg_rel_auto"] = legRelayAutoEnabled;
  doc["leg_rel_start"] = legRelayStartDelayMin;
  doc["leg_rel_delay"] = legRelayOffDelayMin;
  doc["leg_lrdi"] = legLastRunDateInt;
  doc["leg_lrep"] = legLastRunEpoch;

  doc["ts_map"] = tempSensorMapJson;
  doc["ts_ctrl"] = tempControlAddrStr;
  doc["ts_pub"] = tempPublishNamesToHA;

  doc["fw_auto"] = fwAutoUpdateEnabled;

  String out;
  serializeJson(doc, out);
  apiSendJson(200, out);
}

void handleApiConfigPost() {
  DynamicJsonDocument doc(8192);
  String err;
  if (!apiGetJsonBody(doc, &err)) {
    apiSendJson(400, String("{\"ok\":false,\"error\":\"") + err + "\"}");
    return;
  }

  bool rebootRequired = false;
  bool mqttParamsChanged = false;

  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  apiMaybeReconnectMqtt(mqttParamsChanged);
  apiPublishAllRelevantState();

  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  resp["reboot_required"] = rebootRequired;
  String out;
  serializeJson(resp, out);
  apiSendJson(200, out);
}

void handleApiForceHeatPost() {
  DynamicJsonDocument doc(256);
  String err;
  if (!apiGetJsonBody(doc, &err)) {
    apiSendJson(400, String("{\"ok\":false,\"error\":\"") + err + "\"}");
    return;
  }
  if (!doc.containsKey("on")) {
    apiSendJson(400, "{\"ok\":false,\"error\":\"missing_on\"}");
    return;
  }
  bool on = doc["on"].as<bool>();

  const bool wasForce = ((uint8_t)controlMode == 1);
  controlMode = on ? (ControlMode)1 : (ControlMode)0;
  if (wasForce && !on) {
    setBoilerDutyPercent(0.0f);
  }
  saveConfig();
  mqttPublishForceState(true);
  mqttPublishMultiControllerStates(true);
  mqttPublishStatusJson(true);

  apiSendJson(200, "{\"ok\":true}");
}

void handleApiLegionellaRunNowPost() {
  DynamicJsonDocument doc(256);
  String err;
  if (!apiGetJsonBody(doc, &err)) {
    apiSendJson(400, String("{\"ok\":false,\"error\":\"") + err + "\"}");
    return;
  }
  bool startNow = doc.containsKey("start") ? doc["start"].as<bool>() : true;
  if (startNow) {
    legStart("REST");
    mqttPublishLegionellaState(true);
    mqttPublishStatusJson(true);
  }
  apiSendJson(200, "{\"ok\":true}");
}

void handleApiLegionellaCancelPost() {
  if (legRunning) {
    legStop("REST_CANCEL");
  }
  mqttPublishLegionellaState(true);
  mqttPublishStatusJson(true);
  apiSendJson(200, "{\"ok\":true}");
}

void handleApiMultiControllerTargetsPost() {
  DynamicJsonDocument doc(2048);
  String err;
  if (!apiGetJsonBody(doc, &err)) {
    apiSendJson(400, String("{\"ok\":false,\"error\":\"") + err + "\"}");
    return;
  }
  if (!multiControllerApplyRemoteTargetsDocument(doc, err)) {
    apiSendJson(409, String("{\"ok\":false,\"error\":\"") + err + "\"}");
    return;
  }
  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  resp["mc_remote_target_active"] = multiControllerIsRemoteTargetControlActive();
  resp["mc_remote_target_age_ms"] = multiControllerGetRemoteTargetAgeMs();
  resp["mc_remote_target_owner_ip"] = multiControllerGetRemoteTargetOwnerIp();
  String out;
  serializeJson(resp, out);
  apiSendJson(200, out);
}

void handleApiRebootPost() {
  DynamicJsonDocument doc(128);
  String err;
  if (!apiGetJsonBody(doc, &err)) {
    apiSendJson(400, String("{\"ok\":false,\"error\":\"") + err + "\"}");
    return;
  }
  if (!doc.containsKey("reboot") || !doc["reboot"].as<bool>()) {
    apiSendJson(400, "{\"ok\":false,\"error\":\"missing_reboot_true\"}");
    return;
  }
  apiSendJson(200, "{\"ok\":true}");
  delay(200);
  ESP.restart();
}

void handleFwSetAuto() {
  String v = server.arg("value");
  bool en = (v == "1" || v == "true" || v == "on" || v == "yes");

  fwAutoUpdateEnabled = en;
  saveConfig();

  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "application/json; charset=utf-8", String("{\"ok\":true,\"auto\":") + (fwAutoUpdateEnabled ? "true" : "false") + "}");
}
