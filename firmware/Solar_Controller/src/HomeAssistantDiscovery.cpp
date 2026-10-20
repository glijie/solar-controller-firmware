#include "HomeAssistantDiscovery.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "MultiController.h"
#include "app_globals.h"
#include "sensors.h"
// ==================================================
// Home Assistant MQTT Discovery (auto entities)
// ==================================================
const char* HA_DISCOVERY_PREFIX = "homeassistant";

String haNodeId() {
  // Uniek per device, stabiel: solar_controller_XXXXXX (chip-id low 24 bits)
  static String node;
  if (node.length() == 0) {
    uint32_t chipId = (uint32_t)ESP.getEfuseMac();
    char buf[40];
    snprintf(buf, sizeof(buf), "%s_%06X", DEVICE_ID, (unsigned int)(chipId & 0xFFFFFF));
    node = String(buf);
  }
  return node;
}

static String haUniqueId(const String& objectId) {
  return haNodeId() + "_" + objectId;
}

static void haAddAvailability(JsonDocument& doc) {
  doc["availability_topic"] = mqttAvailabilityTopic();
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";
}

static void haAddDevice(JsonDocument& doc) {
  JsonObject dev = doc.createNestedObject("device");
  dev["name"] = DEVICE_NAME;
  JsonArray ids = dev.createNestedArray("identifiers");
  ids.add(haNodeId());
  dev["manufacturer"] = "DIY";
  dev["model"] = "Solar Controller";
  dev["sw_version"] = FW_VERSION;

  if (WiFi.status() == WL_CONNECTED) {
    dev["configuration_url"] = String("http://") + WiFi.localIP().toString() + "/";
  }
}

static String haConfigTopic(const char* component, const String& objectId) {
  // homeassistant/<component>/<node_id>/<object_id>/config
  String t;
  t.reserve(128);
  t += HA_DISCOVERY_PREFIX;
  t += "/";
  t += component;
  t += "/";
  t += haNodeId();
  t += "/";
  t += objectId;
  t += "/config";
  return t;
}

static void mqttPublishHADiscoveryDoc(const char* component, const String& objectId, JsonDocument& doc) {
  if (!mqttEnabled || !mqttClient.connected()) return;
  if (!haDiscoveryEnabled) return;

  String topic = haConfigTopic(component, objectId);
  String payload;
  payload.reserve(700);
  serializeJson(doc, payload);

  bool ok = mqttClient.publish(topic.c_str(), payload.c_str(), true); // retained config
  if (!ok) {
    Serial.printf("[MQTT] DISCOVERY publish FAILED (%u bytes) topic=%s\n", (unsigned)payload.length(), topic.c_str());
  }
}

static uint8_t haConfiguredSlotCount() {
  if (mcControllerCount < 1) return 1;
  return (mcControllerCount > MC_MAX_SLOTS) ? MC_MAX_SLOTS : mcControllerCount;
}

static uint16_t mqttClearHADiscoveryConfig(const char* component, const String& objectId) {
  if (!mqttClient.connected()) return 0;

  const String topic = haConfigTopic(component, objectId);
  const bool ok = mqttClient.publish(topic.c_str(), "", true);
  if (!ok) {
    Serial.printf("[MQTT] DISCOVERY clear FAILED topic=%s\n", topic.c_str());
    return 0;
  }
  return 1;
}

static uint16_t mqttCleanupHATempSensorDiscovery() {
  uint16_t cleared = 0;

  auto clearTempAddr = [&](const String& addr) {
    if (addr.length() == 16) {
      cleared += mqttClearHADiscoveryConfig("sensor", "temp_" + addr);
    }
  };

  for (int i = 0; i < tempSensorCount; i++) {
    clearTempAddr(tempSensors[i].addrStr);
  }

  if (tempSensorMapJson.length() > 0) {
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, tempSensorMapJson);
    if (!err && doc.is<JsonObject>()) {
      JsonObject root = doc.as<JsonObject>();
      for (JsonPair kv : root) {
        clearTempAddr(String(kv.key().c_str()));
      }
    }
  }

  return cleared;
}

static uint16_t mqttCleanupHAMultiControllerSlots() {
  uint16_t cleared = 0;

  // Slot 4+ covers retained discovery from older/experimental builds.
  for (uint8_t slot = 1; slot <= 8; slot++) {
    const String idx = String(slot);
    cleared += mqttClearHADiscoveryConfig("sensor", "mc_slot" + idx + "_duty_percent");
    cleared += mqttClearHADiscoveryConfig("sensor", "mc_slot" + idx + "_phase");
  }

  return cleared;
}

