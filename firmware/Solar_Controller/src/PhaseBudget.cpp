#include "PhaseBudget.h"

#include <math.h>

#include "app_globals.h"
#include "PhaseGuard.h"

namespace {

PhaseBudgetSnapshot gPhaseBudgetSnapshot = {
  false,
  false,
  false,
  NAN,
  NAN,
  NAN,
  0UL,
};

bool isP1MeterType() {
  return (meterType == "youless" || meterType == "homewizard" || meterType == "smartgateways");
}

void phaseBudgetSyncSnapshotFromGlobals(bool readOkNow) {
  gPhaseBudgetSnapshot.meterSupportsPhases = isP1MeterType();
  gPhaseBudgetSnapshot.readOk = readOkNow;
  gPhaseBudgetSnapshot.phasesOk = lastPhasesOk;
  gPhaseBudgetSnapshot.i1A = lastPhaseI1A;
  gPhaseBudgetSnapshot.i2A = lastPhaseI2A;
  gPhaseBudgetSnapshot.i3A = lastPhaseI3A;
  gPhaseBudgetSnapshot.updatedMs = lastPhasesUpdateMs;
}

} // namespace

bool phaseBudgetMeterSupportsPhases() {
  return isP1MeterType();
}

bool phaseBudgetRefreshSnapshot() {
  const bool meterSupportsPhases = isP1MeterType();
  bool readOkNow = false;

  if (meterSupportsPhases) {
    float i1 = NAN, i2 = NAN, i3 = NAN;
    readOkNow = readMeterPhases(i1, i2, i3);
    if (readOkNow) {
      lastPhaseI1A = i1;
      lastPhaseI2A = i2;
      lastPhaseI3A = i3;
      lastPhasesUpdateMs = millis();
    }

    const unsigned long age = (lastPhasesUpdateMs == 0UL) ? 0xFFFFFFFFUL : (millis() - lastPhasesUpdateMs);
    lastPhasesOk = (age <= PHASES_STALE_MS);
  } else {
    lastPhasesOk = false;
  }

  updatePhaseGuardDecision();
  phaseBudgetSyncSnapshotFromGlobals(readOkNow);
  return readOkNow;
}

bool phaseBudgetSnapshotFresh(uint32_t maxAgeMs) {
  if (!gPhaseBudgetSnapshot.phasesOk) return false;
  if (gPhaseBudgetSnapshot.updatedMs == 0UL) return false;
  return ((uint32_t)(millis() - gPhaseBudgetSnapshot.updatedMs) <= maxAgeMs);
}

const PhaseBudgetSnapshot& phaseBudgetGetSnapshot() {
  return gPhaseBudgetSnapshot;
}

float phaseBudgetSelectedPhaseCurrentA(uint8_t phase123) {
  switch (phase123) {
    case 1: return gPhaseBudgetSnapshot.i1A;
    case 2: return gPhaseBudgetSnapshot.i2A;
    case 3: return gPhaseBudgetSnapshot.i3A;
    default: return NAN;
  }
}

float phaseBudgetEstimateStandaloneAllowedPercent(uint8_t phase123, float currentEffectivePercent, float capMaxPercent) {
  if (capMaxPercent <= 0.0f) capMaxPercent = 100.0f;
  if (!phaseGuardEnabled) return capMaxPercent;
  if (!phaseBudgetMeterSupportsPhases()) return capMaxPercent;
  if (!phaseBudgetSnapshotFresh(PHASES_STALE_MS)) return 0.0f;

  const float phaseA = phaseBudgetSelectedPhaseCurrentA(phase123);
  const float maxA = getConfiguredPhaseGuardMaxA(phase123);
  if (!isfinite(phaseA) || !isfinite(maxA) || maxA <= 0.0f) return 0.0f;

  float effectivePercent = currentEffectivePercent;
  if (!isfinite(effectivePercent)) effectivePercent = 0.0f;
  if (effectivePercent < 0.0f) effectivePercent = 0.0f;
  if (effectivePercent > capMaxPercent) effectivePercent = capMaxPercent;

  float boilerMaxA = 0.0f;
  if (maxBoilerPowerW > 1.0f) boilerMaxA = maxBoilerPowerW / 230.0f;
  if (!isfinite(boilerMaxA) || boilerMaxA <= 0.01f) return capMaxPercent;

  const float ownCurrentA = boilerMaxA * (effectivePercent / 100.0f);
  float baseCurrentA = phaseA - ownCurrentA;
  if (!isfinite(baseCurrentA) || baseCurrentA < 0.0f) baseCurrentA = 0.0f;

  static constexpr float kReserveA = 0.3f;
  const float availableA = maxA - baseCurrentA - kReserveA;
  float allowedPercent = (availableA / boilerMaxA) * 100.0f;
  if (!isfinite(allowedPercent)) allowedPercent = 0.0f;
  if (allowedPercent < 0.0f) allowedPercent = 0.0f;
  if (allowedPercent > capMaxPercent) allowedPercent = capMaxPercent;
  return allowedPercent;
}
