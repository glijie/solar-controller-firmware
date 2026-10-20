#include "MqttPublish.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <math.h>
#include <stdlib.h>

#include "HomeAssistantDiscovery.h"
#include "MultiController.h"
#include "MultiControllerTelemetry.h"
#include "RelayControl.h"
#include "ScenarioLogic.h"
#include "StatusLabels.h"
#include "app_globals.h"
#include "sensors.h"

static String mqttTuningStateTopic() { return mqttBase() + "/tuning/state"; }

static uint8_t mqttConfiguredSlotCount() {
  if (mcControllerCount < 1) return 1;
  return (mcControllerCount > MC_MAX_SLOTS) ? MC_MAX_SLOTS : mcControllerCount;
}

void mqttPublishAvailability(const char* s) {
  // Availability moet altijd direct (retained) gepubliceerd worden.
  // Deze topic wordt ook gebruikt door Home Assistant om entities "available/unavailable" te markeren.
  if (!mqttEnabled || !mqttClient.connected()) return;
  mqttClient.publish(mqttAvailabilityTopic().c_str(), s, true);
}

void mqttPublishForceState(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;
  static bool _sent = false;
  static uint8_t _last = 255;
  if (!forceNow && _sent && _last == (uint8_t)controlMode) return;
  _sent = true;
  _last = (uint8_t)controlMode;
  String topic = mqttBase() + "/force_heat/state";
  const char* payload = (controlMode == MODE_FORCE_HEAT) ? "ON" : "OFF";
  mqttClient.publish(topic.c_str(), payload, true);
}

void mqttPublishRelayState(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;
  static bool _sent = false;
  static bool _enabled = false;
  static bool _manualOn = false;

  const bool enabledNow = relayIsEnabled();
  const bool manualOnNow = enabledNow ? relayGetManualOn() : false;

  if (!forceNow && _sent && _enabled == enabledNow && _manualOn == manualOnNow) return;

  _sent = true;
  _enabled = enabledNow;
  _manualOn = manualOnNow;

  String topic = mqttBase() + "/relay/state";
  const char* payload = (enabledNow && manualOnNow) ? "ON" : "OFF";
  mqttClient.publish(topic.c_str(), payload, true);
}

void mqttPublishMultiControllerStates(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;

  static bool _sent = false;
  static bool _enabled = false;
  static bool _fallback = false;
  static uint8_t _requestedOutputs = 0;
  static uint8_t _effectiveOutputs = 0;
  static uint8_t _activeOutputs = 0;
  static float _requestedPct = NAN;
  static float _assignedPct = NAN;
  static float _surplusMaxPct = NAN;
  static bool _negativeOverride = false;
  static String _scenario = "";
  static float _slotPct[MC_MAX_SLOTS] = {NAN, NAN, NAN};
  static String _slotPhase[MC_MAX_SLOTS] = {"", "", ""};
  static uint8_t _slotPublishCount = 0;

  const bool enabledNow = mcEnabled;
  const bool fallbackNow = multiControllerIsFallbackActive();
  const uint8_t requestedOutputsNow = multiControllerGetRequestedLocalPwmCount();
  const uint8_t effectiveOutputsNow = multiControllerGetEffectiveLocalPwmCount();
  const uint8_t activeOutputsNow = multiControllerGetActiveLocalPwmCount();
  const float requestedPctNow = multiControllerGetRequestedGroupPercent();
  const float assignedPctNow = multiControllerGetAssignedGroupPercent();
  const float surplusMaxPctNow = multiControllerGetSurplusGroupMaxPercent();
  const bool negativeOverrideNow = multiControllerIsNegativePriorityOverrideActive();
  const String scenarioNow = multiControllerGetScenarioLabel();
  const uint8_t slotPublishCountNow = mqttConfiguredSlotCount();

  float slotPctNow[MC_MAX_SLOTS] = {0.0f, 0.0f, 0.0f};
  String slotPhaseNow[MC_MAX_SLOTS] = {"none", "none", "none"};
  for (uint8_t i = 0; i < slotPublishCountNow; i++) {
    const bool slotConfigured = (i < mcControllerCount);
    const bool slotAttached = slotConfigured && multiControllerIsLocalPwmSlotAttached(i);
    const bool slotActive = slotConfigured && multiControllerIsLocalPwmSlotActive(i);
    slotPctNow[i] = slotActive ? multiControllerGetLocalPwmSlotPercent(i) : 0.0f;
    if (slotConfigured && slotAttached) {
      switch (multiControllerGetSlotAssignedPhase(i)) {
        case 1: slotPhaseNow[i] = "L1"; break;
        case 2: slotPhaseNow[i] = "L2"; break;
        case 3: slotPhaseNow[i] = "L3"; break;
        default: slotPhaseNow[i] = "none"; break;
      }
    }
  }

  bool same = _sent &&
              (_enabled == enabledNow) &&
              (_fallback == fallbackNow) &&
              (_requestedOutputs == requestedOutputsNow) &&
              (_effectiveOutputs == effectiveOutputsNow) &&
              (_activeOutputs == activeOutputsNow) &&
              (fabs(_requestedPct - requestedPctNow) < 0.05f) &&
              (fabs(_assignedPct - assignedPctNow) < 0.05f) &&
              (fabs(_surplusMaxPct - surplusMaxPctNow) < 0.05f) &&
              (_negativeOverride == negativeOverrideNow) &&
              (_scenario == scenarioNow) &&
              (_slotPublishCount == slotPublishCountNow);

  for (uint8_t i = 0; same && i < slotPublishCountNow; i++) {
    if (fabs(_slotPct[i] - slotPctNow[i]) >= 0.05f) same = false;
    if (_slotPhase[i] != slotPhaseNow[i]) same = false;
  }

  if (!forceNow && same) return;

  _sent = true;
  _enabled = enabledNow;
  _fallback = fallbackNow;
  _requestedOutputs = requestedOutputsNow;
  _effectiveOutputs = effectiveOutputsNow;
  _activeOutputs = activeOutputsNow;
  _requestedPct = requestedPctNow;
  _assignedPct = assignedPctNow;
  _surplusMaxPct = surplusMaxPctNow;
  _negativeOverride = negativeOverrideNow;
  _scenario = scenarioNow;
  _slotPublishCount = slotPublishCountNow;
  for (uint8_t i = 0; i < slotPublishCountNow; i++) {
    _slotPct[i] = slotPctNow[i];
    _slotPhase[i] = slotPhaseNow[i];
  }

  const String base = mqttBase();
  mqttClient.publish((base + "/mc/enabled/state").c_str(), enabledNow ? "ON" : "OFF", true);
  mqttClient.publish((base + "/mc/fallback_active/state").c_str(), fallbackNow ? "ON" : "OFF", true);
  mqttClient.publish((base + "/mc/scenario/state").c_str(), scenarioNow.c_str(), true);
  mqttClient.publish((base + "/mc/negative_override_active/state").c_str(), negativeOverrideNow ? "ON" : "OFF", true);

  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", (unsigned)requestedOutputsNow);
    mqttClient.publish((base + "/mc/local_pwm_outputs_requested/state").c_str(), buf, true);
  }
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", (unsigned)effectiveOutputsNow);
    mqttClient.publish((base + "/mc/local_pwm_outputs_effective/state").c_str(), buf, true);
  }
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", (unsigned)activeOutputsNow);
    mqttClient.publish((base + "/mc/local_pwm_outputs_active/state").c_str(), buf, true);
  }
  {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f", requestedPctNow);
    mqttClient.publish((base + "/mc/group_pwm_requested_pct/state").c_str(), buf, true);
  }
  {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f", assignedPctNow);
    mqttClient.publish((base + "/mc/group_pwm_assigned_pct/state").c_str(), buf, true);
  }
  {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.1f", surplusMaxPctNow);
    mqttClient.publish((base + "/mc/surplus_group_max_pct/state").c_str(), buf, true);
  }

  for (uint8_t i = 0; i < slotPublishCountNow; i++) {
    char topic[96];
    char payload[24];
    snprintf(topic, sizeof(topic), "%s/mc/slot%u/duty_percent/state", base.c_str(), (unsigned)(i + 1));
    snprintf(payload, sizeof(payload), "%.1f", slotPctNow[i]);
    mqttClient.publish(topic, payload, true);

    snprintf(topic, sizeof(topic), "%s/mc/slot%u/phase/state", base.c_str(), (unsigned)(i + 1));
    mqttClient.publish(topic, slotPhaseNow[i].c_str(), true);
  }
}


