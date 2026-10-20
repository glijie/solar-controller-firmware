#pragma once

#include <Arduino.h>

struct PhaseBudgetSnapshot {
  bool meterSupportsPhases;
  bool readOk;
  bool phasesOk;
  float i1A;
  float i2A;
  float i3A;
  unsigned long updatedMs;
};

bool phaseBudgetMeterSupportsPhases();
bool phaseBudgetRefreshSnapshot();
bool phaseBudgetSnapshotFresh(uint32_t maxAgeMs);
const PhaseBudgetSnapshot& phaseBudgetGetSnapshot();
float phaseBudgetSelectedPhaseCurrentA(uint8_t phase123);
float phaseBudgetEstimateStandaloneAllowedPercent(uint8_t phase123, float currentEffectivePercent, float capMaxPercent);
