#include "mqtt.h"
#include "app_globals.h"
#include "MqttParsers.h"
#include "MqttTuning.h"
#include "RelayControl.h"
#include <ArduinoJson.h>  // Step 7 prep: for future mqttCallback splitting

// Bestaande functies (blijven in de .ino)
extern void mqttSetup();
extern void mqttLoop();



// ===== moved from .ino (Step 5) =====
String mqttBase() {
  String base = mqttBaseTopic;
  if (base.length() == 0) base = DEVICE_ID;
  if (base.endsWith("/")) base.remove(base.length() - 1);
  return base;
}

String mqttAvailabilityTopic() { return mqttBase() + "/availability"; }

String getMqttClientId() {
  uint32_t chipId = (uint32_t)ESP.getEfuseMac();
  char buf[32];
  snprintf(buf, sizeof(buf), "SolarController-%06X", (unsigned int)(chipId & 0xFFFFFF));
  return String(buf);
}
// ===== end moved =====


// ===== helper moved from mqttCallback prep (Step 6 - small) =====
static String mqttPayloadToString(byte* payload, unsigned int length) {
  String p;
  p.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) p += (char)payload[i];
  return p;
}
// ===== end helper =====


static void publishRetainedStateBundle(bool includeDiscovery, bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;

  if (includeDiscovery && haDiscoveryEnabled) {
    mqttPublishHADiscovery(true);
  }

  mqttPublishForceState(forceNow);
  mqttPublishRelayState(forceNow);
  mqttPublishMultiControllerStates(forceNow);
  mqttPublishLegionellaConfig(forceNow);
  mqttPublishLegionellaState(forceNow);
  mqttPublishTuningStates(forceNow);
  mqttPublishLimitsStates(forceNow);
  mqttPublishNormalConfigStates(forceNow);
  mqttPublishLegionellaPlanStates(forceNow);
  mqttPublishSunStrategyStates(forceNow);
  mqttPublishElecPriceStates(forceNow);
  mqttPublishHeatCheapConfigStates(forceNow);
  mqttPublishNordpoolStates(forceNow);
  mqttPublishStatusJson(forceNow);
  mqttPublishSystemJson(forceNow);
}