void mqttPublishCoreLiveStates(bool forceNow) {
  static bool _wasConnected = false;
  if (!mqttEnabled || !mqttClient.connected()) {
    _wasConnected = false;
    return;
  }
  if (!_wasConnected) {
    forceNow = true;
    _wasConnected = true;
  }

  static bool _sent = false;
  static int _lastNet = 0;
  static int _lastErr = 0;
  static int _lastOvr = 0;
  static float _lastDuty = NAN;
  static float _lastLegTemp = NAN;
  static bool _lastLegValid = false;
  static int _lastWifi = -999;
  static unsigned long _lastUptime = 0;
  static unsigned long _lastPublishMs = 0;

  const unsigned long nowMs = millis();
  const bool legTempValidNow = tempIsValid(lastTempC);
  const int wifiNow = WiFi.RSSI();
  const unsigned long uptimeNow = nowMs / 1000UL;
  const bool heartbeatDue = (nowMs - _lastPublishMs) >= 60000UL;

  const bool changed = (!_sent) ||
                       (abs(lastNetPowerW - _lastNet) >= 1) ||
                       (abs(lastErrorW - _lastErr) >= 1) ||
                       (abs((int)lround(lastOvershootW) - _lastOvr) >= 1) ||
                       (!_lastLegValid != !legTempValidNow) ||
                       (fabsf(lastDutyPercent - _lastDuty) >= 0.1f) ||
                       (legTempValidNow && (!_lastLegValid || fabsf(lastTempC - _lastLegTemp) >= 0.1f)) ||
                       (abs(wifiNow - _lastWifi) >= 1) ||
                       (labs((long)uptimeNow - (long)_lastUptime) >= 60L);

  if (!forceNow && !changed && !heartbeatDue) return;

  _sent = true;
  _lastNet = lastNetPowerW;
  _lastErr = lastErrorW;
  _lastOvr = (int)lround(lastOvershootW);
  _lastDuty = lastDutyPercent;
  _lastLegValid = legTempValidNow;
  _lastLegTemp = legTempValidNow ? lastTempC : NAN;
  _lastWifi = wifiNow;
  _lastUptime = uptimeNow;
  _lastPublishMs = nowMs;

  const String base = mqttBase();
  char buf[32];

  snprintf(buf, sizeof(buf), "%ld", (long)lastNetPowerW);
  mqttClient.publish((base + "/live/net_power_w/state").c_str(), buf, true);

  snprintf(buf, sizeof(buf), "%.1f", lastDutyPercent);
  mqttClient.publish((base + "/live/duty_percent/state").c_str(), buf, true);

  snprintf(buf, sizeof(buf), "%ld", (long)lastErrorW);
  mqttClient.publish((base + "/live/error_w/state").c_str(), buf, true);

  snprintf(buf, sizeof(buf), "%ld", (long)lround(lastOvershootW));
  mqttClient.publish((base + "/live/overshoot_w/state").c_str(), buf, true);

  if (legTempValidNow) {
    snprintf(buf, sizeof(buf), "%.1f", lastTempC);
    mqttClient.publish((base + "/live/leg_temp_c/state").c_str(), buf, true);
  } else {
    mqttClient.publish((base + "/live/leg_temp_c/state").c_str(), "unknown", true);
  }

  snprintf(buf, sizeof(buf), "%d", wifiNow);
  mqttClient.publish((base + "/live/wifi_rssi_dbm/state").c_str(), buf, true);

  snprintf(buf, sizeof(buf), "%lu", uptimeNow);
  mqttClient.publish((base + "/live/uptime_s/state").c_str(), buf, true);
}

void mqttPublishLegionellaEnableState(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;
  static bool _sent = false;
  static bool _last = false;
  if (!forceNow && _sent && _last == legScheduleEnabled) return;
  _sent = true;
  _last = legScheduleEnabled;
  const char* payload = legScheduleEnabled ? "ON" : "OFF";
  mqttClient.publish((mqttBase()+"/legionella/enable/state").c_str(), payload, true);
}

void mqttPublishTuningStates(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;
  static bool _sent = false;
  static uint8_t _alg = 255;
  static float _sf = NAN, _dbw = NAN, _msp = NAN;
  bool same = _sent && (_alg == normalAlg) && (fabs(_sf - stepFactor) < 0.0005f) && (fabs(_dbw - deadbandW) < 0.05f) && (fabs(_msp - maxStepPctPerUpdatePerUpdate) < 0.05f);
  if (!forceNow && same) return;
  _sent = true;
  _alg = normalAlg; _sf = stepFactor; _dbw = deadbandW; _msp = maxStepPctPerUpdatePerUpdate;

  // Individual state topics
  mqttClient.publish((mqttBase()+"/tuning/algorithm/state").c_str(), normalAlgLabel().c_str(), true);

  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", stepFactor);
    mqttClient.publish((mqttBase()+"/tuning/step_factor/state").c_str(), buf, true);
  }
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", deadbandW);
    mqttClient.publish((mqttBase()+"/tuning/deadband_w/state").c_str(), buf, true);
  }
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", maxStepPctPerUpdatePerUpdate);
    mqttClient.publish((mqttBase()+"/tuning/max_step_pct/state").c_str(), buf, true);
  }

  // Combined JSON retained
  StaticJsonDocument<256> doc;
  doc["algorithm"] = normalAlgLabel();
  doc["step_factor"] = stepFactor;
  doc["deadband_w"]  = deadbandW;
  doc["max_step_pct"] = maxStepPctPerUpdatePerUpdate;

  String out;
  out.reserve(900);
  serializeJson(doc, out);
  mqttClient.publish(mqttTuningStateTopic().c_str(), out.c_str(), true);
}


