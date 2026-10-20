#include "PwmOutput.h"

#include <math.h>

#include "app_globals.h"
#include "MultiController.h"
#include "ControllerTemperatureGate.h"
#include "PhaseBudget.h"

namespace {

static constexpr uint32_t kPwmFreqHz = 1000;
static constexpr uint8_t  kPwmResolutionBits = 8;
static constexpr uint32_t kPwmMaxDuty = 255;

static float applyPhaseGuardHybridLimit(float targetPercent, float currentEffectivePercent, float capMaxPercent) {
  static uint8_t  overCount = 0;
  static float    capPct    = 100.0f;
  static uint32_t lastStepMs = 0;
  static float    lastCapMaxPct = 100.0f;

  const uint32_t now = millis();
  if (capMaxPercent <= 0.0f) capMaxPercent = 100.0f;

  const bool active = phaseGuardEnabled;
  if (!active) {
    overCount = 0;
    capPct = capMaxPercent;
    lastCapMaxPct = capMaxPercent;
    lastStepMs = now;
    return targetPercent;
  }

  if (fabsf(lastCapMaxPct - capMaxPercent) > 0.01f) {
    if (capMaxPercent > lastCapMaxPct) {
      capPct = capMaxPercent;
    } else if (capPct > capMaxPercent) {
      capPct = capMaxPercent;
    }
    if (capPct <= 0.0f) capPct = capMaxPercent;
    lastCapMaxPct = capMaxPercent;
  }

  if (!lastPhasesOk) {
    overCount = 0;
    capPct = 0.0f;
    lastCapMaxPct = capMaxPercent;
    lastStepMs = now;
    return 0.0f;
  }

  const float a = appGetPhaseCurrentA(phaseGuardPhase);
  const float maxA = getConfiguredPhaseGuardMaxA(phaseGuardPhase);
  if (!isfinite(a) || !isfinite(maxA) || maxA <= 0.0f) {
    overCount = 0;
    capPct = 0.0f;
    lastCapMaxPct = capMaxPercent;
    lastStepMs = now;
    return 0.0f;
  }

  if (a >= maxA && targetPercent > (currentEffectivePercent + 0.05f)) {
    targetPercent = currentEffectivePercent;
  }

  if (a > maxA) {
    const float factor = maxA / a;
    targetPercent *= factor;
  }

  static const float MARGIN_A = 0.3f;
  static const uint8_t OVER_N = 3;
  static const float STEP_DOWN_PCT = 5.0f;
  static const float STEP_UP_PCT   = 5.0f;

  if ((uint32_t)(now - lastStepMs) >= 1000UL) {
    const bool over  = (a > (maxA + MARGIN_A));
    const bool under = (a < (maxA - MARGIN_A));

    if (over) {
      if (overCount < 255) overCount++;
      if (overCount >= OVER_N) {
        capPct -= STEP_DOWN_PCT;
        if (capPct < 0.0f) capPct = 0.0f;
        overCount = OVER_N;
      }
    } else {
      overCount = 0;
      if (under && capPct < capMaxPercent) {
        if (capMaxPercent > 100.0f) {
          capPct = capMaxPercent;
        } else {
          capPct += STEP_UP_PCT;
          if (capPct > capMaxPercent) capPct = capMaxPercent;
        }
      }
    }

    lastStepMs = now;
  }

  if (capPct < capMaxPercent && targetPercent > capPct) {
    targetPercent = capPct;
  }

  return targetPercent;
}

} // namespace

bool pwmInit() {
  return multiControllerInitLocalPwmRuntime(kPwmFreqHz, kPwmResolutionBits);
}

void pwmWriteDuty(uint32_t duty) {
  const uint32_t now = millis();
  const bool bootLockActive = (pwmBootStartMs != 0) && ((uint32_t)(now - pwmBootStartMs) < PWM_BOOT_LOCK_MS);
  const bool dataReady = lastMeterOk && pwmSeenValidMeterSample;

  if (configMode || bootLockActive || !dataReady) {
    duty = 0;
  }

  if (duty > kPwmMaxDuty) duty = kPwmMaxDuty;
  multiControllerSetAppScenario(appMultiControllerCurrentAppScenario());
  multiControllerApplyLocalPwmState(duty);
}

float multiControllerGroupTargetMaxForScenario(uint8_t scenario) {
  float maxGroupPercent = multiControllerGetScenarioGroupMaxPercent(scenario);
  if (maxGroupPercent < 0.0f) maxGroupPercent = 0.0f;
  return maxGroupPercent;
}

