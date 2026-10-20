#pragma once
#include <Arduino.h>

float getPhaseCurrentA(uint8_t phase123);
float getConfiguredPhaseGuardMaxA(uint8_t phase123);
void refreshLegacyPhaseGuardMaxA();
void updatePhaseGuardDecision();