void mqttPublishLimitsStates(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;

  static bool _sent=false;
  static float _maxOut=NAN, _legOut=NAN;

  bool same = _sent && (fabs(_maxOut - maxOutputPercentLimit) < 0.05f) && (fabs(_legOut - legOutputLimitPercent) < 0.05f);
  if (!forceNow && same) return;
  _sent = true;
  _maxOut = maxOutputPercentLimit;
  _legOut = legOutputLimitPercent;

  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", maxOutputPercentLimit);
    mqttClient.publish((mqttBase()+"/limits/max_output_pct/state").c_str(), buf, true);
  }
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", legOutputLimitPercent);
    mqttClient.publish((mqttBase()+"/limits/legionella_output_pct/state").c_str(), buf, true);
  }
}

void mqttPublishNormalConfigStates(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;

  static bool _sent=false;
  static float _target=NAN, _maxB=NAN, _minStep=NAN;
  static unsigned long _minIntMs=0;

  bool same = _sent &&
              (fabs(_target - targetExportW) < 0.05f) &&
              (fabs(_maxB - maxBoilerPowerW) < 0.05f) &&
              (fabs(_minStep - minStepPercent) < 0.05f) &&
              (_minIntMs == minUpdateIntervalMs);

  if (!forceNow && same) return;
  _sent = true;
  _target = targetExportW;
  _maxB = maxBoilerPowerW;
  _minStep = minStepPercent;
  _minIntMs = minUpdateIntervalMs;

  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", targetExportW);
    mqttClient.publish((mqttBase()+"/normal/target_export_w/state").c_str(), buf, true);
  }
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", maxBoilerPowerW);
    mqttClient.publish((mqttBase()+"/normal/max_boiler_power_w/state").c_str(), buf, true);
  }
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", minStepPercent);
    mqttClient.publish((mqttBase()+"/normal/min_step_percent/state").c_str(), buf, true);
  }
  {
    // publish seconds (UI / HA friendly)
    float sec = (float)minUpdateIntervalMs / 1000.0f;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f", sec);
    mqttClient.publish((mqttBase()+"/normal/min_interval_s/state").c_str(), buf, true);
  }
}

void mqttPublishSunStrategyStates(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;

  static bool _sent=false;
  static uint8_t _mode=255;
  static int _ws=-1, _we=-1, _n=-1, _marg=-1;
  static float _lat=NAN, _lon=NAN;
  static bool _lbl=false;

  bool same = _sent &&
              (_mode == sunSchedMode) &&
              (_ws == sunWinStartMin) &&
              (_we == sunWinEndMin) &&
              (_n  == sunCheapNBlocks) &&
              (_marg == sunDaylightMarginMin) &&
              (fabs(_lat - sunLat) < 0.0001f) &&
              (fabs(_lon - sunLon) < 0.0001f) &&
              (_lbl == dashShowSunWindowLabel);

  if (!forceNow && same) return;

  _sent = true;
  _mode = sunSchedMode;
  _ws   = sunWinStartMin;
  _we   = sunWinEndMin;
  _n    = sunCheapNBlocks;
  _marg = sunDaylightMarginMin;
  _lat  = sunLat;
  _lon  = sunLon;
  _lbl  = dashShowSunWindowLabel;

  // Publish retained state topics (mirrors Settings -> Energy strategy)
  {
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", (unsigned)_mode);
    mqttClient.publish((mqttBase()+"/sun/sched_mode/state").c_str(), buf, true);
  }
  {
    char buf[8];
    int h = (_ws/60)%24, m = _ws%60;
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    mqttClient.publish((mqttBase()+"/sun/manual_start/state").c_str(), buf, true);
    // also publish raw minutes (handy for automations)
    char buf2[16];
    snprintf(buf2, sizeof(buf2), "%d", _ws);
    mqttClient.publish((mqttBase()+"/sun/manual_start_min/state").c_str(), buf2, true);
  }
  {
    char buf[8];
    int h = (_we/60)%24, m = _we%60;
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    mqttClient.publish((mqttBase()+"/sun/manual_end/state").c_str(), buf, true);
    char buf2[16];
    snprintf(buf2, sizeof(buf2), "%d", _we);
    mqttClient.publish((mqttBase()+"/sun/manual_end_min/state").c_str(), buf2, true);
  }
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", _n);
    mqttClient.publish((mqttBase()+"/sun/cheapest_blocks/state").c_str(), buf, true);
  }
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", _marg);
    mqttClient.publish((mqttBase()+"/sun/daylight_margin_min/state").c_str(), buf, true);
  }
  {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.4f", _lat);
    mqttClient.publish((mqttBase()+"/sun/lat/state").c_str(), buf, true);
  }
  {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.4f", _lon);
    mqttClient.publish((mqttBase()+"/sun/lon/state").c_str(), buf, true);
  }
  {
    const char* payload = _lbl ? "ON" : "OFF";
    mqttClient.publish((mqttBase()+"/sun/dashboard_label/state").c_str(), payload, true);
  }
}


// ===== ELEKTRA PRIJSBRON (MQTT retained states for HA + remote control) =====
void mqttPublishElecPriceStates(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;

  static bool _sent=false;
  static uint8_t _src=255;
  static float _fix=NAN;

  bool same = _sent && (_src == elecPriceSource) && (fabs(_fix - elecFixedCtKwhAllIn) < 0.0001f);
  if (!forceNow && same) return;

  _sent = true;
  _src  = elecPriceSource;
  _fix  = elecFixedCtKwhAllIn;

  const String base = mqttBase();

  // Source (0=dynamisch, 1=vast)
  {
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", (unsigned)_src);
    mqttClient.publish((base + "/elec/src/state").c_str(), buf, true);
  }

  // Fixed all-in price (ct/kWh)
  {
    char buf[32];
    if (isnan(_fix)) {
      mqttClient.publish((base + "/elec/fixed_ct/state").c_str(), "", true);
    } else {
      dtostrf(_fix, 0, 3, buf);
      mqttClient.publish((base + "/elec/fixed_ct/state").c_str(), buf, true);
    }
  }
}


// ===== GAS VS ELEKTRA + SLIMME ELEC-BLOKKEN (MQTT retained states voor HA/remote) =====
// Functienaam blijft behouden voor compatibiliteit met bestaande aanroepen uit step315.
void mqttPublishHeatCheapConfigStates(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;

  static bool _sent = false;
  static bool _autoEnabled = false;
  static bool _execElec = false;
  static bool _cheapEnabled = false;
  static bool _useBoilerTarget = false;
  static int _maxBlocks = -1;

  const bool same = _sent &&
                    (_autoEnabled == heatAutoEnabled) &&
                    (_execElec == heatExecElecWhenCheaper) &&
                    (_cheapEnabled == heatCheapBlocksEnabled) &&
                    (_useBoilerTarget == heatCheapUseBoilerTarget) &&
                    (_maxBlocks == heatCheapNBlocks);
  if (!forceNow && same) return;

  _sent = true;
  _autoEnabled = heatAutoEnabled;
  _execElec = heatExecElecWhenCheaper;
  _cheapEnabled = heatCheapBlocksEnabled;
  _useBoilerTarget = heatCheapUseBoilerTarget;
  _maxBlocks = heatCheapNBlocks;

  const String base = mqttBase();
  mqttClient.publish((base + "/heat/auto/state").c_str(), _autoEnabled ? "ON" : "OFF", true);
  mqttClient.publish((base + "/heat/exec_el/state").c_str(), _execElec ? "ON" : "OFF", true);
  mqttClient.publish((base + "/heat/cheap_blocks/enabled/state").c_str(), _cheapEnabled ? "ON" : "OFF", true);
  mqttClient.publish((base + "/heat/cheap_blocks/use_boiler_target/state").c_str(), _useBoilerTarget ? "ON" : "OFF", true);

  char buf[16];
  snprintf(buf, sizeof(buf), "%d", _maxBlocks);
  mqttClient.publish((base + "/heat/cheap_blocks/max/state").c_str(), buf, true);
}