void setBoilerDutyPercentForScenario(float targetPercent, uint8_t forcedScenario) {
  controllerTemperatureGateRefresh();

  const uint8_t scenario = (forcedScenario == MC_SCENARIO_IDLE) ? appMultiControllerCurrentAppScenario() : forcedScenario;
  const bool routeAsGroup = (scenario != MC_SCENARIO_SURPLUS) && multiControllerShouldUseGroupControlForScenario(scenario);

  const uint32_t nowGate = millis();
  const bool bootLockActive = ((uint32_t)(nowGate - pwmBootStartMs) < PWM_BOOT_LOCK_MS);
  const bool dataReady = lastMeterOk && pwmSeenValidMeterSample;
  const bool gateActive = (configMode || bootLockActive || !dataReady);
  if (gateActive) {
    targetPercent = 0.0f;
  }

  const float routeCapPercent = routeAsGroup ? multiControllerGroupTargetMaxForScenario(scenario) : 100.0f;
  const bool primaryTempReleased = controllerTemperatureGateIsReleased(0);
  const bool tempGateApplies = controllerTemperatureGateAppliesToScenario(scenario);

  if (!routeAsGroup && tempGateApplies && !primaryTempReleased) {
    targetPercent = 0.0f;
  }

  if (routeAsGroup) {
    if (targetPercent < 0.0f) targetPercent = 0.0f;
    if (targetPercent > routeCapPercent) targetPercent = routeCapPercent;
  } else {
    if (targetPercent < 0.0f) targetPercent = 0.0f;
    if (targetPercent > 100.0f) targetPercent = 100.0f;
    if (targetPercent > maxOutputPercentLimit) targetPercent = maxOutputPercentLimit;
  }

  const bool prioActive = appPriorityHeatingActiveNow();
  appRefreshSmartGatewaysPhasesForGuard();

  const float currentEffectivePercent = routeAsGroup ? multiControllerGetAssignedGroupPercent() : lastDutyPercent;
  const bool usePerSlotPriorityRamp = routeAsGroup && prioActive && multiControllerScenarioUsesParallelPriorityTargets(scenario);
  const bool usePerControllerPhaseGuard = usePerSlotPriorityRamp && phaseGuardEnabled;

  // Step 264:
  // In standalone zonne-overschot moet de veilige fase-/loadbalancing-grens
  // niet pas achteraf terugduwen, maar al vóór de gewone PWM-aansturing het
  // maximaal toegestane target begrenzen. Daardoor blijft de laatst veilige
  // toegepaste PWM de leidende referentie voor de volgende update.
  if (!routeAsGroup && scenario == MC_SCENARIO_SURPLUS) {
    const float allowedPercent = phaseBudgetEstimateStandaloneAllowedPercent(phaseGuardPhase, currentEffectivePercent, maxOutputPercentLimit);
    if (targetPercent > allowedPercent) targetPercent = allowedPercent;
  }

  // Step 265:
  // Ook standalone prio/force/boost eerst op een veilige allowed waarde begrenzen,
  // zodat soft-start niet blijft doorrampen richting een oud intern maximum terwijl
  // fase/loadbalancing de echte uitgang al lager heeft vastgezet.
  const bool standalonePriorityScenario = (!routeAsGroup && prioActive && scenario != MC_SCENARIO_SURPLUS);
  if (standalonePriorityScenario) {
    const float allowedPercent = phaseBudgetEstimateStandaloneAllowedPercent(phaseGuardPhase, currentEffectivePercent, maxOutputPercentLimit);
    if (targetPercent > allowedPercent) targetPercent = allowedPercent;
  }

  if (!usePerSlotPriorityRamp) {
    float softStartBeginPercent = softStartStartPct;
    if (routeAsGroup && prioActive) {
      const float groupStartPercent = multiControllerGetScenarioSoftStartGroupStartPercent(scenario, softStartStartPct);
      if (groupStartPercent > 0.0f) softStartBeginPercent = groupStartPercent;
    }
    targetPercent = appApplyPrioritySoftStartWithStart(targetPercent, prioActive, gateActive, softStartBeginPercent, currentEffectivePercent, standalonePriorityScenario);
  }

  if (!usePerControllerPhaseGuard) {
    targetPercent = applyPhaseGuardHybridLimit(targetPercent, currentEffectivePercent, routeCapPercent);
  }

  const float comparePercent = routeAsGroup ? multiControllerGetAssignedGroupPercent() : lastDutyPercent;
  const float diff = fabs(targetPercent - comparePercent);
  const unsigned long now = millis();

  if (diff < minStepPercent && (now - lastUpdateMs) < minUpdateIntervalMs) {
    return;
  }

  lastUpdateMs = now;

  if (!routeAsGroup && tempGateApplies && !primaryTempReleased) {
    targetPercent = 0.0f;
  }

  if (routeAsGroup) {
    multiControllerSetAppScenario(scenario);
    multiControllerApplyRequestedGroupPercentForScenario(targetPercent, scenario);
    lastDutyPercent = multiControllerGetPrimaryAssignedPercent();
    return;
  }

  lastDutyPercent = targetPercent;
  const uint32_t duty = (uint32_t)lround((targetPercent / 100.0f) * (float)kPwmMaxDuty);
  pwmWriteDuty(duty);
}

void setBoilerDutyPercent(float targetPercent) {
  setBoilerDutyPercentForScenario(targetPercent, appMultiControllerCurrentAppScenario());
}

void setBoilerGroupDutyPercent(float targetPercent) {
  const uint32_t nowGate = millis();
  const bool bootLockActive = ((uint32_t)(nowGate - pwmBootStartMs) < PWM_BOOT_LOCK_MS);
  const bool dataReady = lastMeterOk && pwmSeenValidMeterSample;
  const bool gateActive = (configMode || bootLockActive || !dataReady);
  if (gateActive) {
    targetPercent = 0.0f;
  }

  if (targetPercent < 0.0f) targetPercent = 0.0f;

  float maxGroupPercent = multiControllerGetSurplusGroupMaxPercent();
  if (maxGroupPercent < 0.0f) maxGroupPercent = 0.0f;
  if (targetPercent > maxGroupPercent) targetPercent = maxGroupPercent;

  const float currentGroupPercent = multiControllerGetAssignedGroupPercent();
  const float diff = fabs(targetPercent - currentGroupPercent);
  const unsigned long now = millis();

  if (diff < minStepPercent && (now - lastUpdateMs) < minUpdateIntervalMs) {
    return;
  }

  lastUpdateMs = now;
  multiControllerApplyRequestedGroupPercentForScenario(targetPercent, MC_SCENARIO_SURPLUS);
  lastDutyPercent = multiControllerGetPrimaryAssignedPercent();
}
