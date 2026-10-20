#pragma once

#include <Arduino.h>

void mqttPublishAvailability(const char* s);
void mqttPublishForceState(bool forceNow);
void mqttPublishRelayState(bool forceNow);
void mqttPublishMultiControllerStates(bool forceNow);
void mqttPublishCoreLiveStates(bool forceNow);
void mqttPublishTuningStates(bool forceNow);
void mqttPublishLimitsStates(bool forceNow);
void mqttPublishNormalConfigStates(bool forceNow);
void mqttPublishSunStrategyStates(bool forceNow);
void mqttPublishElecPriceStates(bool forceNow);
void mqttPublishNordpoolStates(bool forceNow);
void mqttPublishHeatCheapConfigStates(bool forceNow);
void mqttPublishLegionellaPlanStates(bool forceNow);
void mqttPublishLegionellaState(bool forceNow);
void mqttPublishLegionellaConfig(bool forceNow);
void mqttPublishStatusJson(bool forceNow);