// ===== NORDPOOL SETTINGS (MQTT retained states for HA + remote control) =====
void mqttPublishNordpoolStates(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;

  static bool _sent=false;
  static bool _npEn=false, _incl=false, _boostEn=false;
  static String _area="";
  static int _res=-1, _nBlocks=-1;
  static float _tax=NAN, _ink=NAN, _btw=NAN, _thr=NAN;
  static uint8_t _src=255;

  bool same = _sent &&
              (_npEn == npEnabled) &&
              (_area == npArea) &&
              (_res == npResMin) &&
              (_incl == npInclTaxEnabled) &&
              (fabs(_tax - npEnergyTaxCt) < 0.0001f) &&
              (fabs(_ink - npInkoopCt) < 0.0001f) &&
              (fabs(_btw - npBtwPct) < 0.0001f) &&
              (_boostEn == npBoostEnabled) &&
              (_src == npBoostPriceSource) &&
              (fabs(_thr - npBoostThresholdCt) < 0.0001f) &&
              (_nBlocks == npBoostNBlocks);

  if (!forceNow && same) return;

  _sent   = true;
  _npEn   = npEnabled;
  _area   = npArea;
  _res    = npResMin;
  _incl   = npInclTaxEnabled;
  _tax    = npEnergyTaxCt;
  _ink    = npInkoopCt;
  _btw    = npBtwPct;

  _boostEn = npBoostEnabled;
  _src     = npBoostPriceSource;
  _thr     = npBoostThresholdCt;
  _nBlocks = npBoostNBlocks;

  const String base = mqttBase();

  // helpers
  auto pubOnOff = [&](const String& topic, bool v){
    mqttClient.publish(topic.c_str(), v ? "ON" : "OFF", true);
  };
  auto pubInt = [&](const String& topic, int v){
    char buf[16]; snprintf(buf, sizeof(buf), "%d", v);
    mqttClient.publish(topic.c_str(), buf, true);
  };
  auto pubFloat = [&](const String& topic, float v, int decimals){
    char buf[32];
    if (isnan(v)) {
      mqttClient.publish(topic.c_str(), "", true);
      return;
    }
    // dtostrf(width, prec) -> keep width minimal
    dtostrf(v, 0, decimals, buf);
    mqttClient.publish(topic.c_str(), buf, true);
  };

  // Nordpool basics
  pubOnOff(base + "/np/enabled/state", _npEn);
  mqttClient.publish((base + "/np/area/state").c_str(), _area.c_str(), true);
  pubInt(base + "/np/res_min/state", _res);

  // Taxes
  pubOnOff(base + "/np/incl_taxes/state", _incl);
  pubFloat(base + "/np/energy_tax_ct/state", _tax, 4);
  pubFloat(base + "/np/inkoop_ct/state", _ink, 4);
  pubFloat(base + "/np/btw_pct/state", _btw, 1);

  // Price boost
  pubOnOff(base + "/np/boost_enabled/state", _boostEn);
  mqttClient.publish((base + "/np/boost_price_source/state").c_str(), (_src==1) ? "total" : "spot", true);
  pubFloat(base + "/np/boost_threshold_ct/state", _thr, 4);
  pubInt(base + "/np/boost_n_blocks/state", _nBlocks);
}



void mqttPublishLegionellaPlanStates(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;

  static bool _sent=false;
  static bool _en=false, _ts=false;
  static int _dow=-1, _hr=-1, _min=-1, _dur=-1, _hold=-1;
  static float _tc=NAN;

  bool same = _sent &&
              (_en == legScheduleEnabled) &&
              (_ts == legUseTempSensor) &&
              (_dow == legDow) &&
              (_hr == legStartHour) &&
              (_min == legStartMinute) &&
              (_dur == legDurationMin) &&
              (fabs(_tc - legTargetTempC) < 0.05f) &&
              (_hold == legHoldMin);

  if (!forceNow && same) return;
  _sent = true;
  _en = legScheduleEnabled;
  _ts = legUseTempSensor;
  _dow = legDow;
  _hr = legStartHour;
  _min = legStartMinute;
  _dur = legDurationMin;
  _tc = legTargetTempC;
  _hold = legHoldMin;

  // Keep existing enable state topic (used by HA switch)
  mqttPublishLegionellaEnableState(true);

  // DOW as Dutch string for HA select
  static const char* DOW_OPTS[7] = {"Zondag","Maandag","Dinsdag","Woensdag","Donderdag","Vrijdag","Zaterdag"};
  const char* dowStr = (legDow >= 0 && legDow <= 6) ? DOW_OPTS[legDow] : "Zondag";
  mqttClient.publish((mqttBase()+"/legionella/dow/state").c_str(), dowStr, true);

  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", legStartHour);
    mqttClient.publish((mqttBase()+"/legionella/start_hour/state").c_str(), buf, true);
  }
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", legStartMinute);
    mqttClient.publish((mqttBase()+"/legionella/start_minute/state").c_str(), buf, true);
  }
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", legDurationMin);
    mqttClient.publish((mqttBase()+"/legionella/duration_min/state").c_str(), buf, true);
  }

  // Temp sensor switch
  mqttClient.publish((mqttBase()+"/legionella/use_temp_sensor/state").c_str(), legUseTempSensor ? "ON" : "OFF", true);

  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", legTargetTempC);
    mqttClient.publish((mqttBase()+"/legionella/target_temp_c/state").c_str(), buf, true);
  }
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", legHoldMin);
    mqttClient.publish((mqttBase()+"/legionella/hold_min/state").c_str(), buf, true);
  }
}