static void subscribeControlTopics(const String& base) {
  // subscribe control
  mqttClient.subscribe((base + "/force_heat/set").c_str());
  mqttClient.subscribe((base + "/relay/set").c_str());
  mqttClient.subscribe((base + "/legionella/run_now").c_str());
  mqttClient.subscribe((base + "/legionella/cancel").c_str());
  mqttClient.subscribe((base + "/legionella/enable/set").c_str());
  mqttClient.subscribe((base + "/legionella/config/set").c_str());

  // subscribe tuning
  mqttClient.subscribe((base + "/tuning/set").c_str());
  mqttClient.subscribe((base + "/tuning/algorithm/set").c_str());
  mqttClient.subscribe((base + "/tuning/step_factor/set").c_str());
  mqttClient.subscribe((base + "/tuning/deadband_w/set").c_str());
  mqttClient.subscribe((base + "/tuning/max_step_pct/set").c_str());

  // subscribe safety limits
  mqttClient.subscribe((base + "/limits/max_output_pct/set").c_str());
  mqttClient.subscribe((base + "/limits/legionella_output_pct/set").c_str());

  // subscribe NORMAL config
  mqttClient.subscribe((base + "/normal/target_export_w/set").c_str());
  mqttClient.subscribe((base + "/normal/max_boiler_power_w/set").c_str());
  mqttClient.subscribe((base + "/normal/min_step_percent/set").c_str());
  mqttClient.subscribe((base + "/normal/min_interval_s/set").c_str());

  // subscribe Energy Strategy (sun scheduling)
  mqttClient.subscribe((base + "/sun/sched_mode/set").c_str());
  mqttClient.subscribe((base + "/sun/manual_start/set").c_str());
  mqttClient.subscribe((base + "/sun/manual_end/set").c_str());
  mqttClient.subscribe((base + "/sun/cheapest_blocks/set").c_str());
  mqttClient.subscribe((base + "/sun/daylight_margin_min/set").c_str());
  mqttClient.subscribe((base + "/sun/lat/set").c_str());
  mqttClient.subscribe((base + "/sun/lon/set").c_str());
  mqttClient.subscribe((base + "/sun/dashboard_label/set").c_str());

  // subscribe Elektra prijsbron (vast/dynamisch)
  mqttClient.subscribe((base + "/elec/src/set").c_str());
  mqttClient.subscribe((base + "/elec/fixed_ct/set").c_str());

  // subscribe Gas vs Elektra + slimme ELEC-blokken
  mqttClient.subscribe((base + "/heat/auto/set").c_str());
  mqttClient.subscribe((base + "/heat/exec_el/set").c_str());
  mqttClient.subscribe((base + "/heat/cheap_blocks/enabled/set").c_str());
  mqttClient.subscribe((base + "/heat/cheap_blocks/use_boiler_target/set").c_str());
  mqttClient.subscribe((base + "/heat/cheap_blocks/max/set").c_str());

  // subscribe Nordpool settings
  mqttClient.subscribe((base + "/np/enabled/set").c_str());
  mqttClient.subscribe((base + "/np/area/set").c_str());
  mqttClient.subscribe((base + "/np/res_min/set").c_str());
  mqttClient.subscribe((base + "/np/incl_taxes/set").c_str());
  mqttClient.subscribe((base + "/np/energy_tax_ct/set").c_str());
  mqttClient.subscribe((base + "/np/inkoop_ct/set").c_str());
  mqttClient.subscribe((base + "/np/btw_pct/set").c_str());
  mqttClient.subscribe((base + "/np/boost_enabled/set").c_str());
  mqttClient.subscribe((base + "/np/boost_price_source/set").c_str());
  mqttClient.subscribe((base + "/np/boost_threshold_ct/set").c_str());
  mqttClient.subscribe((base + "/np/boost_n_blocks/set").c_str());

  // subscribe Legionella planning (individual)
  mqttClient.subscribe((base + "/legionella/dow/set").c_str());
  mqttClient.subscribe((base + "/legionella/start_hour/set").c_str());
  mqttClient.subscribe((base + "/legionella/start_minute/set").c_str());
  mqttClient.subscribe((base + "/legionella/duration_min/set").c_str());
  mqttClient.subscribe((base + "/legionella/use_temp_sensor/set").c_str());
  mqttClient.subscribe((base + "/legionella/target_temp_c/set").c_str());
  mqttClient.subscribe((base + "/legionella/hold_min/set").c_str());

  // subscribe HA birth (optioneel)
  if (haDiscoveryEnabled) {
    mqttClient.subscribe((String(HA_DISCOVERY_PREFIX) + "/status").c_str());
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  const String t = String(topic);
  const String p = mqttPayloadToString(payload, length);
  const String base = mqttBase();

  if (MQTT::handleTuningMaxStepPct(t, p, base)) return;

  if (haDiscoveryEnabled && t == String(HA_DISCOVERY_PREFIX) + "/status") {
    String s = p;
    s.trim();
    s.toLowerCase();
    if (s == "online") {
      publishRetainedStateBundle(true, true);
    }
    return;
  }

  if (MQTT::handleForceHeat(t, p, base)) return;
  if (MQTT::handleRelaySet(t, p, base)) return;
  if (MQTT::handleLegionellaRunNow(t, p, base)) return;
  if (MQTT::handleLegionellaCancel(t, p, base)) return;
  if (MQTT::handleLegionellaConfigSet(t, p, base)) return;
  if (MQTT::handleTuningSet(t, p, base)) return;
  if (MQTT::handleTuningAlgorithm(t, p, base)) return;
  if (MQTT::handleTuningStepFactor(t, p, base)) return;
  if (MQTT::handleTuningDeadbandW(t, p, base)) return;
  if (MQTT::handleLegScheduleEnabled(t, p, base)) return;
  if (MQTT::handleLegDow(t, p, base)) return;
  if (MQTT::handleLegStartHour(t, p, base)) return;
  if (MQTT::handleLegStartMinute(t, p, base)) return;
  if (MQTT::handleLegDurationMin(t, p, base)) return;
  if (MQTT::handleLegUseTempSensor(t, p, base)) return;
  if (MQTT::handleLegTargetTempC(t, p, base)) return;
  if (MQTT::handleLegHoldMin(t, p, base)) return;
  if (MQTT::handleLimitsMaxOutputPct(t, p, base)) return;
  if (MQTT::handleLimitsLegionellaOutputPct(t, p, base)) return;
  if (MQTT::handleNormalTargetExportW(t, p, base)) return;
  if (MQTT::handleNormalMaxBoilerPowerW(t, p, base)) return;
  if (MQTT::handleNormalMinStepPercent(t, p, base)) return;
  if (MQTT::handleNormalMinIntervalS(t, p, base)) return;
  if (MQTT::handleElecPriceSource(t, p, base)) return;
  if (MQTT::handleElecFixedCtKwhAllIn(t, p, base)) return;
  if (MQTT::handleHeatAutoEnabled(t, p, base)) return;
  if (MQTT::handleHeatExecElec(t, p, base)) return;
  if (MQTT::handleHeatCheapBlocksEnabled(t, p, base)) return;
  if (MQTT::handleHeatCheapUseBoilerTarget(t, p, base)) return;
  if (MQTT::handleHeatCheapBlocksMax(t, p, base)) return;
  if (MQTT::handleSunSchedMode(t, p, base)) return;
  if (MQTT::handleSunManualStart(t, p, base)) return;
  if (MQTT::handleSunManualEnd(t, p, base)) return;
  if (MQTT::handleSunCheapestBlocks(t, p, base)) return;
  if (MQTT::handleSunDaylightMarginMin(t, p, base)) return;
  if (MQTT::handleSunLat(t, p, base)) return;
  if (MQTT::handleSunLon(t, p, base)) return;
  if (MQTT::handleSunDashboardLabel(t, p, base)) return;
  if (MQTT::handleNpEnabled(t, p, base)) return;
  if (MQTT::handleNpArea(t, p, base)) return;
  if (MQTT::handleNpResMin(t, p, base)) return;
  if (MQTT::handleNpInclTaxes(t, p, base)) return;
  if (MQTT::handleNpEnergyTaxCt(t, p, base)) return;
  if (MQTT::handleNpInkoopCt(t, p, base)) return;
  if (MQTT::handleNpBtwPct(t, p, base)) return;
  if (MQTT::handleNpBoostEnabled(t, p, base)) return;
  if (MQTT::handleNpBoostPriceSource(t, p, base)) return;
  if (MQTT::handleNpBoostThresholdCt(t, p, base)) return;
  if (MQTT::handleNpBoostNBlocks(t, p, base)) return;
}

namespace MQTT {

// Handle Force Heat MQTT command.
// Returns true if the topic matched and was handled (so mqttCallback can return).
bool handleForceHeat(const String& t, const String& p, const String& base) {
  if (t != base + "/force_heat/set") return false;

  bool on;
  if (parseOnOff(p, on)) {
    const bool wasForce = ((uint8_t)controlMode == 1);  // 1=MODE_FORCE_HEAT
    controlMode = on ? (ControlMode)1 : (ControlMode)0;  // 1=MODE_FORCE_HEAT, 0=MODE_NORMAL
    if (wasForce && !on) {
      // MQTT: direct naar 0% bij Force uit, net als via UI/API.
      setBoilerDutyPercent(0.0f);
    }
    saveConfig();
    mqttPublishForceState(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

// Handle Relay MQTT command.
// Returns true if the topic matched and was handled (so mqttCallback can return).
bool handleRelaySet(const String& t, const String& p, const String& base) {
  if (t != base + "/relay/set") return false;

  bool on;
  if (parseOnOff(p, on)) {
    if (relayIsEnabled()) {
      relaySetManualOn(on);
    } else {
      relaySetManualOn(false);
    }
    mqttPublishRelayState(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

// Handle Legionella "run now" MQTT command.
// Returns true if the topic matched and was handled (so mqttCallback can return).
bool handleLegionellaRunNow(const String& t, const String& p, const String& base) {
  if (t != base + "/legionella/run_now") return false;

  bool on;
  if (parseOnOff(p, on) && on) {
    legStart("MANUAL");
    mqttPublishLegionellaState(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

// Handle Legionella "cancel" MQTT command.
// Returns true if the topic matched and was handled (so mqttCallback can return).
bool handleLegionellaCancel(const String& t, const String& p, const String& base) {
  if (t != base + "/legionella/cancel") return false;

  bool on;
  if (parseOnOff(p, on) && on) {
    if (legRunning) legStop("CANCEL");
    mqttPublishLegionellaState(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

// Handle Legionella config JSON payload on topic ".../legionella/config/set"
// Returns true if the topic matched and was handled (so mqttCallback can return).
bool handleLegionellaConfigSet(const String& t, const String& p, const String& base) {
  if (t != base + "/legionella/config/set") return false;

  // legacy / existing config JSON handler (kept same as v3.9.1)
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, p) != DeserializationError::Ok) return true;

  if (doc.containsKey("schedule_enabled")) legScheduleEnabled = doc["schedule_enabled"].as<bool>();
  if (doc.containsKey("dow"))    legDow = doc["dow"].as<int>();
  if (doc.containsKey("hour"))   legStartHour = doc["hour"].as<int>();
  if (doc.containsKey("minute")) legStartMinute = doc["minute"].as<int>();
  if (doc.containsKey("duration_min")) legDurationMin = doc["duration_min"].as<int>();
  if (doc.containsKey("output_limit_percent")) legOutputLimitPercent = doc["output_limit_percent"].as<float>();

  if (doc.containsKey("use_temp_sensor")) legUseTempSensor = doc["use_temp_sensor"].as<bool>();
  if (doc.containsKey("target_temp_c"))   legTargetTempC = doc["target_temp_c"].as<float>();
  if (doc.containsKey("hold_min"))        legHoldMin = doc["hold_min"].as<int>();

  clampAll();
  saveConfig();
  mqttPublishLegionellaConfig(true);
  mqttPublishLegionellaState(true);
  mqttPublishStatusJson(true);
  return true;
}

// Handle NORMAL tuning JSON payload on topic ".../tuning/set"
// Returns true if the topic matched and was handled (so mqttCallback can return).
bool handleTuningSet(const String& t, const String& p, const String& base) {
  if (t != base + "/tuning/set") return false;
  applyTuningFromJson(p);
  return true;
}

// Handle individual tuning topics
bool handleTuningAlgorithm(const String& t, const String& p, const String& base) {
  if (t != base + "/tuning/algorithm/set") return false;
  setTuningAlgorithm(p);
  return true;
}
bool handleTuningStepFactor(const String& t, const String& p, const String& base) {
  if (t != base + "/tuning/step_factor/set") return false;
  setTuningStepFactor(p);
  return true;
}
bool handleTuningDeadbandW(const String& t, const String& p, const String& base) {
  if (t != base + "/tuning/deadband_w/set") return false;
  setTuningDeadband(p);
  return true;
}

// ===== SAFETY LIMITS handlers =====
bool handleLimitsMaxOutputPct(const String& t, const String& p, const String& base) {
  if (t != base + "/limits/max_output_pct/set") return false;

  DynamicJsonDocument doc(64);
  doc["max_out_pct"] = p.toFloat();

  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);

  mqttPublishLimitsStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleLimitsLegionellaOutputPct(const String& t, const String& p, const String& base) {
  if (t != base + "/limits/legionella_output_pct/set") return false;

  DynamicJsonDocument doc(64);
  doc["leg_pct"] = p.toFloat();

  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);

  mqttPublishLimitsStates(true);
  mqttPublishLegionellaConfig(true);
  mqttPublishStatusJson(true);
  return true;
}

// ===== NORMAL CONFIG handlers =====
bool handleNormalTargetExportW(const String& t, const String& p, const String& base) {
  if (t != base + "/normal/target_export_w/set") return false;

  DynamicJsonDocument doc(64);
  doc["target_export"] = (long)p.toFloat();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishNormalConfigStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleNormalMaxBoilerPowerW(const String& t, const String& p, const String& base) {
  if (t != base + "/normal/max_boiler_power_w/set") return false;

  DynamicJsonDocument doc(64);
  doc["max_power"] = p.toFloat();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishNormalConfigStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleNormalMinStepPercent(const String& t, const String& p, const String& base) {
  if (t != base + "/normal/min_step_percent/set") return false;

  DynamicJsonDocument doc(64);
  doc["min_step"] = p.toFloat();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishNormalConfigStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleNormalMinIntervalS(const String& t, const String& p, const String& base) {
  if (t != base + "/normal/min_interval_s/set") return false;

  float sec = p.toFloat();
  if (sec < 0) sec = 0;
  DynamicJsonDocument doc(64);
  doc["min_interval"] = (unsigned long)(sec * 1000.0f);
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishNormalConfigStates(true);
  mqttPublishStatusJson(true);
  return true;
}

// ===== SUN STRATEGY handlers =====
static bool applyJsonKeyFloat(const char* key, float v) {
  DynamicJsonDocument doc(64);
  doc[key] = v;
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  return mqttParamsChanged;
}
static bool applyJsonKeyInt(const char* key, long v) {
  DynamicJsonDocument doc(64);
  doc[key] = v;
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  return mqttParamsChanged;
}

bool handleSunSchedMode(const String& t, const String& p, const String& base) {
  if (t != base + "/sun/sched_mode/set") return false;
  String x = p;
  x.trim();
  x.toUpperCase();
  uint8_t mode = 0;
  if (x == "1" || x == "HANDMATIG" || x == "MANUAL") mode = 1;
  else if (x == "2" || x == "CHEAPEST" || x == "CHEAPEST DAYLIGHT" || x.indexOf("SLIM") >= 0) mode = 2;
  else if (x == "3" || x == "UIT" || x == "OFF") mode = 3;
  applyJsonKeyInt("sun_s_mode", mode);
  mqttPublishSunStrategyStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleSunManualStart(const String& t, const String& p, const String& base) {
  if (t != base + "/sun/manual_start/set") return false;
  int outMin = p.toInt();
  if (outMin < 0) outMin = 0;
  if (outMin > 24*60) outMin = 24*60;
  applyJsonKeyInt("sun_w_s", outMin);
  mqttPublishSunStrategyStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleSunManualEnd(const String& t, const String& p, const String& base) {
  if (t != base + "/sun/manual_end/set") return false;
  int outMin = p.toInt();
  if (outMin < 0) outMin = 0;
  if (outMin > 24*60) outMin = 24*60;
  applyJsonKeyInt("sun_w_e", outMin);
  mqttPublishSunStrategyStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleSunCheapestBlocks(const String& t, const String& p, const String& base) {
  if (t != base + "/sun/cheapest_blocks/set") return false;
  applyJsonKeyInt("sun_c_n", p.toInt());
  mqttPublishSunStrategyStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleSunDaylightMarginMin(const String& t, const String& p, const String& base) {
  if (t != base + "/sun/daylight_margin_min/set") return false;
  applyJsonKeyInt("sun_marg", p.toInt());
  mqttPublishSunStrategyStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleSunLat(const String& t, const String& p, const String& base) {
  if (t != base + "/sun/lat/set") return false;
  applyJsonKeyFloat("sun_lat", p.toFloat());
  mqttPublishSunStrategyStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleSunLon(const String& t, const String& p, const String& base) {
  if (t != base + "/sun/lon/set") return false;
  applyJsonKeyFloat("sun_lon", p.toFloat());
  mqttPublishSunStrategyStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleSunDashboardLabel(const String& t, const String& p, const String& base) {
  if (t != base + "/sun/dashboard_label/set") return false;
  bool b;
  if (parseOnOff(p, b)) {
    DynamicJsonDocument doc(64);
    doc["dash_sun_lbl"] = b;
    bool rebootRequired=false, mqttParamsChanged=false;
    apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
    mqttPublishSunStrategyStates(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

// ===== ELEKTRA PRIJSBRON (vast/dynamisch) handlers =====
bool handleElecPriceSource(const String& t, const String& p, const String& base) {
  if (t != base + "/elec/src/set") return false;
  String x = p; x.trim(); x.toLowerCase();
  uint8_t v = 0;
  if (x == "1" || x == "fixed" || x == "vast" || x.indexOf("vast") >= 0) v = 1;
  DynamicJsonDocument doc(64);
  doc["elec_src"] = v;
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishElecPriceStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleElecFixedCtKwhAllIn(const String& t, const String& p, const String& base) {
  if (t != base + "/elec/fixed_ct/set") return false;
  String x = p; x.trim(); x.replace(",", ".");
  DynamicJsonDocument doc(64);
  doc["elec_fix_ct"] = x.toFloat();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishElecPriceStates(true);
  mqttPublishStatusJson(true);
  return true;
}


// ===== GAS VS ELEKTRA handlers =====
bool handleHeatAutoEnabled(const String& t, const String& p, const String& base) {
  if (t != base + "/heat/auto/set") return false;

  bool b;
  if (parseOnOff(p, b)) {
    DynamicJsonDocument doc(64);
    doc["heat_auto"] = b;
    bool rebootRequired=false, mqttParamsChanged=false;
    apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
    mqttPublishHeatCheapConfigStates(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

bool handleHeatExecElec(const String& t, const String& p, const String& base) {
  if (t != base + "/heat/exec_el/set") return false;

  bool b;
  if (parseOnOff(p, b)) {
    DynamicJsonDocument doc(64);
    doc["heat_exec_el"] = b;
    bool rebootRequired=false, mqttParamsChanged=false;
    apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
    mqttPublishHeatCheapConfigStates(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

// ===== SLIMME ELEC-BLOKKEN handlers =====
bool handleHeatCheapBlocksEnabled(const String& t, const String& p, const String& base) {
  if (t != base + "/heat/cheap_blocks/enabled/set") return false;

  bool b;
  if (parseOnOff(p, b)) {
    DynamicJsonDocument doc(64);
    doc["heat_ch_en"] = b;
    bool rebootRequired=false, mqttParamsChanged=false;
    apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
    mqttPublishHeatCheapConfigStates(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

bool handleHeatCheapUseBoilerTarget(const String& t, const String& p, const String& base) {
  if (t != base + "/heat/cheap_blocks/use_boiler_target/set") return false;

  bool b;
  if (parseOnOff(p, b)) {
    DynamicJsonDocument doc(64);
    doc["heat_ch_bt"] = b;
    bool rebootRequired=false, mqttParamsChanged=false;
    apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
    mqttPublishHeatCheapConfigStates(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

bool handleHeatCheapBlocksMax(const String& t, const String& p, const String& base) {
  if (t != base + "/heat/cheap_blocks/max/set") return false;

  DynamicJsonDocument doc(64);
  doc["heat_ch_n"] = p.toInt();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishHeatCheapConfigStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleNpEnabled(const String& t, const String& p, const String& base) {
  if (t != base + "/np/enabled/set") return false;
  bool b;
  if (parseOnOff(p, b)) {
    npEnabled = b;
    clampAll();
    saveConfig();
    mqttPublishNordpoolStates(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

bool handleNpArea(const String& t, const String& p, const String& base) {
  if (t != base + "/np/area/set") return false;
  String x = p; x.trim();
  if (x.length() > 0) {
    npArea = x;
    clampAll();
    saveConfig();
    mqttPublishNordpoolStates(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

bool handleNpResMin(const String& t, const String& p, const String& base) {
  if (t != base + "/np/res_min/set") return false;
  int r = p.toInt();
  if (r != 15 && r != 60) {
    // also accept "15m"/"60m"
    String x = p; x.trim(); x.toLowerCase();
    if (x.startsWith("15")) r = 15;
    else if (x.startsWith("60")) r = 60;
  }
  if (r == 15 || r == 60) {
    npResMin = r;
    clampAll();
    saveConfig();
    mqttPublishNordpoolStates(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

bool handleNpInclTaxes(const String& t, const String& p, const String& base) {
  if (t != base + "/np/incl_taxes/set") return false;
  bool b;
  if (parseOnOff(p, b)) {
    npInclTaxEnabled = b;
    clampAll();
    saveConfig();
    mqttPublishNordpoolStates(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

bool handleNpEnergyTaxCt(const String& t, const String& p, const String& base) {
  if (t != base + "/np/energy_tax_ct/set") return false;
  DynamicJsonDocument doc(64);
  doc["np_tax_eb"] = p.toFloat();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishNordpoolStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleNpInkoopCt(const String& t, const String& p, const String& base) {
  if (t != base + "/np/inkoop_ct/set") return false;
  DynamicJsonDocument doc(64);
  doc["np_tax_in"] = p.toFloat();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishNordpoolStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleNpBtwPct(const String& t, const String& p, const String& base) {
  if (t != base + "/np/btw_pct/set") return false;
  DynamicJsonDocument doc(64);
  doc["np_tax_btw"] = p.toFloat();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishNordpoolStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleNpBoostEnabled(const String& t, const String& p, const String& base) {
  if (t != base + "/np/boost_enabled/set") return false;
  bool b;
  if (parseOnOff(p, b)) {
    DynamicJsonDocument doc(64);
    doc["np_boost_en"] = b;
    bool rebootRequired=false, mqttParamsChanged=false;
    apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
    mqttPublishNordpoolStates(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

bool handleNpBoostPriceSource(const String& t, const String& p, const String& base) {
  if (t != base + "/np/boost_price_source/set") return false;
  String x = p; x.trim(); x.toLowerCase();
  uint8_t v = npBoostPriceSource;
  if (x == "total" || x == "1") v = 1;
  else if (x == "spot" || x == "0") v = 0;

  DynamicJsonDocument doc(64);
  doc["np_boost_src"] = v;
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishNordpoolStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleNpBoostThresholdCt(const String& t, const String& p, const String& base) {
  if (t != base + "/np/boost_threshold_ct/set") return false;
  DynamicJsonDocument doc(64);
  doc["np_boost_thr"] = p.toFloat();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishNordpoolStates(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleNpBoostNBlocks(const String& t, const String& p, const String& base) {
  if (t != base + "/np/boost_n_blocks/set") return false;
  DynamicJsonDocument doc(64);
  doc["np_boost_n"] = p.toInt();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishNordpoolStates(true);
  mqttPublishStatusJson(true);
  return true;
}

// ===== LEGIONELLA PLANNING handlers =====
bool handleLegScheduleEnabled(const String& t, const String& p, const String& base) {
  if (t != base + "/legionella/enable/set") return false;
  bool on;
  if (parseOnOff(p, on)) {
    legScheduleEnabled = on;
    saveConfig();
    mqttPublishLegionellaConfig(true);
    mqttPublishLegionellaState(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

bool handleLegDow(const String& t, const String& p, const String& base) {
  if (t != base + "/legionella/dow/set") return false;
  int d;
  if (parseDowPayload(p, d)) {
    DynamicJsonDocument doc(64);
    doc["leg_dow"] = d;
    bool rebootRequired=false, mqttParamsChanged=false;
    apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
    mqttPublishLegionellaConfig(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

bool handleLegStartHour(const String& t, const String& p, const String& base) {
  if (t != base + "/legionella/start_hour/set") return false;
  DynamicJsonDocument doc(64);
  doc["leg_hr"] = p.toInt();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishLegionellaConfig(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleLegStartMinute(const String& t, const String& p, const String& base) {
  if (t != base + "/legionella/start_minute/set") return false;
  DynamicJsonDocument doc(64);
  doc["leg_min"] = p.toInt();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishLegionellaConfig(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleLegDurationMin(const String& t, const String& p, const String& base) {
  if (t != base + "/legionella/duration_min/set") return false;
  DynamicJsonDocument doc(64);
  doc["leg_dur"] = p.toInt();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishLegionellaConfig(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleLegUseTempSensor(const String& t, const String& p, const String& base) {
  if (t != base + "/legionella/use_temp_sensor/set") return false;
  bool on;
  if (parseOnOff(p, on)) {
    DynamicJsonDocument doc(64);
    doc["leg_ts"] = on;
    bool rebootRequired=false, mqttParamsChanged=false;
    apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
    mqttPublishLegionellaConfig(true);
    mqttPublishStatusJson(true);
  }
  return true;
}

bool handleLegTargetTempC(const String& t, const String& p, const String& base) {
  if (t != base + "/legionella/target_temp_c/set") return false;
  DynamicJsonDocument doc(64);
  doc["leg_tc"] = p.toFloat();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishLegionellaConfig(true);
  mqttPublishStatusJson(true);
  return true;
}

bool handleLegHoldMin(const String& t, const String& p, const String& base) {
  if (t != base + "/legionella/hold_min/set") return false;
  DynamicJsonDocument doc(64);
  doc["leg_hold"] = p.toInt();
  bool rebootRequired=false, mqttParamsChanged=false;
  apiApplyConfigFromJson(doc, rebootRequired, mqttParamsChanged);
  mqttPublishLegionellaConfig(true);
  mqttPublishStatusJson(true);
  return true;
}

// ===== TUNING handlers =====
bool handleTuningMaxStepPct(const String& t, const String& p, const String& base) {
  if (t != base + "/tuning/max_step_pct/set") return false;
  setTuningMaxStep(p);
  mqttPublishTuningStates(true);
  mqttPublishStatusJson(true);
  return true;
}













void begin() { mqttSetup(); }
void loop()  { mqttLoop(); }

bool ensureConnected() {
  if (!mqttEnabled) return false;
  if (mqttHost.length() == 0) return false;
  if (mqttClient.connected()) return true;

  unsigned long nowMs = millis();
  if (nowMs - lastMqttReconnectAttempt < MQTT_RECONNECT_INTERVAL_MS) return false;
  lastMqttReconnectAttempt = nowMs;

  mqttClient.setServer(mqttHost.c_str(), mqttPort);
  mqttClient.setCallback(mqttCallback);

  // Veilig: buffer nogmaals zetten vlak voor connect
  #if USE_PUBSUBCLIENT_SETBUFFERSIZE
    mqttClient.setBufferSize(MQTT_CLIENT_BUFFER_SIZE);
  #endif
  String clientId = getMqttClientId();

  // LWT: offline retained
  String willTopic = mqttAvailabilityTopic();
  bool ok = false;

  if (mqttUser.length() > 0) {
    ok = mqttClient.connect(
      clientId.c_str(),
      mqttUser.c_str(),
      mqttPassStr.c_str(),
      willTopic.c_str(), 0, true,
      "offline"
    );
  } else {
    ok = mqttClient.connect(
      clientId.c_str(),
      willTopic.c_str(), 0, true,
      "offline"
    );
  }

  if (ok) {
    String base = mqttBase();

    subscribeControlTopics(base);

    mqttPublishAvailability("online");
    publishRetainedStateBundle(true, true);
  }
  return ok;
}

} // namespace MQTT
