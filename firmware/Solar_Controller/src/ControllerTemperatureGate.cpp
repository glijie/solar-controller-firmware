#include "ControllerTemperatureGate.h"

#include <math.h>
#include <stdio.h>

#include "app_globals.h"
#include "sensors.h"

ControllerTemperatureGateConfig ctgConfigs[MC_MAX_SLOTS];
ControllerTemperatureGateRuntime ctgRuntime[MC_MAX_SLOTS];

namespace {
static float ctgClampFloat(float v, float lo, float hi) {
  if (!isfinite(v)) return lo;
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static bool ctgReadBool(WebServer& server, const String& key, bool currentValue) {
  if (!server.hasArg(key)) return currentValue;
  String v = server.arg(key);
  v.trim();
  v.toLowerCase();
  return (v == "1" || v == "true" || v == "on" || v == "yes");
}

static String ctgReadString(WebServer& server, const String& key, const String& currentValue) {
  if (!server.hasArg(key)) return currentValue;
  String v = server.arg(key);
  v.trim();
  v.toLowerCase();
  return v;
}

static float ctgReadFloat(WebServer& server, const String& key, float currentValue) {
  if (!server.hasArg(key)) return currentValue;
  return server.arg(key).toFloat();
}

static void ctgResetRuntimeSlot(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return;
  ControllerTemperatureGateRuntime& rt = ctgRuntime[slotIndex];
  rt.initialized = false;
  rt.released = true;
  rt.state = CTG_STATE_RELEASED;
  rt.sensorTempC = NAN;
  rt.sensorValid = false;
  rt.sensorAddr = "";
  rt.sensorName = "";
  rt.reason = "Temperatuurvrijgave uit.";
}

static int ctgFindSensorByAddr(const String& addr) {
  if (addr.length() != 16) return -1;
  for (int i = 0; i < tempSensorCount; i++) {
    if (tempSensors[i].addrStr == addr) return i;
  }
  return -1;
}

static String ctgDefaultSlotName(uint8_t slotIndex) {
  return String("Controller ") + String(slotIndex + 1);
}

static uint8_t ctgVisibleControllerCount() {
  if (mcEnabled && mcRole == MC_ROLE_MASTER) {
    if (mcControllerCount < 1) return 1;
    if (mcControllerCount > MC_MAX_SLOTS) return MC_MAX_SLOTS;
    return mcControllerCount;
  }
  return 1;
}
}

void controllerTemperatureGateSetDefaults(ControllerTemperatureGateConfig& cfg) {
  cfg.enabled = false;
  cfg.sensorAddr = "";
  cfg.stopTempC = 60.0f;
  cfg.startTempC = 55.0f;
}

void controllerTemperatureGateSetAllDefaults() {
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    controllerTemperatureGateSetDefaults(ctgConfigs[i]);
    ctgResetRuntimeSlot(i);
  }
}

void controllerTemperatureGateClamp(ControllerTemperatureGateConfig& cfg) {
  cfg.sensorAddr.trim();
  cfg.sensorAddr.toLowerCase();
  if (cfg.sensorAddr.length() != 16) cfg.sensorAddr = "";
  cfg.stopTempC = ctgClampFloat(cfg.stopTempC, 0.0f, 100.0f);
  cfg.startTempC = ctgClampFloat(cfg.startTempC, 0.0f, 100.0f);
  if (cfg.startTempC > cfg.stopTempC) cfg.startTempC = cfg.stopTempC;
}

void controllerTemperatureGateClampAll() {
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) controllerTemperatureGateClamp(ctgConfigs[i]);
}

void controllerTemperatureGateLoadConfig(Preferences& prefs) {
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    char key[20];
    ControllerTemperatureGateConfig& cfg = ctgConfigs[i];
    controllerTemperatureGateSetDefaults(cfg);

    snprintf(key, sizeof(key), "ctg%u_en", (unsigned)(i + 1));
    cfg.enabled = prefs.getBool(key, cfg.enabled);

    snprintf(key, sizeof(key), "ctg%u_addr", (unsigned)(i + 1));
    cfg.sensorAddr = prefs.getString(key, cfg.sensorAddr);

    snprintf(key, sizeof(key), "ctg%u_stop", (unsigned)(i + 1));
    cfg.stopTempC = prefs.getFloat(key, cfg.stopTempC);

    snprintf(key, sizeof(key), "ctg%u_start", (unsigned)(i + 1));
    cfg.startTempC = prefs.getFloat(key, cfg.startTempC);

    controllerTemperatureGateClamp(cfg);
    ctgResetRuntimeSlot(i);
  }
}