uint16_t mqttCleanupHADiscovery() {
  if (!mqttClient.connected()) return 0;

  uint16_t cleared = 0;

  struct DiscoveryEntry {
    const char* component;
    const char* objectId;
  };

  static const DiscoveryEntry entries[] = {
    {"sensor", "status"},
    {"sensor", "legionella_state"},
    {"sensor", "legionella_reason"},
    {"sensor", "leg_next_run"},
    {"sensor", "leg_last_run"},
    {"sensor", "leg_status"},
    {"sensor", "leg_last_success_ts"},
    {"sensor", "leg_last_success_iso"},
    {"sensor", "leg_due_ts"},
    {"sensor", "leg_due_iso"},
    {"sensor", "leg_hours_since_success"},
    {"sensor", "leg_hours_until_due"},
    {"sensor", "leg_dow"},
    {"sensor", "leg_start_hour"},
    {"sensor", "leg_start_minute"},
    {"sensor", "leg_duration_min"},
    {"sensor", "leg_output_limit_pct"},
    {"sensor", "leg_target_temp_c"},
    {"sensor", "leg_hold_min"},
    {"sensor", "net_power_w"},
    {"sensor", "duty_percent"},
    {"sensor", "error_w"},
    {"sensor", "overshoot_w"},
    {"sensor", "leg_temp_c"},
    {"sensor", "wifi_rssi_dbm"},
    {"sensor", "uptime_s"},
    {"sensor", "heat_recommend"},
    {"sensor", "mc_scenario"},
    {"sensor", "mc_group_pwm_requested_pct"},
    {"sensor", "mc_group_pwm_assigned_pct"},
    {"sensor", "mc_surplus_group_max_pct"},
    {"sensor", "mc_local_pwm_outputs_requested"},
    {"sensor", "mc_local_pwm_outputs_effective"},
    {"sensor", "mc_local_pwm_outputs_active"},

    {"binary_sensor", "meter_ok"},
    {"binary_sensor", "time_valid"},
    {"binary_sensor", "legionella_running"},
    {"binary_sensor", "leg_schedule_enabled"},
    {"binary_sensor", "leg_use_temp_sensor"},
    {"binary_sensor", "leg_sensor_found"},
    {"binary_sensor", "leg_success_in_last_7d"},
    {"binary_sensor", "heat_exec_active"},
    {"binary_sensor", "mc_enabled"},
    {"binary_sensor", "mc_fallback_active"},
    {"binary_sensor", "mc_negative_override_active"},

    {"switch", "force_heat"},
    {"switch", "relay_manual"},
    {"switch", "legionella_schedule"},
    {"switch", "leg_use_temp_sensor"},
    {"switch", "dash_sun_label"},
    {"switch", "np_enabled"},
    {"switch", "np_incl_taxes"},
    {"switch", "np_boost_enabled"},
    {"switch", "heat_auto"},
    {"switch", "heat_exec_el"},
    {"switch", "heat_cheap_blocks_enabled"},
    {"switch", "heat_cheap_use_boiler_target"},

    {"button", "legionella_run_now"},
    {"button", "legionella_cancel"},

    {"select", "normal_algorithm"},
    {"select", "leg_dow"},
    {"select", "sun_sched_mode"},
    {"select", "elec_price_source"},
    {"select", "np_res_min"},
    {"select", "np_boost_price_source"},

    {"number", "step_factor"},
    {"number", "deadband_w"},
    {"number", "max_step_pct"},
    {"number", "max_output_pct"},
    {"number", "legionella_output_pct"},
    {"number", "target_export_w"},
    {"number", "max_boiler_power_w"},
    {"number", "min_step_percent"},
    {"number", "min_interval_s"},
    {"number", "leg_start_hour"},
    {"number", "leg_start_minute"},
    {"number", "leg_duration_min"},
    {"number", "leg_target_temp_c"},
    {"number", "leg_hold_min"},
    {"number", "sun_cheapest_blocks"},
    {"number", "sun_daylight_margin_min"},
    {"number", "sun_lat"},
    {"number", "sun_lon"},
    {"number", "elec_fixed_ct"},
    {"number", "np_energy_tax_ct"},
    {"number", "np_inkoop_ct"},
    {"number", "np_btw_pct"},
    {"number", "np_boost_threshold_ct"},
    {"number", "np_boost_n_blocks"},
    {"number", "heat_cheap_blocks_max"},

    {"text", "sun_manual_start"},
    {"text", "sun_manual_end"},
    {"text", "np_area"},
  };

  for (const DiscoveryEntry& entry : entries) {
    cleared += mqttClearHADiscoveryConfig(entry.component, entry.objectId);
  }

  cleared += mqttCleanupHATempSensorDiscovery();
  cleared += mqttCleanupHAMultiControllerSlots();

  Serial.printf("[MQTT] HA Discovery cleanup published %u retained clears\n", (unsigned)cleared);
  return cleared;
}

uint16_t mqttRebuildHADiscovery() {
  const uint16_t cleared = mqttCleanupHADiscovery();
  delay(900);

  if (haDiscoveryEnabled) {
    mqttPublishHADiscovery(true);
  }

  return cleared;
}

