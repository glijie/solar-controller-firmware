#pragma once

#include <Arduino.h>

void applyTuningFromJson(const String& json);
void setTuningAlgorithm(const String& payload);
void setTuningStepFactor(const String& payload);
void setTuningDeadband(const String& payload);
void setTuningMaxStep(const String& payload);