void controllerTemperatureGateSaveConfig(Preferences& prefs) {
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    char key[20];
    const ControllerTemperatureGateConfig& cfg = ctgConfigs[i];

    snprintf(key, sizeof(key), "ctg%u_en", (unsigned)(i + 1));
    prefs.putBool(key, cfg.enabled);

    snprintf(key, sizeof(key), "ctg%u_addr", (unsigned)(i + 1));
    prefs.putString(key, cfg.sensorAddr);

    snprintf(key, sizeof(key), "ctg%u_stop", (unsigned)(i + 1));
    prefs.putFloat(key, cfg.stopTempC);

    snprintf(key, sizeof(key), "ctg%u_start", (unsigned)(i + 1));
    prefs.putFloat(key, cfg.startTempC);
  }
}

void controllerTemperatureGateApplyControlSettings(WebServer& server) {
  const uint8_t visibleCount = ctgVisibleControllerCount();
  for (uint8_t i = 0; i < visibleCount; i++) {
    if (!controllerTemperatureGateUsesLocalSensorConfig(mcRole, i)) continue;

    ControllerTemperatureGateConfig& cfg = ctgConfigs[i];
    const String prefix = String("ctg_s") + String(i + 1) + String("_");

    if (server.hasArg(prefix + "en")) {
      cfg.enabled = ctgReadBool(server, prefix + "en", cfg.enabled);
    } else {
      cfg.enabled = false;
    }

    cfg.sensorAddr = ctgReadString(server, prefix + "addr", cfg.sensorAddr);
    cfg.stopTempC = ctgReadFloat(server, prefix + "stop", cfg.stopTempC);
    cfg.startTempC = ctgReadFloat(server, prefix + "start", cfg.startTempC);
    controllerTemperatureGateClamp(cfg);
    ctgRuntime[i].initialized = false;
  }
}

void controllerTemperatureGateRefresh() {
  updateTempIfNeeded();

  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    const ControllerTemperatureGateConfig& cfg = ctgConfigs[i];
    ControllerTemperatureGateRuntime& rt = ctgRuntime[i];

    rt.sensorTempC = NAN;
    rt.sensorValid = false;
    rt.sensorAddr = cfg.sensorAddr;
    rt.sensorName = "";

    if (!controllerTemperatureGateUsesLocalSensorConfig(mcRole, i)) {
      rt.initialized = true;
      rt.released = true;
      rt.state = CTG_STATE_RELEASED;
      rt.sensorAddr = "";
      rt.sensorName = "";
      rt.reason = "Temperatuurvrijgave van deze netwerkcontroller wordt lokaal op de slave beoordeeld.";
      continue;
    }

    if (!cfg.enabled) {
      rt.initialized = true;
      rt.released = true;
      rt.state = CTG_STATE_RELEASED;
      rt.reason = "Temperatuurvrijgave uit.";
      continue;
    }

    const int sensorIndex = ctgFindSensorByAddr(cfg.sensorAddr);
    if (sensorIndex < 0) {
      rt.initialized = true;
      rt.released = false;
      rt.state = CTG_STATE_BLOCKED_SENSOR;
      rt.reason = (cfg.sensorAddr.length() == 16)
        ? "Gekozen temperatuursensor is niet gevonden."
        : "Geen temperatuursensor gekozen.";
      continue;
    }

    TempSensorEntry& s = tempSensors[sensorIndex];
    rt.sensorAddr = s.addrStr;
    rt.sensorName = s.name.length() ? s.name : (String("DS18B20 ") + String(sensorIndex + 1));
    rt.sensorTempC = s.tempC;
    rt.sensorValid = s.present && tempIsValid(s.tempC);

    if (!rt.sensorValid) {
      rt.initialized = true;
      rt.released = false;
      rt.state = CTG_STATE_BLOCKED_SENSOR;
      rt.reason = String("Sensor ") + rt.sensorName + String(" is niet beschikbaar of geeft geen geldige waarde.");
      continue;
    }

    if (!rt.initialized) {
      rt.released = (s.tempC < cfg.stopTempC);
      rt.initialized = true;
    } else if (rt.released) {
      if (s.tempC >= cfg.stopTempC) rt.released = false;
    } else {
      if (s.tempC <= cfg.startTempC) rt.released = true;
    }

    if (rt.released) {
      rt.state = CTG_STATE_RELEASED;
      rt.reason = String("Vrijgegeven op temperatuur") + (rt.sensorName.length() ? String(" (") + rt.sensorName + String(")") : String(""));
      if (rt.sensorValid) rt.reason += String(": ") + String(rt.sensorTempC, 1) + String("°C.");
    } else {
      rt.state = CTG_STATE_BLOCKED_HOT;
      rt.reason = String("Geblokkeerd door temperatuur") + (rt.sensorName.length() ? String(" (") + rt.sensorName + String(")") : String(""));
      rt.reason += String(": ") + String(rt.sensorTempC, 1) + String("°C, stop ") + String(cfg.stopTempC, 1) + String("°C, start ") + String(cfg.startTempC, 1) + String("°C.");
    }
  }
}

