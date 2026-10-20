#include "MqttTuning.h"

#include <ArduinoJson.h>

#include "app_globals.h"
#include "nordpool.h"

void applyTuningFromJson(const String& json) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return;

  if (doc.containsKey("algorithm")) {
    uint8_t a;
    if (parseAlgorithm(doc["algorithm"].as<String>(), a)) normalAlg = a;
  }
  if (doc.containsKey("step_factor")) stepFactor = doc["step_factor"].as<float>();
  if (doc.containsKey("deadband_w"))  deadbandW  = doc["deadband_w"].as<float>();
  if (doc.containsKey("max_step_pct")) maxStepPctPerUpdatePerUpdate = doc["max_step_pct"].as<float>();

  clampAll();
  // Prijs-boost plan meteen updaten op basis van huidige 'vandaag' buffer.
  npRecomputeBoostPlanToday();
  saveConfig();
  mqttPublishTuningStates();
  mqttPublishCoreLiveStates(true);
  mqttPublishStatusJson(true);
}

void setTuningAlgorithm(const String& payload) {
  uint8_t a;
  if (!parseAlgorithm(payload, a)) return;
  normalAlg = a;
  clampAll();
  saveConfig();
  mqttPublishTuningStates();
  mqttPublishCoreLiveStates(true);
  mqttPublishStatusJson(true);
}

void setTuningStepFactor(const String& payload) {
  stepFactor = payload.toFloat();
  clampAll();
  saveConfig();
  mqttPublishTuningStates();
  mqttPublishCoreLiveStates(true);
  mqttPublishStatusJson(true);
}

void setTuningDeadband(const String& payload) {
  deadbandW = payload.toFloat();
  clampAll();
  saveConfig();
  mqttPublishTuningStates();
  mqttPublishCoreLiveStates(true);
  mqttPublishStatusJson(true);
}

void setTuningMaxStep(const String& payload) {
  maxStepPctPerUpdatePerUpdate = payload.toFloat();
  clampAll();
  saveConfig();
  mqttPublishTuningStates();
  mqttPublishCoreLiveStates(true);
  mqttPublishStatusJson(true);
}
