#include "HealthMonitor.h"

#include <WiFi.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

#include "app_globals.h"
#include "ScenarioLogic.h"
#include "MultiController.h"
#include "MultiControllerInternal.h"
#include "RelayControl.h"
#include "sensors.h"
#include "PhaseGuard.h"
#include "MeterManager.h"
#include "MultiControllerTelemetry.h"
#include "StatusLabels.h"
#include "NetworkManager.h"

extern WebServer server;

extern String meterType;
extern String meterIp;
extern long   lastNetPowerW;
extern bool   sysPublishEnabled;
extern int    sysPublishIntervalSec;
extern unsigned long lastSysPublishMs;
extern uint8_t sunSchedMode;
extern int     sunWinStartMin;
extern int     sunWinEndMin;
extern int     sunDaylightStartMin;
extern int     sunDaylightEndMin;
extern int     sunCheapestStartMin;
extern int     sunCheapestEndMin;
extern String  sunWinDate;

extern String cachedWifiIP;
extern String cachedWifiSSID;
extern int    cachedWifiRSSI;
extern bool   cachedWifiConnected;
void updateWifiCache(bool force);

String resetReasonStr() {
  esp_reset_reason_t r = esp_reset_reason();
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT_RESET";
    case ESP_RST_SW:        return "SW_RESET";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

time_t computeNextLegionellaEpoch();

static String healthHaNodeId() {
  static String node;
  if (node.length() == 0) {
    uint32_t chipId = (uint32_t)ESP.getEfuseMac();
    char buf[40];
    snprintf(buf, sizeof(buf), "%s_%06X", DEVICE_ID, (unsigned int)(chipId & 0xFFFFFF));
    node = String(buf);
  }
  return node;
}

static String healthMqttSystemTopic() {
  return mqttBase() + "/system/state";
}

static void appendLegionellaCompliance(JsonDocument& doc) {
  String leg_status = "DISABLED";
  bool leg_success_in_last_7d = false;
  uint32_t leg_hours_since_success = 0;
  uint32_t leg_hours_until_due = 0;

  if (!timeIsValid()) {
    leg_status = "UNKNOWN";
  } else {
    time_t nowTs = time(nullptr);

    if (legRunning) {
      leg_status = "RUNNING";
    } else if (!legUseTempSensor) {
      leg_status = "DISABLED";
    } else if (!ds18Available || !tempIsValid(lastTempC)) {
      leg_status = "NO_SENSOR";
    } else if (legLastSuccessEpoch > 0 && nowTs >= (time_t)legLastSuccessEpoch) {
      uint32_t diffS = (uint32_t)(nowTs - (time_t)legLastSuccessEpoch);
      leg_hours_since_success = diffS / 3600UL;

      if (diffS < 168UL * 3600UL) {
        leg_success_in_last_7d = true;
        leg_status = "OK";
        leg_hours_until_due = 168UL - leg_hours_since_success;
      } else {
        leg_status = "DUE";
      }
    } else {
      leg_status = "DUE";
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

void registerHealthRoutes(WebServer& routeServer) {
  routeServer.on("/api/status", HTTP_GET, handleStatusJson);
  routeServer.on("/api/status_light", HTTP_GET, handleStatusLightJson);
  routeServer.on("/api/mc/peer_status", HTTP_GET, handleMultiControllerPeerStatusJson);
  routeServer.on("/api/live", HTTP_GET, handleLiveJson);
  routeServer.on("/api/system", HTTP_GET, handleSystemJson);
}

void mqttPublishSystemJson(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;
  if (!sysPublishEnabled) return;

  unsigned long nowMs = millis();
  unsigned long intervalMs = (unsigned long)sysPublishIntervalSec * 1000UL;

  if (!forceNow) {
    if (intervalMs < 10000UL) intervalMs = 10000UL;
    if (nowMs - lastSysPublishMs < intervalMs) return;
  }
  lastSysPublishMs = nowMs;

  StaticJsonDocument<900> doc;
  doc["fw"] = FW_VERSION;
  doc["uptime_s"] = (uint32_t)(millis() / 1000UL);
  doc["free_heap_b"] = (uint32_t)ESP.getFreeHeap();
  doc["wifi_rssi_dbm"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  doc["ip"]   = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
  doc["ssid"] = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("");
  doc["mac"]  = WiFi.macAddress();
  doc["reset_reason"] = resetReasonStr();
  doc["chip_model"] = String(ESP.getChipModel());
  doc["chip_rev"] = (uint32_t)ESP.getChipRevision();
  doc["sdk_version"] = String(ESP.getSdkVersion());
  doc["flash_size_b"] = (uint32_t)ESP.getFlashChipSize();
  doc["sketch_size_b"] = (uint32_t)ESP.getSketchSize();
#if defined(BOARD_HAS_PSRAM)
  doc["psram_b"] = (uint32_t)ESP.getPsramSize();
#else
  doc["psram_b"] = 0;
#endif

  String payload;
  payload.reserve(900);
  serializeJson(doc, payload);
  bool ok = mqttClient.publish(healthMqttSystemTopic().c_str(), payload.c_str(), true);
  if (!ok) {
    Serial.print("[MQTT] System publish FAILED topic=");
    Serial.print(healthMqttSystemTopic());
    Serial.print(" len=");
    Serial.println(payload.length());
  }
}

void handleStatusJson() {
  StaticJsonDocument<4500> doc;

  updateWifiCache(false);
  doc["fw"] = FW_VERSION;
  doc["mode"] = configMode ? "CONFIG" : "RUN";
  doc["wifi_connected"] = cachedWifiConnected;
  doc["wifi_rssi_dbm"] = cachedWifiConnected ? cachedWifiRSSI : 0;
  doc["ip"] = cachedWifiConnected ? cachedWifiIP : String("");
  doc["ssid"] = cachedWifiConnected ? cachedWifiSSID : String("");

  doc["mqtt_enabled"] = mqttEnabled;
  doc["mqtt_connected"] = (mqttEnabled && mqttClient.connected());
  doc["mqtt_base"] = mqttBase();
  doc["ha_disc"] = haDiscoveryEnabled;
  doc["ha_node_id"] = healthHaNodeId();

  doc["control_mode"] = modeLabel();
  doc["sun_sched_mode"] = sunSchedMode;
  doc["sun_manual_start_min"] = sunWinStartMin;
  doc["sun_manual_end_min"] = sunWinEndMin;
  doc["sun_daylight_start_min"] = sunDaylightStartMin;
  doc["sun_daylight_end_min"] = sunDaylightEndMin;
  doc["sun_cheapest_start_min"] = sunCheapestStartMin;
  doc["sun_cheapest_end_min"] = sunCheapestEndMin;
  doc["sun_window_date"] = sunWinDate;

  doc["force_heat"] = (controlMode == MODE_FORCE_HEAT);
  doc["relay_enabled"] = relayIsEnabled();
  doc["relay_gpio"] = relayGetPin();
  doc["relay_type"] = relayIsActiveLow() ? "active_low" : "active_high";
  doc["relay_boot_on"] = relayGetBootOn();
  doc["relay_manual_on"] = relayGetManualOn();

  doc["normal_algorithm"] = normalAlgLabel();
  doc["step_factor"] = stepFactor;
  doc["deadband_w"] = deadbandW;
  doc["max_step_pct"] = maxStepPctPerUpdatePerUpdate;

  doc["meter_type"] = meterType;
  doc["meter_label"] = meterLabel();
  doc["meter_ip"] = meterIp;
  doc["meter_ok"] = lastMeterOk;

  doc["net_power_w"] = lastNetPowerW;
  doc["error_w"] = lastErrorW;
  doc["overshoot_w"] = lastOvershootW;
  doc["duty_percent"] = lastDutyPercent;

  appendMultiControllerApiStatus(doc);

  doc["target_export_w"] = targetExportW;
  doc["max_boiler_w"] = maxBoilerPowerW;
  doc["min_step_percent"] = minStepPercent;
  doc["min_interval_ms"] = minUpdateIntervalMs;
  doc["max_output_percent"] = maxOutputPercentLimit;
  doc["time_valid"] = timeIsValid();

  doc["legionella_running"] = legRunning;
  doc["legionella_state"] = legStateLabel();
  doc["legionella_reason"] = legReason;
  doc["leg_schedule_enabled"] = legScheduleEnabled;
  doc["leg_dow"] = legDow;
  doc["leg_start_hour"] = legStartHour;
  doc["leg_start_minute"] = legStartMinute;
  doc["leg_duration_min"] = legDurationMin;
  doc["leg_output_limit_pct"] = legOutputLimitPercent;
  doc["leg_use_temp_sensor"] = legUseTempSensor;
  doc["leg_sensor_found"] = ds18Available;
  doc["leg_target_temp_c"] = legTargetTempC;
  doc["leg_hold_min"] = legHoldMin;
  if (tempIsValid(lastTempC)) doc["leg_temp_c"] = lastTempC;

  doc["uptime_s"] = (uint32_t)(millis() / 1000UL);
  time_t nextRun = computeNextLegionellaEpoch();
  if (nextRun > 0) doc["leg_next_run"] = fmtTimeLocal(nextRun);
  if (legLastRunEpoch > 0) doc["leg_last_run"] = fmtTimeLocal((time_t)legLastRunEpoch);

  appendLegionellaCompliance(doc);

  String out;
  out.reserve(2400);
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "application/json", out);
}

void handleStatusLightJson() {
  StaticJsonDocument<4600> doc;

  doc["fw"] = FW_VERSION;
  doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
  doc["wifi_rssi_dbm"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  doc["mqtt_enabled"] = mqttEnabled;
  doc["mqtt_connected"] = (mqttEnabled && mqttClient.connected());
  doc["control_mode"] = modeLabel();
  doc["normal_algorithm"] = normalAlgLabel();

  heatCompareUpdateNow();
  doc["heat_auto"] = heatAutoEnabled;
  doc["heat_exec_el"] = heatExecElecWhenCheaper;
  doc["heat_exec_active"] = heatExecElecActiveNow();
  doc["heat_cheap_blocks_enabled"] = heatCheapBlocksEnabled;
  doc["heat_cheap_use_boiler_target"] = heatCheapUseBoilerTarget;
  doc["heat_cheap_blocks_max"] = heatCheapNBlocks;
  doc["heat_cheap_plan_valid"] = heatCheapPlanValid;
  doc["heat_cheap_plan_selected"] = heatCheapPlanSelectedBlocks;
  doc["heat_cheap_plan_start_date"] = heatCheapPlanStartDate;
  doc["heat_cheap_plan_start_idx"] = heatCheapPlanStartIdx;
  doc["heat_cheap_plan_end_date"] = heatCheapPlanEndDate;
  doc["heat_cheap_plan_end_idx_excl"] = heatCheapPlanEndIdxExcl;
  doc["heat_cheap_plan_interval_min"] = heatCheapPlanIntervalMin;
  doc["heat_cheap_temp_limit_enabled"] = heatCheapTemperatureModeEnabledNow();
  doc["heat_cheap_temp_sensor_assigned"] = heatCheapTemperatureSensorAssignedNow();
  doc["heat_cheap_temp_sensor_valid"] = heatCheapTemperatureSensorValidNow();
  doc["heat_cheap_temp_demand"] = heatCheapTemperatureDemandActiveNow();
  doc["heat_cheap_temp_sensor_name"] = heatCheapTemperatureSensorNameNow();
  if (heatCheapTemperatureSensorValidNow()) {
    doc["heat_cheap_temp_c"] = heatCheapTemperatureCurrentCNow();
    doc["heat_cheap_temp_rearm_c"] = heatCheapTemperatureStartCNow();
    doc["heat_cheap_temp_target_c"] = heatCheapTemperatureStopCNow();
  }
  doc["heat_decision_valid"] = heatPriceDecisionValidNow;
  doc["heat_recommend"] = heatRecommendationLabelNow();
  doc["heat_gas_ct_kwh_heat"] = heatGasCtPerKwhHeatNow;
  doc["heat_elec_ct_kwh_heat"] = heatElecCtPerKwhHeatNow;

  doc["sun_sched_mode"] = sunSchedMode;
  doc["sun_daylight_start_min"] = sunDaylightStartMin;
  doc["sun_daylight_end_min"] = sunDaylightEndMin;
  doc["sun_cheapest_start_min"] = sunCheapestStartMin;
  doc["sun_cheapest_end_min"] = sunCheapestEndMin;

  doc["meter_ok"] = lastMeterOk;
  doc["meter_label"] = meterLabel();
  appendMultiControllerApiStatusLight(doc);

  doc["legionella_state"] = legStateLabel();
  doc["legionella_running"] = legRunning;
  doc["relay_enabled"] = relayIsEnabled();
  doc["relay_gpio"] = relayGetPin();
  doc["relay_type"] = relayIsActiveLow() ? "active_low" : "active_high";
  doc["relay_boot_on"] = relayGetBootOn();
  doc["relay_manual_on"] = relayGetManualOn();
  doc["uptime_s"] = (uint32_t)(millis() / 1000UL);

  if (tempSensorCount > 0) {
    JsonArray ta = doc.createNestedArray("temps");
    for (int i = 0; i < tempSensorCount; i++) {
      if (!tempSensors[i].enabled) continue;
      if (!tempSensors[i].present) continue;
      JsonObject to = ta.createNestedObject();
      const String nm = (tempSensors[i].name.length() > 0) ? tempSensors[i].name : tempSensors[i].addrStr;
      to["name"] = nm;
      if (!isnan(tempSensors[i].tempC)) to["t"] = tempSensors[i].tempC;
    }
  }

  String out;
  out.reserve(512);
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "application/json", out);
}

void handleMultiControllerPeerStatusJson() {
  StaticJsonDocument<2300> doc;

  doc["fw"] = FW_VERSION;
  doc["uptime_s"] = (uint32_t)(millis() / 1000UL);
  appendMultiControllerPeerCompactStatus(doc);

  String out;
  out.reserve(1024);
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "application/json", out);
}

static void appendMultiControllerDashboardSlots(JsonDocument& doc) {
  JsonArray arr = doc.createNestedArray("mc_dashboard_slots");
  if (!mcEnabled) return;

  // Stap 7 versnellen: deze live-route wordt elke seconde door het dashboard
  // uitgelezen. Houd daarom alleen de velden over die de live-chips echt nodig
  // hebben (slot, naam en duty), zodat Multi Controller minder JSON en parsing
  // veroorzaakt terwijl de UI open staat.
  for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
    if (i >= mcControllerCount) break;

    const bool localSlot = (mcSlots[i].slotType == MC_SLOT_LOCAL_PWM);
    const bool networkSlot = (mcSlots[i].slotType == MC_SLOT_NETWORK_PEER);
    if (!localSlot && !networkSlot) continue;
    if (!localSlot && mcRole != MC_ROLE_MASTER) continue;

    JsonObject mcObj = arr.createNestedObject();
    mcObj["slot"] = i + 1;
    mcObj["name"] = mcSlots[i].name.length() ? mcSlots[i].name : (String("Controller ") + String(i + 1));

    float pct = 0.0f;
    if (localSlot) {
      pct = multiControllerGetLocalPwmSlotPercent(i);
    } else {
      pct = mcPeerRuntime[i].assignedPct;
    }

    mcObj["duty_percent"] = pct;
  }
}

void handleLiveJson() {
  StaticJsonDocument<1536> doc;

  doc["net_power_w"] = lastNetPowerW;
  doc["error_w"] = lastErrorW;
  doc["overshoot_w"] = lastOvershootW;
  doc["duty_percent"] = lastDutyPercent;
  doc["mc_enabled"] = mcEnabled;
  doc["mc_fallback_active"] = multiControllerIsFallbackActive();
  doc["mc_local_pwm_outputs_effective"] = multiControllerGetEffectiveLocalPwmCount();
  doc["mc_local_pwm_outputs_active"] = multiControllerGetActiveLocalPwmCount();
  doc["mc_group_pwm_requested_pct"] = multiControllerGetRequestedGroupPercent();
  doc["mc_group_pwm_assigned_pct"] = multiControllerGetAssignedGroupPercent();
  doc["mc_scenario"] = multiControllerGetScenarioLabel();
  doc["mc_negative_override_active"] = multiControllerIsNegativePriorityOverrideActive();
  doc["mc_surplus_group_max_pct"] = multiControllerGetSurplusGroupMaxPercent();
  appendMultiControllerDashboardSlots(doc);

  const bool phasesVisible = (meterType == "youless" || meterType == "homewizard" || meterType == "smartgateways") && lastPhasesOk;
  doc["phases_ok"] = phasesVisible;
  if (phasesVisible) {
    doc["i1_a"] = isnan(lastPhaseI1A) ? 0.0f : lastPhaseI1A;
    doc["i2_a"] = isnan(lastPhaseI2A) ? 0.0f : lastPhaseI2A;
    doc["i3_a"] = isnan(lastPhaseI3A) ? 0.0f : lastPhaseI3A;
  }

  doc["pg_en"] = phaseGuardEnabled;
  doc["pg_ph"] = phaseGuardPhase;
  doc["pg_maxa"] = getConfiguredPhaseGuardMaxA(phaseGuardPhase);
  doc["pg_maxa_l1"] = phaseGuardMaxA_L1;
  doc["pg_maxa_l2"] = phaseGuardMaxA_L2;
  doc["pg_maxa_l3"] = phaseGuardMaxA_L3;
  doc["pg_mode"] = phaseGuardModeActive;
  doc["pg_limit"] = phaseGuardWouldLimit;
  doc["pg_a"] = isfinite(phaseGuardSelectedA) ? phaseGuardSelectedA : 0.0f;
  doc["pg_over"] = phaseGuardOverA;

  String out;
  out.reserve(512);
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "application/json", out);
}

void handleSystemJson() {
  // step308: WiFi-herstel diagnose is bewust ALLEEN via /api/system zichtbaar.
  // Geen extra MQTT-velden en geen UI-wijzigingen.
  StaticJsonDocument<1600> doc;
  doc["fw"] = FW_VERSION;
  doc["uptime_s"] = (uint32_t)(millis() / 1000UL);
  doc["free_heap_b"] = (uint32_t)ESP.getFreeHeap();
  doc["wifi_rssi_dbm"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  doc["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
  doc["ssid"] = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("");
  doc["mac"] = WiFi.macAddress();
  doc["reset_reason"] = resetReasonStr();

  doc["wifi_status"] = wifiStatusText();
  doc["wifi_disconnect_count"] = wifiDisconnectCount();
  doc["wifi_reconnect_count"] = wifiReconnectCount();
  doc["wifi_reconnect_success_count"] = wifiReconnectSuccessCount();
  doc["wifi_reconnect_attempts_current_outage"] = wifiReconnectAttemptsCurrentOutage();
  doc["wifi_last_disconnect_reason"] = wifiLastDisconnectReason();
  doc["wifi_last_disconnect_reason_code"] = wifiLastDisconnectReasonCode();
  doc["wifi_ps_mode"] = wifiPowerSaveModeText();
  doc["wifi_last_event"] = wifiLastEventText();

  JsonObject prevWifi = doc.createNestedObject("wifi_previous_session");
  prevWifi["event"] = wifiPreviousSessionEvent();
  prevWifi["last_disconnect_reason"] = wifiPreviousSessionDisconnectReason();
  prevWifi["reconnect_attempts"] = wifiPreviousSessionReconnectAttempts();
  prevWifi["event_uptime_s"] = wifiPreviousSessionEventUptimeS();

  doc["chip_model"] = String(ESP.getChipModel());
  doc["chip_rev"] = (uint32_t)ESP.getChipRevision();
  doc["sdk_version"] = String(ESP.getSdkVersion());
  doc["flash_size_b"] = (uint32_t)ESP.getFlashChipSize();
  doc["sketch_size_b"] = (uint32_t)ESP.getSketchSize();
#if defined(BOARD_HAS_PSRAM)
  doc["psram_b"] = (uint32_t)ESP.getPsramSize();
#else
  doc["psram_b"] = 0;
#endif

  String out;
  out.reserve(1600);
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "application/json", out);
}

String fmtUptimeDHMM(uint32_t uptimeS) {
  uint32_t days = uptimeS / 86400UL;
  uint32_t rem = uptimeS % 86400UL;
  uint32_t hours = rem / 3600UL;
  uint32_t mins = (rem % 3600UL) / 60UL;

  char buf[24];
  if (days > 0) {
    snprintf(buf, sizeof(buf), "%lud %02lu:%02lu", (unsigned long)days, (unsigned long)hours, (unsigned long)mins);
  } else {
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)hours, (unsigned long)mins);
  }
  return String(buf);
}