String controllerTemperatureGateFeatureName() {
  return String("Temperatuurvrijgave controller");
}

String controllerTemperatureGateBuildSummary(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return "-";
  const ControllerTemperatureGateConfig& cfg = ctgConfigs[slotIndex];
  const ControllerTemperatureGateRuntime& rt = ctgRuntime[slotIndex];
  if (!controllerTemperatureGateUsesLocalSensorConfig(mcRole, slotIndex)) return String("Lokaal op slave");
  if (!cfg.enabled) return String("Uit");
  String out = String("Aan");
  if (rt.sensorName.length()) out += String(" – ") + rt.sensorName;
  else if (cfg.sensorAddr.length() == 16) out += String(" – sensor gekozen");
  else out += String(" – sensor niet gekozen");
  out += String(", stop ") + String(cfg.stopTempC, 1) + String("°C, start ") + String(cfg.startTempC, 1) + String("°C");
  return out;
}

bool controllerTemperatureGateControllerUsesFixedGpio25(uint8_t role, uint8_t controllerIndex) {
  if (controllerIndex != 0) return false;
  return (role == MC_ROLE_STANDALONE || role == MC_ROLE_MASTER || role == MC_ROLE_SLAVE);
}

bool controllerTemperatureGateUsesLocalSensorConfig(uint8_t role, uint8_t controllerIndex) {
  if (controllerIndex >= MC_MAX_SLOTS) return false;
  if (role == MC_ROLE_STANDALONE || role == MC_ROLE_SLAVE) return (controllerIndex == 0);
  if (role == MC_ROLE_MASTER) {
    return (mcSlots[controllerIndex].slotType == MC_SLOT_LOCAL_PWM);
  }
  return false;
}

const ControllerTemperatureGateConfig& controllerTemperatureGateGetConfig(uint8_t slotIndex) {
  static ControllerTemperatureGateConfig dummy{};
  if (slotIndex >= MC_MAX_SLOTS) return dummy;
  return ctgConfigs[slotIndex];
}

const ControllerTemperatureGateRuntime& controllerTemperatureGateGetRuntime(uint8_t slotIndex) {
  static ControllerTemperatureGateRuntime dummy{};
  if (slotIndex >= MC_MAX_SLOTS) return dummy;
  return ctgRuntime[slotIndex];
}

bool controllerTemperatureGateIsEnabled(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return false;
  if (!controllerTemperatureGateUsesLocalSensorConfig(mcRole, slotIndex)) return false;
  return ctgConfigs[slotIndex].enabled;
}

bool controllerTemperatureGateIsReleased(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return true;
  return ctgRuntime[slotIndex].released;
}

String controllerTemperatureGateGetBlockReason(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return "";
  return ctgRuntime[slotIndex].reason;
}

String controllerTemperatureGateGetSensorName(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return "";
  return ctgRuntime[slotIndex].sensorName;
}

String controllerTemperatureGateGetSensorAddr(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return "";
  if (!controllerTemperatureGateUsesLocalSensorConfig(mcRole, slotIndex)) return "";
  return ctgConfigs[slotIndex].sensorAddr;
}

float controllerTemperatureGateGetSensorTempC(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return NAN;
  return ctgRuntime[slotIndex].sensorTempC;
}


bool controllerTemperatureGateGetAssignedSensorReading(uint8_t slotIndex,
                                                        float& tempC,
                                                        float& startTempC,
                                                        float& stopTempC,
                                                        String* sensorName) {
  tempC = NAN;
  startTempC = NAN;
  stopTempC = NAN;
  if (sensorName) *sensorName = "";

  if (slotIndex >= MC_MAX_SLOTS) return false;
  if (!controllerTemperatureGateUsesLocalSensorConfig(mcRole, slotIndex)) return false;

  const ControllerTemperatureGateConfig& cfg = ctgConfigs[slotIndex];
  startTempC = cfg.startTempC;
  stopTempC = cfg.stopTempC;

  // Sensorselectie blijft in de config bewaard wanneer de temperatuur-toggle uit staat.
  // Voor deze uitleesfunctie is alleen een werkelijk toegewezen + geldige sensor nodig.
  if (cfg.sensorAddr.length() != 16) return false;

  updateTempIfNeeded();
  const int sensorIndex = ctgFindSensorByAddr(cfg.sensorAddr);
  if (sensorIndex < 0) return false;

  TempSensorEntry& s = tempSensors[sensorIndex];
  if (sensorName) {
    *sensorName = s.name.length() ? s.name : (String("DS18B20 ") + String(sensorIndex + 1));
  }
  if (!s.present || !tempIsValid(s.tempC)) return false;

  tempC = s.tempC;
  return true;
}