void mqttPublishLegionellaState(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;
  static bool _sent=false;
  static bool _run=false;
  static uint32_t _hb=0;
  static uint32_t _lastRun=0;
  static uint32_t _lastSuccess=0;
  static time_t _nextRun=0;
  static String _reason="";
  time_t nextRun = computeNextLegionellaEpoch();
  uint32_t hb = timeIsValid() ? (uint32_t)(time(nullptr) / 3600) : 0;
  bool same = _sent && (_run==legRunning) && (_lastRun==legLastRunEpoch) && (_lastSuccess==legLastSuccessEpoch) && (_nextRun==nextRun) && (_reason==legReason) && (_hb==hb);
  if (!forceNow && same) return;
  _sent=true; _run=legRunning; _lastRun=legLastRunEpoch; _lastSuccess=legLastSuccessEpoch; _nextRun=nextRun; _reason=legReason; _hb=hb;

  mqttClient.publish((mqttBase()+"/legionella/state").c_str(), legStateLabel().c_str(), true);

  String nextTxt = (nextRun > 0) ? fmtTimeLocal(nextRun) : String("");
  String lastTxt = (legLastRunEpoch > 0) ? fmtTimeLocal((time_t)legLastRunEpoch) : String("");

  mqttClient.publish((mqttBase()+"/legionella/next_run").c_str(), nextTxt.c_str(), true);
  mqttClient.publish((mqttBase()+"/legionella/last_run").c_str(), lastTxt.c_str(), true);
  // ---- Legionella compliance status (Step 3: MQTT read-only) ----
  // SUCCESS = run finished with reason "DONE" (target reached + hold completed)
  // Reporting only; no behavior changes in Step 3.
  {
    String leg_status = "DISABLED";
    bool leg_success_in_last_7d = false;
    uint32_t leg_hours_since_success = 0;
    uint32_t leg_hours_until_due = 0;
    time_t nowTs = 0;
    time_t dueTs = 0;

    if (!timeIsValid()) {
      leg_status = "UNKNOWN";
    } else {
      nowTs = time(nullptr);

      if (legRunning) {
        leg_status = "RUNNING";
      } else if (legUseTempSensor && !ds18Available) {
        leg_status = "NO_SENSOR";
      } else if (!legUseTempSensor) {
        // Without temp sensor we can't prove success; treat as DUE until a real success is recorded.
        leg_status = (legLastSuccessEpoch > 0) ? "OK" : "DUE";
      } else if (legLastSuccessEpoch > 0 && nowTs > 0) {
        // due = success + 168h
        dueTs = (time_t)legLastSuccessEpoch + (time_t)168 * 3600;
        if ((time_t)legLastSuccessEpoch <= nowTs) {
          leg_hours_since_success = (uint32_t)((nowTs - (time_t)legLastSuccessEpoch) / 3600);
          if (nowTs < dueTs) {
            leg_hours_until_due = (uint32_t)((dueTs - nowTs + 3599) / 3600);
            leg_success_in_last_7d = true;
            leg_status = "OK";
          } else {
            leg_success_in_last_7d = false;
            leg_status = "DUE";
            leg_hours_until_due = 0;
          }
        } else {
          leg_status = "DUE"; // clock moved backwards
        }
      } else {
        leg_status = "DUE"; // never succeeded yet
      }
    }

    // Publish per-topic (retained)
    mqttClient.publish((mqttBase()+"/legionella/status").c_str(), leg_status.c_str(), true);

    {
      char buf[16];
      snprintf(buf, sizeof(buf), "%lu", (unsigned long)legLastSuccessEpoch);
      mqttClient.publish((mqttBase()+"/legionella/last_success_ts").c_str(), buf, true);
    }
    String lsIso = (timeIsValid() && legLastSuccessEpoch > 0) ? fmtTimeLocal((time_t)legLastSuccessEpoch) : String("");
    mqttClient.publish((mqttBase()+"/legionella/last_success_iso").c_str(), lsIso.c_str(), true);

    {
      char buf[16];
      snprintf(buf, sizeof(buf), "%lu", (unsigned long)dueTs);
      mqttClient.publish((mqttBase()+"/legionella/due_ts").c_str(), buf, true);
    }
    String dueIso = (timeIsValid() && dueTs > 0) ? fmtTimeLocal(dueTs) : String("");
    mqttClient.publish((mqttBase()+"/legionella/due_iso").c_str(), dueIso.c_str(), true);

    {
      char buf[16];
      snprintf(buf, sizeof(buf), "%lu", (unsigned long)leg_hours_since_success);
      mqttClient.publish((mqttBase()+"/legionella/hours_since_success").c_str(), buf, true);
    }
    {
      char buf[16];
      snprintf(buf, sizeof(buf), "%lu", (unsigned long)leg_hours_until_due);
      mqttClient.publish((mqttBase()+"/legionella/hours_until_due").c_str(), buf, true);
    }
    mqttClient.publish((mqttBase()+"/legionella/success_in_last_7d").c_str(), leg_success_in_last_7d ? "true" : "false", true);
  }

}

void mqttPublishLegionellaConfig(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;
  static bool _sent=false;
  static bool _se=false,_uts=false;
  static int _dow=-1,_hr=-1,_min=-1,_dur=-1,_hold=-1;
  static float _pct=NAN,_tc=NAN;
  bool same = _sent && (_se==legScheduleEnabled) && (_dow==legDow) && (_hr==legStartHour) && (_min==legStartMinute) && (_dur==legDurationMin)
              && (fabs(_pct-legOutputLimitPercent)<0.05f) && (_uts==legUseTempSensor) && (fabs(_tc-legTargetTempC)<0.05f) && (_hold==legHoldMin);
  if (!forceNow && same) return;
  _sent=true;
  _se=legScheduleEnabled; _dow=legDow; _hr=legStartHour; _min=legStartMinute; _dur=legDurationMin;
  _pct=legOutputLimitPercent; _uts=legUseTempSensor; _tc=legTargetTempC; _hold=legHoldMin;

  StaticJsonDocument<640> doc;
  doc["schedule_enabled"]     = legScheduleEnabled;
  doc["dow"]                  = legDow;
  doc["hour"]                 = legStartHour;
  doc["minute"]               = legStartMinute;
  doc["duration_min"]         = legDurationMin;
  doc["output_limit_percent"] = legOutputLimitPercent;

  doc["use_temp_sensor"]      = legUseTempSensor;
  doc["target_temp_c"]        = legTargetTempC;
  doc["hold_min"]             = legHoldMin;

  String out;
  out.reserve(900);
  serializeJson(doc, out);
  mqttClient.publish((mqttBase()+"/legionella/config/state").c_str(), out.c_str(), true);

  mqttPublishLegionellaEnableState(forceNow);
}


