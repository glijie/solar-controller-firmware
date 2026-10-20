#pragma once

#include <Arduino.h>

void handleApiPing();
void handleApiRelayPost();
void handleApiRelayOnPost();
void handleApiRelayOffPost();
void handleApiConfigGet();
void handleApiConfigPost();
void handleApiForceHeatPost();
void handleApiLegionellaRunNowPost();
void handleApiLegionellaCancelPost();
void handleApiMultiControllerTargetsPost();
void handleApiRebootPost();
void handleFwSetAuto();