void mqttPublishHADiscovery(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;
  if (!haDiscoveryEnabled) return;

  // voorkom spam: normaal maar 1x per boot/reconnect, maar bij HA online forceren we
  static bool published = false;
  if (published && !forceNow) return;
  published = true;

  const String base = mqttBase();
  const String statusTopic = base + "/status";

  // --------- 1) Hybride "status" sensor (alles-in-één attributes) ---------
  {
    StaticJsonDocument<512> d;
    d["name"] = String(DEVICE_NAME) + " Status";
    d["unique_id"] = haUniqueId("status");
    d["state_topic"] = statusTopic;
    d["value_template"] = "{{ value_json.mode }}";
    d["json_attributes_topic"] = statusTopic;
    d["entity_category"] = "diagnostic";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("sensor", "status", d);
  }

  // --------- 2) Sensors (uit JSON status) ---------
  auto pubJsonSensor = [&](const String& objectId, const String& nameSuffix,
                          const String& tpl, const String& unit,
                          const String& deviceClass, const String& entityCategory) {
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " " + nameSuffix;
    d["unique_id"] = haUniqueId(objectId);
    d["state_topic"] = statusTopic;
    d["value_template"] = tpl;
    if (unit.length() > 0) d["unit_of_measurement"] = unit;
    if (deviceClass.length() > 0) d["device_class"] = deviceClass;
    if (unit.length() > 0) d["state_class"] = "measurement";
    if (entityCategory.length() > 0) d["entity_category"] = entityCategory;
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("sensor", objectId, d);
  };

  // Snelle kerntelemetrie niet meer uit het grote JSON-statusbericht halen.
  // Zo blijven bestaande HA-entities backward compatible, maar krijgen ze hun waarde
  // uit lichte losse state-topics die ook in Multi Controller-modus betrouwbaar blijven.

  // --------- 1a) Legionella (uit JSON status) ---------
  // Alles komt uit <base>/status (JSON). Hierdoor verschijnen alle legionella-velden automatisch als entiteiten in Home Assistant.
  pubJsonSensor("legionella_state",        "Legionella state",        "{{ value_json.legionella_state }}",        "",   "", "diagnostic");
  pubJsonSensor("legionella_reason",       "Legionella reason",       "{{ value_json.legionella_reason }}",       "",   "", "diagnostic");
  pubJsonSensor("leg_next_run",            "Legionella next run",     "{{ value_json.leg_next_run }}",            "",   "", "diagnostic");
  pubJsonSensor("leg_last_run",            "Legionella last run",     "{{ value_json.leg_last_run }}",            "",   "", "diagnostic");

  pubJsonSensor("leg_status",              "Legionella status",       "{{ value_json.leg_status }}",              "",   "", "diagnostic");
  pubJsonSensor("leg_last_success_ts",     "Legionella last success (ts)", "{{ value_json.leg_last_success_ts }}", "s",  "", "diagnostic");
  pubJsonSensor("leg_last_success_iso",    "Legionella last success", "{{ value_json.leg_last_success_iso }}",    "",   "", "diagnostic");
  pubJsonSensor("leg_due_ts",              "Legionella due (ts)",     "{{ value_json.leg_due_ts }}",              "s",  "", "diagnostic");
  pubJsonSensor("leg_due_iso",             "Legionella due",          "{{ value_json.leg_due_iso }}",             "",   "", "diagnostic");
  pubJsonSensor("leg_hours_since_success", "Legionella hours since success", "{{ value_json.leg_hours_since_success }}", "h", "", "diagnostic");
  pubJsonSensor("leg_hours_until_due",     "Legionella hours until due",     "{{ value_json.leg_hours_until_due }}",     "h", "", "diagnostic");

  // Legionella instellingen (handig in HA, diagnostic)
  pubJsonSensor("leg_dow",                 "Legionella day of week",  "{{ value_json.leg_dow }}",                 "",   "", "diagnostic");
  pubJsonSensor("leg_start_hour",          "Legionella start hour",   "{{ value_json.leg_start_hour }}",          "",   "", "diagnostic");
  pubJsonSensor("leg_start_minute",        "Legionella start minute", "{{ value_json.leg_start_minute }}",        "",   "", "diagnostic");
  pubJsonSensor("leg_duration_min",        "Legionella duration",     "{{ value_json.leg_duration_min }}",        "min","", "diagnostic");
  pubJsonSensor("leg_output_limit_pct",    "Legionella output limit", "{{ value_json.leg_output_limit_pct }}",    "%",  "", "diagnostic");
  pubJsonSensor("leg_target_temp_c",       "Legionella target temp",  "{{ value_json.leg_target_temp_c }}",       "°C", "temperature", "diagnostic");
  pubJsonSensor("leg_hold_min",            "Legionella hold",         "{{ value_json.leg_hold_min }}",            "min","", "diagnostic");
  pubJsonSensor("heat_recommend",          "Verwarmadvies",            "{{ value_json.heat_recommend }}",          "",   "", "");



// --------- 2a) DS18B20 temp sensoren (per sensor topic) ---------
// - state_topic: <base>/temp/<addr>/state  (retained, plain value)
// - unique_id: gebaseerd op address -> stabiel, ook na reboot
// - name: (optioneel) vanuit Solar Controller UI, zodat HA dit automatisch overneemt
for (int i = 0; i < tempSensorCount; i++) {
  const TempSensorEntry& e = tempSensors[i];
  if (e.addrStr.length() != 16) continue;

  StaticJsonDocument<768> d;
  String nameSuffix = tempPublishNamesToHA ? e.name : (String("Temp ") + String(i + 1));
  d["name"] = String(DEVICE_NAME) + " " + nameSuffix;

  String obj = "temp_" + e.addrStr;           // object_id binnen node_id
  d["unique_id"] = haUniqueId(obj);
  d["state_topic"] = base + "/temp/" + e.addrStr + "/state";
  d["unit_of_measurement"] = "°C";
  d["device_class"] = "temperature";
  d["state_class"] = "measurement";
  d["entity_category"] = "diagnostic";

  // respecteer enable in Solar Controller: HA entity is "enabled_by_default" gelijk aan UI toggle
  d["enabled_by_default"] = e.enabled;

  haAddAvailability(d);
  haAddDevice(d);
  mqttPublishHADiscoveryDoc("sensor", obj, d);
}


  // --------- 3) Binary sensors (uit JSON status) ---------
  auto pubJsonBinary = [&](const String& objectId, const String& nameSuffix, const String& tpl,
                           const String& deviceClass, const String& entityCategory) {
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " " + nameSuffix;
    d["unique_id"] = haUniqueId(objectId);
    d["state_topic"] = statusTopic;
    d["value_template"] = tpl; // moet "ON"/"OFF" opleveren
    d["payload_on"] = "ON";
    d["payload_off"] = "OFF";
    if (deviceClass.length() > 0) d["device_class"] = deviceClass;
    if (entityCategory.length() > 0) d["entity_category"] = entityCategory;
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("binary_sensor", objectId, d);
  };

  pubJsonBinary("meter_ok",          "Meter OK",          "{{ 'ON' if value_json.meter_ok else 'OFF' }}", "", "diagnostic");
  pubJsonBinary("time_valid",        "Tijd geldig",       "{{ 'ON' if value_json.time_valid else 'OFF' }}","", "diagnostic");
  pubJsonBinary("legionella_running","Legionella actief", "{{ 'ON' if value_json.legionella_running else 'OFF' }}","", "");
  pubJsonBinary("leg_schedule_enabled","Legionella planning", "{{ 'ON' if value_json.leg_schedule_enabled else 'OFF' }}", "", "diagnostic");
  pubJsonBinary("leg_use_temp_sensor","Legionella temp-modus", "{{ 'ON' if value_json.leg_use_temp_sensor else 'OFF' }}", "", "diagnostic");
  pubJsonBinary("leg_sensor_found",  "Legionella sensor gevonden", "{{ 'ON' if value_json.leg_sensor_found else 'OFF' }}", "", "diagnostic");
  pubJsonBinary("leg_success_in_last_7d","Legionella OK < 7d", "{{ 'ON' if value_json.leg_success_in_last_7d else 'OFF' }}", "", "diagnostic");
  pubJsonBinary("heat_exec_active", "Verwarmen via ELEC-advies actief", "{{ 'ON' if value_json.heat_exec_active else 'OFF' }}", "", "");

  // --------- 3b) Multi Controller (eigen MQTT state-topics) ---------
  auto pubTopicSensor = [&](const String& objectId, const String& nameSuffix,
                            const String& stateTopic, const String& unit,
                            const String& deviceClass, const String& entityCategory) {
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " " + nameSuffix;
    d["unique_id"] = haUniqueId(objectId);
    d["state_topic"] = stateTopic;
    if (unit.length() > 0) d["unit_of_measurement"] = unit;
    if (deviceClass.length() > 0) d["device_class"] = deviceClass;
    if (unit.length() > 0) d["state_class"] = "measurement";
    if (entityCategory.length() > 0) d["entity_category"] = entityCategory;
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("sensor", objectId, d);
  };

  auto pubTopicBinary = [&](const String& objectId, const String& nameSuffix,
                            const String& stateTopic, const String& deviceClass,
                            const String& entityCategory) {
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " " + nameSuffix;
    d["unique_id"] = haUniqueId(objectId);
    d["state_topic"] = stateTopic;
    d["payload_on"] = "ON";
    d["payload_off"] = "OFF";
    if (deviceClass.length() > 0) d["device_class"] = deviceClass;
    if (entityCategory.length() > 0) d["entity_category"] = entityCategory;
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("binary_sensor", objectId, d);
  };

  pubTopicSensor("net_power_w",   "Netvermogen",
                 base + "/live/net_power_w/state", "W", "power", "");
  pubTopicSensor("duty_percent",  "Boiler PWM",
                 base + "/live/duty_percent/state", "%", "", "");
  pubTopicSensor("error_w",       "Fout (W)",
                 base + "/live/error_w/state", "W", "", "");
  pubTopicSensor("overshoot_w",   "Overschot (W)",
                 base + "/live/overshoot_w/state", "W", "", "");
  pubTopicSensor("leg_temp_c",    "Legionella temp",
                 base + "/live/leg_temp_c/state", "°C", "temperature", "");
  pubTopicSensor("wifi_rssi_dbm", "WiFi RSSI",
                 base + "/live/wifi_rssi_dbm/state", "dBm", "signal_strength", "diagnostic");
  pubTopicSensor("uptime_s",      "Uptime",
                 base + "/live/uptime_s/state", "s", "duration", "diagnostic");

  pubTopicBinary("mc_enabled", "Multi Controller actief",
                 base + "/mc/enabled/state", "", "diagnostic");
  pubTopicBinary("mc_fallback_active", "Multi Controller fallback actief",
                 base + "/mc/fallback_active/state", "", "diagnostic");
  pubTopicBinary("mc_negative_override_active", "Multi Controller negatieve prijs override",
                 base + "/mc/negative_override_active/state", "", "diagnostic");

  pubTopicSensor("mc_scenario", "Multi Controller scenario",
                 base + "/mc/scenario/state", "", "", "");
  pubTopicSensor("mc_group_pwm_requested_pct", "Multi Controller groepsvraag PWM",
                 base + "/mc/group_pwm_requested_pct/state", "%", "", "");
  pubTopicSensor("mc_group_pwm_assigned_pct", "Multi Controller groep toegewezen PWM",
                 base + "/mc/group_pwm_assigned_pct/state", "%", "", "");
  pubTopicSensor("mc_surplus_group_max_pct", "Multi Controller surplus max PWM",
                 base + "/mc/surplus_group_max_pct/state", "%", "", "diagnostic");
  pubTopicSensor("mc_local_pwm_outputs_requested", "Multi Controller gevraagde uitgangen",
                 base + "/mc/local_pwm_outputs_requested/state", "", "", "diagnostic");
  pubTopicSensor("mc_local_pwm_outputs_effective", "Multi Controller effectieve uitgangen",
                 base + "/mc/local_pwm_outputs_effective/state", "", "", "diagnostic");
  pubTopicSensor("mc_local_pwm_outputs_active", "Multi Controller actieve uitgangen",
                 base + "/mc/local_pwm_outputs_active/state", "", "", "diagnostic");

  const uint8_t slotCount = haConfiguredSlotCount();
  for (uint8_t i = 0; i < slotCount; i++) {
    String idx = String(i + 1);
    pubTopicSensor("mc_slot" + idx + "_duty_percent", "Multi Controller slot " + idx + " PWM",
                   base + "/mc/slot" + idx + "/duty_percent/state", "%", "", "");
    pubTopicSensor("mc_slot" + idx + "_phase", "Multi Controller slot " + idx + " fase",
                   base + "/mc/slot" + idx + "/phase/state", "", "", "diagnostic");
  }

  // --------- 4) Switches / Controls (via bestaande state/set topics) ---------
  {
    // Force heat switch
    StaticJsonDocument<512> d;
    d["name"] = String(DEVICE_NAME) + " Force heat";
    d["unique_id"] = haUniqueId("force_heat");
    d["command_topic"] = base + "/force_heat/set";
    d["state_topic"]   = base + "/force_heat/state";
    d["payload_on"] = "ON";
    d["payload_off"] = "OFF";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("switch", "force_heat", d);
  }

  {
    // Relay manual switch
    StaticJsonDocument<512> d;
    d["name"] = String(DEVICE_NAME) + " Relais handmatig";
    d["unique_id"] = haUniqueId("relay_manual");
    d["command_topic"] = base + "/relay/set";
    d["state_topic"]   = base + "/relay/state";
    d["payload_on"] = "ON";
    d["payload_off"] = "OFF";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("switch", "relay_manual", d);
  }

  {
    // Legionella schedule enable switch
    StaticJsonDocument<512> d;
    d["name"] = String(DEVICE_NAME) + " Legionella planning";
    d["unique_id"] = haUniqueId("legionella_schedule");
    d["command_topic"] = base + "/legionella/enable/set";
    d["state_topic"]   = base + "/legionella/enable/state";
    d["payload_on"] = "ON";
    d["payload_off"] = "OFF";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("switch", "legionella_schedule", d);
  }

  // Buttons
  {
    StaticJsonDocument<512> d;
    d["name"] = String(DEVICE_NAME) + " Legionella start nu";
    d["unique_id"] = haUniqueId("legionella_run_now");
    d["command_topic"] = base + "/legionella/run_now";
    d["payload_press"] = "ON";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("button", "legionella_run_now", d);
  }
  {
    StaticJsonDocument<512> d;
    d["name"] = String(DEVICE_NAME) + " Legionella stop";
    d["unique_id"] = haUniqueId("legionella_cancel");
    d["command_topic"] = base + "/legionella/cancel";
    d["payload_press"] = "ON";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("button", "legionella_cancel", d);
  }

  // Select: NORMAL algorithm
  {
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " NORMAL algoritme";
    d["unique_id"] = haUniqueId("normal_algorithm");
    d["command_topic"] = base + "/tuning/algorithm/set";
    d["state_topic"]   = base + "/tuning/algorithm/state";
    JsonArray opts = d.createNestedArray("options");
    opts.add("STEP");
    opts.add("DIRECT");
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("select", "normal_algorithm", d);
  }

  // Numbers: tuning
  auto pubNumber = [&](const String& objectId, const String& nameSuffix,
                       const String& cmd, const String& st,
                       float minV, float maxV, float stepV, const String& unit) {
    StaticJsonDocument<768> d;
    d["name"] = String(DEVICE_NAME) + " " + nameSuffix;
    d["unique_id"] = haUniqueId(objectId);
    d["command_topic"] = cmd;
    d["state_topic"]   = st;
    d["min"] = minV;
    d["max"] = maxV;
    d["step"] = stepV;
    d["mode"] = "box";
    if (unit.length() > 0) d["unit_of_measurement"] = unit;
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("number", objectId, d);
  };

  pubNumber("step_factor", "Stapfactor", base + "/tuning/step_factor/set", base + "/tuning/step_factor/state",
            0.01f, 2.00f, 0.01f, "");
  pubNumber("deadband_w", "Deadband", base + "/tuning/deadband_w/set", base + "/tuning/deadband_w/state",
            0.0f, 500.0f, 1.0f, "W");
  pubNumber("max_step_pct", "Max stap", base + "/tuning/max_step_pct/set", base + "/tuning/max_step_pct/state",
            0.1f, 50.0f, 0.5f, "%");

// --------- 5) Safety limits (numbers) ---------
pubNumber("max_output_pct", "Max output (Kemo)", base + "/limits/max_output_pct/set", base + "/limits/max_output_pct/state",
          0.0f, 100.0f, 1.0f, "%");
pubNumber("legionella_output_pct", "Legionella output limiet", base + "/limits/legionella_output_pct/set", base + "/limits/legionella_output_pct/state",
          0.0f, 100.0f, 1.0f, "%");

// --------- 6) NORMAL instellingen (numbers) ---------
pubNumber("target_export_w", "Doel netvermogen", base + "/normal/target_export_w/set", base + "/normal/target_export_w/state",
          -5000.0f, 5000.0f, 1.0f, "W");
pubNumber("max_boiler_power_w", "Max boilervemogen", base + "/normal/max_boiler_power_w/set", base + "/normal/max_boiler_power_w/state",
          100.0f, 10000.0f, 10.0f, "W");
pubNumber("min_step_percent", "Min stap", base + "/normal/min_step_percent/set", base + "/normal/min_step_percent/state",
          0.1f, 10.0f, 0.1f, "%");
pubNumber("min_interval_s", "Min interval", base + "/normal/min_interval_s/set", base + "/normal/min_interval_s/state",
          0.0f, 60.0f, 1.0f, "s");

// --------- 7) Legionella planning (select/switch/numbers) ---------
{
  // DOW select (Zondag..Zaterdag)
  StaticJsonDocument<640> d;
  d["name"] = String(DEVICE_NAME) + " Legionella dag";
  d["unique_id"] = haUniqueId("leg_dow");
  d["command_topic"] = base + "/legionella/dow/set";
  d["state_topic"]   = base + "/legionella/dow/state";
  JsonArray opts = d.createNestedArray("options");
  opts.add("Zondag");
  opts.add("Maandag");
  opts.add("Dinsdag");
  opts.add("Woensdag");
  opts.add("Donderdag");
  opts.add("Vrijdag");
  opts.add("Zaterdag");
  d["entity_category"] = "config";
  haAddAvailability(d);
  haAddDevice(d);
  mqttPublishHADiscoveryDoc("select", "leg_dow", d);
}

pubNumber("leg_start_hour",   "Legionella start uur",    base + "/legionella/start_hour/set",   base + "/legionella/start_hour/state",
          0.0f, 23.0f, 1.0f, "h");
pubNumber("leg_start_minute", "Legionella start minuut", base + "/legionella/start_minute/set", base + "/legionella/start_minute/state",
          0.0f, 59.0f, 1.0f, "min");
pubNumber("leg_duration_min", "Legionella duur",         base + "/legionella/duration_min/set", base + "/legionella/duration_min/state",
          1.0f, 360.0f, 1.0f, "min");

{
  // Use temp sensor switch
  StaticJsonDocument<512> d;
  d["name"] = String(DEVICE_NAME) + " Legionella temp sensor";
  d["unique_id"] = haUniqueId("leg_use_temp_sensor");
  d["command_topic"] = base + "/legionella/use_temp_sensor/set";
  d["state_topic"]   = base + "/legionella/use_temp_sensor/state";
  d["payload_on"] = "ON";
  d["payload_off"] = "OFF";
  d["entity_category"] = "config";
  haAddAvailability(d);
  haAddDevice(d);
  mqttPublishHADiscoveryDoc("switch", "leg_use_temp_sensor", d);
}

pubNumber("leg_target_temp_c", "Legionella doeltemp", base + "/legionella/target_temp_c/set", base + "/legionella/target_temp_c/state",
          40.0f, 75.0f, 0.1f, "°C");
pubNumber("leg_hold_min",      "Legionella hold",     base + "/legionella/hold_min/set",      base + "/legionella/hold_min/state",
          0.0f, 120.0f, 1.0f, "min");


  // --------- Energy strategy (Settings -> Energy strategy) ---------
  // Home Assistant MQTT Discovery entities for the Energy strategy section.
  // We base most "state" values on the main status JSON so HA always reflects the UI/logic state.
  {
    // Select: schema / mode shown as friendly label (ALWAYS/HANDMATIG/CHEAPEST DAYLIGHT/UIT)
    StaticJsonDocument<768> d;
    d["name"] = String(DEVICE_NAME) + " Zonregeling schema (NORMAL)";
    d["unique_id"] = haUniqueId("sun_sched_mode");
    d["command_topic"] = base + "/sun/sched_mode/set";
    d["state_topic"]   = base + "/status";
    d["value_template"] = "{{ value_json.sun_sched_label }}";
    JsonArray opts = d.createNestedArray("options");
    opts.add("ALWAYS");
    opts.add("HANDMATIG");
    opts.add("CHEAPEST DAYLIGHT");
    opts.add("UIT");
    // Map label -> numeric mode expected by the firmware
    d["command_template"] =
      "{% if value == 'ALWAYS' %}0"
      "{% elif value == 'HANDMATIG' %}1"
      "{% elif value == 'CHEAPEST DAYLIGHT' %}2"
      "{% else %}3{% endif %}";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("select", "sun_sched_mode", d);
  }

  {
    // Switch: show label on dashboard
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " Energy strategy - Label Daylight/Cheapest (Dashboard)";
    d["unique_id"] = haUniqueId("dash_sun_label");
    d["command_topic"] = base + "/sun/dashboard_label/set";
    d["state_topic"]   = base + "/status";
    d["value_template"] = "{{ 'ON' if value_json.dash_sun_label else 'OFF' }}";
    d["payload_on"] = "ON";
    d["payload_off"] = "OFF";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("switch", "dash_sun_label", d);
  }

  {
    // Text: manual start time (HH:MM)
    StaticJsonDocument<768> d;
    d["name"] = String(DEVICE_NAME) + " Handmatig start";
    d["unique_id"] = haUniqueId("sun_manual_start");
    d["command_topic"] = base + "/sun/manual_start/set";
    d["state_topic"]   = base + "/status";
    d["value_template"] =
      "{{ '%02d:%02d'|format((value_json.sun_manual_start_min//60)|int,(value_json.sun_manual_start_min%60)|int) }}";
    d["pattern"] = "^([01]\\d|2[0-3]):[0-5]\\d$";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("text", "sun_manual_start", d);
  }

  {
    // Text: manual end time (HH:MM)
    StaticJsonDocument<768> d;
    d["name"] = String(DEVICE_NAME) + " Handmatig einde";
    d["unique_id"] = haUniqueId("sun_manual_end");
    d["command_topic"] = base + "/sun/manual_end/set";
    d["state_topic"]   = base + "/status";
    d["value_template"] =
      "{{ '%02d:%02d'|format((value_json.sun_manual_end_min//60)|int,(value_json.sun_manual_end_min%60)|int) }}";
    d["pattern"] = "^([01]\\d|2[0-3]):[0-5]\\d$";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("text", "sun_manual_end", d);
  }

  // Numbers: blocks / margin / location
  pubNumber("sun_cheapest_blocks", "Energy strategy - Aantal blokken", base + "/sun/cheapest_blocks/set", base + "/sun/cheapest_blocks/state",
            1.0f, 48.0f, 1.0f, "blokken");
  pubNumber("sun_daylight_margin_min", "Energy strategy - Marge (min)", base + "/sun/daylight_margin_min/set", base + "/sun/daylight_margin_min/state",
            0.0f, 180.0f, 1.0f, "min");
  pubNumber("sun_lat", "Energy strategy - Latitude", base + "/sun/lat/set", base + "/sun/lat/state",
            -90.0f, 90.0f, 0.0001f, "°");
  pubNumber("sun_lon", "Energy strategy - Longitude", base + "/sun/lon/set", base + "/sun/lon/state",
            -180.0f, 180.0f, 0.0001f, "°");

  // --------- Gas vs Elektra advies + slimme ELEC-blokken ---------
  {
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " Advies Gas vs Elektra";
    d["unique_id"] = haUniqueId("heat_auto");
    d["command_topic"] = base + "/heat/auto/set";
    d["state_topic"]   = base + "/heat/auto/state";
    d["payload_on"] = "ON";
    d["payload_off"] = "OFF";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("switch", "heat_auto", d);
  }

  {
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " Voer ELEC-advies uit";
    d["unique_id"] = haUniqueId("heat_exec_el");
    d["command_topic"] = base + "/heat/exec_el/set";
    d["state_topic"]   = base + "/heat/exec_el/state";
    d["payload_on"] = "ON";
    d["payload_off"] = "OFF";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("switch", "heat_exec_el", d);
  }

  {
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " Alleen goedkoopste ELEC-blokken";
    d["unique_id"] = haUniqueId("heat_cheap_blocks_enabled");
    d["command_topic"] = base + "/heat/cheap_blocks/enabled/set";
    d["state_topic"]   = base + "/heat/cheap_blocks/enabled/state";
    d["payload_on"] = "ON";
    d["payload_off"] = "OFF";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("switch", "heat_cheap_blocks_enabled", d);
  }

  {
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " Boiler-doeltemp bij ELEC-blokken";
    d["unique_id"] = haUniqueId("heat_cheap_use_boiler_target");
    d["command_topic"] = base + "/heat/cheap_blocks/use_boiler_target/set";
    d["state_topic"]   = base + "/heat/cheap_blocks/use_boiler_target/state";
    d["payload_on"] = "ON";
    d["payload_off"] = "OFF";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("switch", "heat_cheap_use_boiler_target", d);
  }

  pubNumber("heat_cheap_blocks_max", "Max. goedkoopste aaneengesloten ELEC-blokken",
            base + "/heat/cheap_blocks/max/set", base + "/heat/cheap_blocks/max/state",
            1.0f, 96.0f, 1.0f, "blokken");

  // --------- Nordpool settings (Settings -> Nordpool prijzen) ---------
  // --------- Elektra prijsbron (vast/dynamisch) ---------
  {
    StaticJsonDocument<768> d;
    d["name"] = String(DEVICE_NAME) + " Elektra prijsbron";
    d["unique_id"] = haUniqueId("elec_price_source");
    d["command_topic"] = base + "/elec/src/set";
    d["state_topic"]   = base + "/elec/src/state";
    d["value_template"] = "{% if value == '1' %}Vast (all-in){% else %}Dynamisch{% endif %}";
    JsonArray opts = d.createNestedArray("options");
    opts.add("Dynamisch");
    opts.add("Vast (all-in)");
    d["command_template"] = "{% if value == 'Vast (all-in)' %}1{% else %}0{% endif %}";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("select", "elec_price_source", d);
  }

  pubNumber("elec_fixed_ct", "Vaste stroomprijs (all-in)",
            base + "/elec/fixed_ct/set", base + "/elec/fixed_ct/state",
            0.0f, 200.0f, 0.001f, "ct/kWh");

  // Let op: we publiceren retained state topics via mqttPublishNordpoolStates()
  {
    // Switch: Nordpool inschakelen
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " Nordpool - Inschakelen";
    d["unique_id"] = haUniqueId("np_enabled");
    d["command_topic"] = base + "/np/enabled/set";
    d["state_topic"]   = base + "/np/enabled/state";
    d["payload_on"] = "ON";
    d["payload_off"]= "OFF";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("switch", "np_enabled", d);
  }
  {
    // Text: delivery area
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " Nordpool - Gebied";
    d["unique_id"] = haUniqueId("np_area");
    d["command_topic"] = base + "/np/area/set";
    d["state_topic"]   = base + "/np/area/state";
    d["mode"] = "text";
    d["max"] = 16;
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("text", "np_area", d);
  }
  {
    // Select: price resolution (minutes)
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " Nordpool - Prijsresolutie";
    d["unique_id"] = haUniqueId("np_res_min");
    d["command_topic"] = base + "/np/res_min/set";
    d["state_topic"]   = base + "/np/res_min/state";
    JsonArray opts = d.createNestedArray("options");
    opts.add("15");
    opts.add("60");
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("select", "np_res_min", d);
  }
  {
    // Switch: include taxes
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " Nordpool - Inclusief belastingen";
    d["unique_id"] = haUniqueId("np_incl_taxes");
    d["command_topic"] = base + "/np/incl_taxes/set";
    d["state_topic"]   = base + "/np/incl_taxes/state";
    d["payload_on"] = "ON";
    d["payload_off"]= "OFF";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("switch", "np_incl_taxes", d);
  }

  // Numbers: taxes/costs
  pubNumber("np_energy_tax_ct", "Nordpool - Energiebelasting (ct/kWh)",
            base + "/np/energy_tax_ct/set", base + "/np/energy_tax_ct/state",
            0.0f, 50.0f, 0.0001f, "ct/kWh");

  pubNumber("np_inkoop_ct", "Nordpool - Inkoopkosten (ct/kWh)",
            base + "/np/inkoop_ct/set", base + "/np/inkoop_ct/state",
            0.0f, 50.0f, 0.0001f, "ct/kWh");

  pubNumber("np_btw_pct", "Nordpool - BTW (%)",
            base + "/np/btw_pct/set", base + "/np/btw_pct/state",
            0.0f, 30.0f, 0.1f, "%");

  // Price-boost settings
  {
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " Nordpool - Prijs-boost (PWM MAX)";
    d["unique_id"] = haUniqueId("np_boost_enabled");
    d["command_topic"] = base + "/np/boost_enabled/set";
    d["state_topic"]   = base + "/np/boost_enabled/state";
    d["payload_on"] = "ON";
    d["payload_off"]= "OFF";
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("switch", "np_boost_enabled", d);
  }
  {
    StaticJsonDocument<640> d;
    d["name"] = String(DEVICE_NAME) + " Nordpool - Prijsbron boost";
    d["unique_id"] = haUniqueId("np_boost_price_source");
    d["command_topic"] = base + "/np/boost_price_source/set";
    d["state_topic"]   = base + "/np/boost_price_source/state";
    JsonArray opts = d.createNestedArray("options");
    opts.add("total");
    opts.add("spot");
    d["entity_category"] = "config";
    haAddAvailability(d);
    haAddDevice(d);
    mqttPublishHADiscoveryDoc("select", "np_boost_price_source", d);
  }

  pubNumber("np_boost_threshold_ct", "Nordpool - Boost drempel (ct/kWh)",
            base + "/np/boost_threshold_ct/set", base + "/np/boost_threshold_ct/state",
            -50.0f, 200.0f, 0.0001f, "ct/kWh");

  pubNumber("np_boost_n_blocks", "Nordpool - Aantal boost-blokken vandaag (N)",
            base + "/np/boost_n_blocks/set", base + "/np/boost_n_blocks/state",
            0.0f, 96.0f, 1.0f, "blokken");


}