void mqttPublishStatusJson(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;

  // Status JSON is bedoeld als "live" telemetrie maar hoeft niet elke seconde.
  // We publiceren standaard elke N seconden (instelbaar in Settings).
  //
  // EXTRA (v3.11.38): om WiFi/CPU/heap verder te ontlasten slaan we een publish over
  // als de belangrijkste waarden niet "betekenisvol" veranderd zijn (delta-filter),
  // én er ook geen relevante state-change is. Dit heeft GEEN invloed op de echte regeling.
  unsigned long nowMs = millis();
  unsigned long intervalMs = (unsigned long)statusPublishIntervalSec * 1000UL;
  if (!forceNow) {
    if (intervalMs < 1000UL) intervalMs = 1000UL;
    if (nowMs - lastStatusPublishMs < intervalMs) return;

    // Delta-filter (alleen telemetrie naar HA/MQTT)
    const int DELTA_W   = 25; // W: net/error/overshoot minimale wijziging
    const int DELTA_PWM = 1;  // %: duty minimale wijziging
    const unsigned long HEARTBEAT_MS = 60000UL; // Forceer minimaal 1 status publish per minuut (keep-alive)

    static int  _lastPubNet = 0;
    static int  _lastPubErr = 0;
    static int  _lastPubOvr = 0;
    static int  _lastPubPwm = 0;
    static bool _init = false;

    // Relevante state flags (als deze veranderen willen we wél publishen)
    static uint8_t _lastPubMode = 255;
    static uint8_t _lastPubLeg  = 255;
    static bool    _lastPubMeterOk = false;
    static bool    _lastPubCfg = false;
    static bool    _lastPubRelayEnabled = false;
    static bool    _lastPubRelayManualOn = false;
    static bool    _lastPubMcEnabled = false;
    static bool    _lastPubMcFallback = false;
    static bool    _lastPubMcNegativeOverride = false;
    static bool    _lastPubMcInternalCoordinator = false;
    static bool    _lastPubMcRemoteTargetActive = false;
    static uint8_t _lastPubMcRequestedOutputs = 0;
    static uint8_t _lastPubMcEffectiveOutputs = 0;
    static uint8_t _lastPubMcActiveOutputs = 0;
    static uint8_t _lastPubMcPeersConfigured = 0;
    static uint8_t _lastPubMcPeersOnline = 0;
    static uint8_t _lastPubMcPeersHealthy = 0;
    static uint8_t _lastPubMcPeersWritebackOk = 0;
    static int     _lastPubMcRequestedPct = 0;
    static int     _lastPubMcAssignedPct = 0;
    static String  _lastPubMcScenario = "";

    if (!_init) {
      _init = true;
      _lastPubNet = lastNetPowerW;
      _lastPubErr = lastErrorW;
      _lastPubOvr = (int)lastOvershootW;
      _lastPubPwm = (int)lastDutyPercent;
      _lastPubMode = (uint8_t)controlMode;
      _lastPubLeg  = (uint8_t)(legRunning ? 1 : 0);
      _lastPubMeterOk = lastMeterOk;
      _lastPubCfg = configMode;
      _lastPubRelayEnabled = relayIsEnabled();
      _lastPubRelayManualOn = relayIsEnabled() ? relayGetManualOn() : false;
      _lastPubMcEnabled = mcEnabled;
      _lastPubMcFallback = multiControllerIsFallbackActive();
      _lastPubMcNegativeOverride = multiControllerIsNegativePriorityOverrideActive();
      _lastPubMcInternalCoordinator = multiControllerIsInternalCoordinator();
      _lastPubMcRemoteTargetActive = multiControllerIsRemoteTargetControlActive();
      _lastPubMcRequestedOutputs = multiControllerGetRequestedLocalPwmCount();
      _lastPubMcEffectiveOutputs = multiControllerGetEffectiveLocalPwmCount();
      _lastPubMcActiveOutputs = multiControllerGetActiveLocalPwmCount();
      _lastPubMcPeersConfigured = multiControllerGetConfiguredPeerCount();
      _lastPubMcPeersOnline = multiControllerGetOnlinePeerCount();
      _lastPubMcPeersHealthy = multiControllerGetHealthyPeerCount();
      _lastPubMcPeersWritebackOk = multiControllerGetWritebackHealthyPeerCount();
      _lastPubMcRequestedPct = (int)roundf(multiControllerGetRequestedGroupPercent() * 10.0f);
      _lastPubMcAssignedPct = (int)roundf(multiControllerGetAssignedGroupPercent() * 10.0f);
      _lastPubMcScenario = multiControllerGetScenarioLabel();
    }

    const bool relayEnabledNow = relayIsEnabled();
    const bool relayManualOnNow = relayEnabledNow ? relayGetManualOn() : false;
    const bool mcEnabledNow = mcEnabled;
    const bool mcFallbackNow = multiControllerIsFallbackActive();
    const bool mcNegativeOverrideNow = multiControllerIsNegativePriorityOverrideActive();
    const bool mcInternalCoordinatorNow = multiControllerIsInternalCoordinator();
    const bool mcRemoteTargetActiveNow = multiControllerIsRemoteTargetControlActive();
    const uint8_t mcRequestedOutputsNow = multiControllerGetRequestedLocalPwmCount();
    const uint8_t mcEffectiveOutputsNow = multiControllerGetEffectiveLocalPwmCount();
    const uint8_t mcActiveOutputsNow = multiControllerGetActiveLocalPwmCount();
    const uint8_t mcPeersConfiguredNow = multiControllerGetConfiguredPeerCount();
    const uint8_t mcPeersOnlineNow = multiControllerGetOnlinePeerCount();
    const uint8_t mcPeersHealthyNow = multiControllerGetHealthyPeerCount();
    const uint8_t mcPeersWritebackOkNow = multiControllerGetWritebackHealthyPeerCount();
    const int mcRequestedPctNow = (int)roundf(multiControllerGetRequestedGroupPercent() * 10.0f);
    const int mcAssignedPctNow = (int)roundf(multiControllerGetAssignedGroupPercent() * 10.0f);
    const String mcScenarioNow = multiControllerGetScenarioLabel();

    bool stateChanged =
      (_lastPubMode != (uint8_t)controlMode) ||
      (_lastPubLeg  != (uint8_t)(legRunning ? 1 : 0))    ||
      (_lastPubMeterOk != lastMeterOk)       ||
      (_lastPubCfg != configMode)            ||
      (_lastPubRelayEnabled != relayEnabledNow) ||
      (_lastPubRelayManualOn != relayManualOnNow) ||
      (_lastPubMcEnabled != mcEnabledNow) ||
      (_lastPubMcFallback != mcFallbackNow) ||
      (_lastPubMcNegativeOverride != mcNegativeOverrideNow) ||
      (_lastPubMcInternalCoordinator != mcInternalCoordinatorNow) ||
      (_lastPubMcRemoteTargetActive != mcRemoteTargetActiveNow) ||
      (_lastPubMcRequestedOutputs != mcRequestedOutputsNow) ||
      (_lastPubMcEffectiveOutputs != mcEffectiveOutputsNow) ||
      (_lastPubMcActiveOutputs != mcActiveOutputsNow) ||
      (_lastPubMcPeersConfigured != mcPeersConfiguredNow) ||
      (_lastPubMcPeersOnline != mcPeersOnlineNow) ||
      (_lastPubMcPeersHealthy != mcPeersHealthyNow) ||
      (_lastPubMcPeersWritebackOk != mcPeersWritebackOkNow) ||
      (_lastPubMcRequestedPct != mcRequestedPctNow) ||
      (_lastPubMcAssignedPct != mcAssignedPctNow) ||
      (_lastPubMcScenario != mcScenarioNow);

    bool valueChanged =
      (abs(lastNetPowerW - _lastPubNet) >= DELTA_W) ||
      (abs(lastErrorW    - _lastPubErr) >= DELTA_W) ||
      (abs((int)lastOvershootW - _lastPubOvr) >= DELTA_W) ||
      (abs((int)lastDutyPercent - _lastPubPwm) >= DELTA_PWM);

        bool heartbeatDue = (lastStatusActualPublishMs == 0) || (nowMs - lastStatusActualPublishMs >= HEARTBEAT_MS);

    if (!stateChanged && !valueChanged && !heartbeatDue) {
      // Interval was verstreken, maar er is niets relevants veranderd én heartbeat is niet nodig → publish overslaan.
      lastStatusPublishMs = nowMs;
      return;
    }

    // Update last-published snapshot
    _lastPubNet = lastNetPowerW;
    _lastPubErr = lastErrorW;
    _lastPubOvr = (int)lastOvershootW;
    _lastPubPwm = (int)lastDutyPercent;
    _lastPubMode = (uint8_t)controlMode;
    _lastPubLeg  = (uint8_t)(legRunning ? 1 : 0);
    _lastPubMeterOk = lastMeterOk;
    _lastPubCfg = configMode;
    _lastPubRelayEnabled = relayEnabledNow;
    _lastPubRelayManualOn = relayManualOnNow;
    _lastPubMcEnabled = mcEnabledNow;
    _lastPubMcFallback = mcFallbackNow;
    _lastPubMcNegativeOverride = mcNegativeOverrideNow;
    _lastPubMcInternalCoordinator = mcInternalCoordinatorNow;
    _lastPubMcRemoteTargetActive = mcRemoteTargetActiveNow;
    _lastPubMcRequestedOutputs = mcRequestedOutputsNow;
    _lastPubMcEffectiveOutputs = mcEffectiveOutputsNow;
    _lastPubMcActiveOutputs = mcActiveOutputsNow;
    _lastPubMcPeersConfigured = mcPeersConfiguredNow;
    _lastPubMcPeersOnline = mcPeersOnlineNow;
    _lastPubMcPeersHealthy = mcPeersHealthyNow;
    _lastPubMcPeersWritebackOk = mcPeersWritebackOkNow;
    _lastPubMcRequestedPct = mcRequestedPctNow;
    _lastPubMcAssignedPct = mcAssignedPctNow;
    _lastPubMcScenario = mcScenarioNow;
  }

  lastStatusPublishMs = nowMs;

  StaticJsonDocument<4200> doc;
  doc["fw"]                 = FW_VERSION;
  doc["mode"]               = configMode ? "CONFIG" : "RUN";

  // MQTT/HA meta (handig voor debug)
  doc["mqtt_host"]          = mqttHost;
  doc["mqtt_port"]          = mqttPort;
  doc["mqtt_base"]          = mqttBase();
  doc["ha_disc"]            = haDiscoveryEnabled;
  doc["ha_node_id"]         = haNodeId();
  doc["control_mode"]       = modeLabel();
  doc["force_heat"]         = (controlMode == MODE_FORCE_HEAT);
  doc["heat_exec_active"]   = heatExecElecActiveNow();

  doc["relay_enabled"]      = relayIsEnabled();
  doc["relay_gpio"]         = relayGetPin();
  doc["relay_type"]         = relayIsActiveLow() ? "active_low" : "active_high";
  doc["relay_boot_on"]      = relayGetBootOn();
  doc["relay_manual_on"]    = relayGetManualOn();

  appendMultiControllerMqttStatus(doc);

// Nordpool prijs-boost status
doc["price_boost_enabled"]     = npBoostEnabled;
doc["price_boost_active"]      = npBoostActiveNow;
doc["price_boost_price_source"]= (npBoostPriceSource == 1) ? "total" : "spot";
doc["price_boost_threshold_ct"]= npBoostThresholdCt;
doc["price_boost_n_blocks"]    = npBoostNBlocks;
doc["price_boost_selected_n"]  = npBoostSelectedN;
doc["price_boost_eligible_n"]  = npBoostEligibleN;
doc["price_boost_negative_n"]  = npBoostNegativeN;


  doc["normal_algorithm"]   = normalAlgLabel();

  // Energy Strategy (sun scheduling) - expose to MQTT status (read-only snapshot)
  doc["sun_sched_mode"]        = sunSchedMode;  // 0=ALWAYS,1=MANUAL,2=CHEAPEST_DAYLIGHT,3=OFF
  doc["sun_sched_label"]       = (sunSchedMode==0) ? "ALWAYS" : (sunSchedMode==1) ? "HANDMATIG" : (sunSchedMode==2) ? "CHEAPEST DAYLIGHT" : "UIT";
  doc["sun_manual_start_min"]  = sunWinStartMin;
  doc["sun_manual_end_min"]    = sunWinEndMin;
  doc["sun_cheapest_blocks"]   = sunCheapNBlocks;
  doc["sun_daylight_margin_min"]= sunDaylightMarginMin;
  doc["sun_lat"]               = sunLat;
  doc["sun_lon"]               = sunLon;
  doc["dash_sun_label"]        = dashShowSunWindowLabel;

  // Precomputed windows (today) - useful for dashboards
  doc["sun_daylight_start_min"]= sunDaylightStartMin;
  doc["sun_daylight_end_min"]  = sunDaylightEndMin;
  doc["sun_cheapest_start_min"]= sunCheapestStartMin;
  doc["sun_cheapest_end_min"]  = sunCheapestEndMin;
  doc["sun_window_date"]       = sunWinDate;

  doc["error_w"]            = lastErrorW;
  doc["overshoot_w"]        = lastOvershootW;

  doc["meter_type"]         = meterType;
  doc["meter_label"]        = meterLabel();
  doc["meter_ip"]           = meterIp;
  doc["meter_ok"]           = lastMeterOk;

  doc["net_power_w"]        = lastNetPowerW;
  doc["duty_percent"]       = lastDutyPercent;

  doc["max_output_percent"] = maxOutputPercentLimit;
  doc["time_valid"]         = timeIsValid();

  doc["legionella_running"] = legRunning;
  doc["legionella_state"]   = legStateLabel();
  doc["legionella_reason"]  = legReason;
  doc["leg_schedule_enabled"] = legScheduleEnabled;
  doc["leg_dow"] = legDow;
  doc["leg_duration_min"] = legDurationMin;
  doc["leg_target_temp_c"] = legTargetTempC;
  doc["leg_hold_min"] = legHoldMin;
  doc["leg_start_hour"] = legStartHour;
  doc["leg_start_minute"] = legStartMinute;
  doc["leg_output_limit_pct"] = legOutputLimitPercent;
  doc["mc_slave_legionella_override_enabled"] = multiControllerSlaveLegionellaOverrideEnabled();
  doc["mc_slave_legionella_override_active"]  = multiControllerSlaveLegionellaOverrideActive();

  doc["leg_use_temp_sensor"]= legUseTempSensor;
  doc["leg_sensor_found"]   = ds18Available;
  if (tempIsValid(lastTempC)) doc["leg_temp_c"] = lastTempC;

  // ---- Legionella compliance status (Step 2: REST read-only) ----
  // SUCCESS = run finished with reason "DONE" (target reached + hold completed)
  // We only *report* status here; no behavior changes in Step 2.
  {
    String leg_status = "DISABLED";
    bool leg_success_in_last_7d = false;
    uint32_t leg_hours_since_success = 0;
    uint32_t leg_hours_until_due = 0;
    time_t nowTs = 0;

    if (!timeIsValid()) {
      leg_status = "UNKNOWN"; // NTP/time not valid
    } else {
      nowTs = time(nullptr);
      if (legRunning) {
        leg_status = "RUNNING";
      } else if (!legUseTempSensor) {
        leg_status = "DISABLED";
      } else if (!ds18Available || !tempIsValid(lastTempC)) {
        leg_status = "NO_SENSOR";
      } else {
        if (legLastSuccessEpoch > 0 && nowTs >= (time_t)legLastSuccessEpoch) {
          uint32_t diffS = (uint32_t)(nowTs - (time_t)legLastSuccessEpoch);
          leg_hours_since_success = diffS / 3600UL;
          if (leg_hours_since_success < 168UL) {
            leg_success_in_last_7d = true;
            leg_status = "OK";
            leg_hours_until_due = 168UL - leg_hours_since_success;
          } else {
            leg_status = "DUE";
            leg_hours_until_due = 0;
          }
        } else {
          leg_status = "DUE"; // never succeeded yet (or clock moved backwards)
        }
      }
    }

    doc["leg_status"] = leg_status;
    doc["leg_last_success_ts"] = (uint32_t)legLastSuccessEpoch;
    if (legLastSuccessEpoch > 0) doc["leg_last_success_iso"] = fmtTimeLocal((time_t)legLastSuccessEpoch);

    // due = success + 168h, only meaningful when time is valid and success exists
    time_t dueTs = 0;
    if (timeIsValid() && legLastSuccessEpoch > 0) dueTs = (time_t)legLastSuccessEpoch + (time_t)(168UL * 3600UL);
    doc["leg_due_ts"] = (uint32_t)dueTs;
    if (dueTs > 0) doc["leg_due_iso"] = fmtTimeLocal(dueTs);

    doc["leg_hours_since_success"] = leg_hours_since_success;
    doc["leg_hours_until_due"] = leg_hours_until_due;
    doc["leg_success_in_last_7d"] = leg_success_in_last_7d;
  }


// Multi DS18B20: expose alle gevonden sensoren in status JSON (voor UI + debug)
doc["temp_ctrl_addr"]   = tempControlAddrStr;
doc["temp_pub_names"]  = tempPublishNamesToHA;
JsonArray tarr = doc.createNestedArray("temps");
for (int i = 0; i < tempSensorCount; i++) {
  JsonObject o = tarr.createNestedObject();
  o["addr"]    = tempSensors[i].addrStr;
  o["name"]    = tempSensors[i].name;
  o["en"]      = tempSensors[i].enabled;
  o["present"] = tempSensors[i].present;
  if (tempIsValid(tempSensors[i].tempC)) o["temp_c"] = tempSensors[i].tempC;
}


  doc["uptime_s"]           = (uint32_t)(millis() / 1000UL);
  doc["wifi_rssi_dbm"]      = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;

  time_t nextRun = computeNextLegionellaEpoch();
  if (nextRun > 0) doc["leg_next_run"] = fmtTimeLocal(nextRun);
  if (legLastRunEpoch > 0) doc["leg_last_run"] = fmtTimeLocal((time_t)legLastRunEpoch);

  // --- Legionella compliance status (Step 2, read-only) ---
  // SUCCESS is stored when a legionella run completes with reason "DONE" (target + hold reached).
  // This block only reports status; it does NOT change any behaviour.
  {
    String leg_status = "DISABLED";
    bool leg_success_in_last_7d = false;
    uint32_t leg_hours_since_success = 0;
    uint32_t leg_hours_until_due = 0;

    if (!timeIsValid()) {
      leg_status = "UNKNOWN";  // NTP/time not valid
    } else {
      time_t nowTs = time(nullptr);

      if (legRunning) {
        leg_status = "RUNNING";
      } else if (!legUseTempSensor) {
        leg_status = "DISABLED";
      } else if (!ds18Available || !tempIsValid(lastTempC)) {
        leg_status = "NO_SENSOR";
      } else {
        if (legLastSuccessEpoch > 0 && nowTs >= (time_t)legLastSuccessEpoch) {
          uint32_t diffS = (uint32_t)(nowTs - (time_t)legLastSuccessEpoch);
          leg_hours_since_success = diffS / 3600UL;

          if (diffS < 168UL * 3600UL) {
            leg_success_in_last_7d = true;
            leg_status = "OK";
            leg_hours_until_due = 168UL - leg_hours_since_success;
          } else {
            leg_status = "DUE";
            leg_hours_until_due = 0;
          }
        } else {
          // Never succeeded yet (or clock moved backwards)
          leg_status = "DUE";
          leg_hours_until_due = 0;
        }
      }
    }

    doc["leg_status"] = leg_status;
    doc["leg_last_success_ts"] = (uint32_t)legLastSuccessEpoch;
    if (legLastSuccessEpoch > 0) doc["leg_last_success_iso"] = fmtTimeLocal((time_t)legLastSuccessEpoch);

    time_t dueTs = 0;
    if (legLastSuccessEpoch > 0) dueTs = (time_t)legLastSuccessEpoch + (time_t)(168UL * 3600UL);
    doc["leg_due_ts"] = (uint32_t)dueTs;
    if (dueTs > 0) doc["leg_due_iso"] = fmtTimeLocal(dueTs);

    doc["leg_hours_since_success"] = leg_hours_since_success;
    doc["leg_hours_until_due"] = leg_hours_until_due;
    doc["leg_success_in_last_7d"] = leg_success_in_last_7d;
  }


// Elektra prijsbron (voor UI/HA)
doc["elec_src"] = elecPriceSource;
doc["elec_src_label"] = (elecPriceSource==1) ? "fixed" : "dynamic";
doc["elec_fix_ct"] = elecFixedCtKwhAllIn;

// Gas vs Elektra (advies)
heatCompareUpdateNow();
doc["heat_auto"] = heatAutoEnabled;
doc["heat_exec_el"] = heatExecElecWhenCheaper;
  doc["heat_exec_active"] = heatExecElecActiveNow();
doc["heat_cheap_blocks_enabled"] = heatCheapBlocksEnabled;
doc["heat_cheap_use_boiler_target"] = heatCheapUseBoilerTarget;
doc["heat_cheap_blocks_max"] = heatCheapNBlocks;
doc["heat_decision_valid"] = heatPriceDecisionValidNow;
doc["heat_recommend"] = heatRecommendationLabelNow();
doc["heat_gas_ct_kwh_heat"]  = heatGasCtPerKwhHeatNow;
doc["heat_elec_ct_kwh_heat"] = heatElecCtPerKwhHeatNow;

  String payload;
  payload.reserve(700);
  serializeJson(doc, payload);

  {
    String t = (mqttBase()+"/status");
    bool ok = mqttClient.publish(t.c_str(), payload.c_str(), true);
    if (ok) lastStatusActualPublishMs = nowMs; // heartbeat marker (alleen bij succesvolle publish)
    if (!ok) {
      Serial.print("[MQTT] Status publish FAILED topic=");
      Serial.print(t);
      Serial.print(" len=");
      Serial.println(payload.length());
    }
  }
}

