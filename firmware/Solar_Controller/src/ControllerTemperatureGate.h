#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>

#include "MultiController.h"

// Temperatuurvrijgave per controller.
//
// Uitgangspunten:
// - controller 1 blijft de vaste lokale uitgang op GPIO25
// - simpele vrijgave/blokkering op basis van temperatuur + hysterese
// - lokaal beoordeeld waar de sensor zit
// - master/slave/standalone kunnen dezelfde config- en runtimebasis delen

static constexpr int8_t CTG_SENSOR_INDEX_NONE = -1;
static constexpr uint8_t CTG_STATE_RELEASED = 0;
static constexpr uint8_t CTG_STATE_BLOCKED_HOT = 1;
static constexpr uint8_t CTG_STATE_BLOCKED_SENSOR = 2;

struct ControllerTemperatureGateConfig {
  bool enabled;
  String sensorAddr;   // DS18B20 address string (robuuster dan losse index)
  float stopTempC;
  float startTempC;
};

struct ControllerTemperatureGateRuntime {
  bool initialized;
  bool released;
  uint8_t state;
  float sensorTempC;
  bool sensorValid;
  String sensorAddr;
  String sensorName;
  String reason;
};

extern ControllerTemperatureGateConfig ctgConfigs[MC_MAX_SLOTS];
extern ControllerTemperatureGateRuntime ctgRuntime[MC_MAX_SLOTS];

void controllerTemperatureGateSetDefaults(ControllerTemperatureGateConfig& cfg);
void controllerTemperatureGateSetAllDefaults();
void controllerTemperatureGateClamp(ControllerTemperatureGateConfig& cfg);
void controllerTemperatureGateClampAll();
void controllerTemperatureGateLoadConfig(Preferences& prefs);
void controllerTemperatureGateSaveConfig(Preferences& prefs);
void controllerTemperatureGateApplyControlSettings(WebServer& server);
void controllerTemperatureGateRefresh();
String controllerTemperatureGateFeatureName();
String controllerTemperatureGateBuildSummary(uint8_t slotIndex);
bool controllerTemperatureGateControllerUsesFixedGpio25(uint8_t role, uint8_t controllerIndex);
bool controllerTemperatureGateUsesLocalSensorConfig(uint8_t role, uint8_t controllerIndex);
inline bool controllerTemperatureGateAppliesToScenario(uint8_t scenario) {
  return scenario == MC_SCENARIO_SURPLUS;
}

const ControllerTemperatureGateConfig& controllerTemperatureGateGetConfig(uint8_t slotIndex);
const ControllerTemperatureGateRuntime& controllerTemperatureGateGetRuntime(uint8_t slotIndex);
bool controllerTemperatureGateIsEnabled(uint8_t slotIndex);
bool controllerTemperatureGateIsReleased(uint8_t slotIndex);
String controllerTemperatureGateGetBlockReason(uint8_t slotIndex);
String controllerTemperatureGateGetSensorName(uint8_t slotIndex);
String controllerTemperatureGateGetSensorAddr(uint8_t slotIndex);
float controllerTemperatureGateGetSensorTempC(uint8_t slotIndex);

// Geeft de aan een lokale controller toegewezen DS18B20 terug, ook als
// "Rekening houden met temperatuur" uit staat. Dit is bewust losgekoppeld
// van cfg.enabled zodat andere functies (zoals slim AUTO_ELEC) de bestaande
// sensor en temperatuurgrenzen kunnen hergebruiken zonder de NORMAL-gate aan te zetten.
// Of de stoptemperatuur werkelijk mag begrenzen blijft een aparte keuze van de aanroepende functie.
bool controllerTemperatureGateGetAssignedSensorReading(uint8_t slotIndex,
                                                        float& tempC,
                                                        float& startTempC,
                                                        float& stopTempC,
                                                        String* sensorName = nullptr);
