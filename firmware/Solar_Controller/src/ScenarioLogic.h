#pragma once

#include <Arduino.h>
#include "app_globals.h"

enum PriorityModeNow : uint8_t {
  PMODE_NONE = 0,
  PMODE_LEGIONELLA = 1,
  PMODE_FORCE = 2,
  PMODE_BOOST = 3,
  PMODE_AUTO_ELEC = 4
};

uint8_t currentPriorityModeNow();
bool priorityHeatingActiveNow();
uint8_t multiControllerCurrentAppScenario();

void applyNormalDirect(long netPowerW);
void applyNormalStep(long netPowerW);
void applyNormalMode(long netPowerW);
void applyForceHeatMode();
void applyPriceBoostMode();

void heatCompareUpdateNow();
const char* heatRecommendationLabelNow();
bool heatExecElecActiveNow();
void heatCheapPlanInvalidate();
bool heatCheapTemperatureSensorAssignedNow();
bool heatCheapTemperatureModeEnabledNow();
bool heatCheapTemperatureSensorValidNow();
bool heatCheapTemperatureDemandActiveNow();
float heatCheapTemperatureCurrentCNow();
float heatCheapTemperatureStartCNow();
float heatCheapTemperatureStopCNow();
String heatCheapTemperatureSensorNameNow();
