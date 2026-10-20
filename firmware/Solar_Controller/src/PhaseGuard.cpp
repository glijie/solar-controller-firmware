#include "PhaseGuard.h"

#include <math.h>

#include "app_globals.h"

float getPhaseCurrentA(uint8_t phase123) {
  switch (phase123) {
    case 1: return lastPhaseI1A;
    case 2: return lastPhaseI2A;
    case 3: return lastPhaseI3A;
    default: return NAN;
  }
}

float getConfiguredPhaseGuardMaxA(uint8_t phase123) {
  switch (phase123) {
    case 1: return phaseGuardMaxA_L1;
    case 2: return phaseGuardMaxA_L2;
    case 3: return phaseGuardMaxA_L3;
    default: return phaseGuardMaxA;
  }
}

void refreshLegacyPhaseGuardMaxA() {
  phaseGuardMaxA = getConfiguredPhaseGuardMaxA(phaseGuardPhase);
}

void updatePhaseGuardDecision() {
  phaseGuardModeActive = phaseGuardEnabled;

  if (!phaseGuardEnabled || !phaseGuardModeActive) {
    phaseGuardWouldLimit = false;
    phaseGuardSelectedA  = NAN;
    phaseGuardOverA      = 0.0f;
    return;
  }

  if (!lastPhasesOk) {
    phaseGuardWouldLimit = false;
    phaseGuardSelectedA  = NAN;
    phaseGuardOverA      = 0.0f;
    return;
  }

  const float a = getPhaseCurrentA(phaseGuardPhase);
  phaseGuardSelectedA = a;
  if (!isfinite(a)) {
    phaseGuardWouldLimit = false;
    phaseGuardOverA      = 0.0f;
    return;
  }

  const float maxA = getConfiguredPhaseGuardMaxA(phaseGuardPhase);

  static bool latched = false;
  if (latched) {
    if (a < (maxA - 0.3f)) latched = false;
  } else {
    if (a > (maxA + 0.1f)) latched = true;
  }

  phaseGuardWouldLimit = latched;
  phaseGuardOverA      = (a > maxA) ? (a - maxA) : 0.0f;
}
