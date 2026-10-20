#include "MultiController.h"
#include "MultiControllerInternal.h"
#include "ControllerTemperatureGate.h"
#include "app_globals.h"
#include "ScenarioLogic.h"

#include <driver/gpio.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

static constexpr uint8_t MC_APP_MODE_FORCE_HEAT = 1;


static bool mcRemotePriorityScenarioUsesLocalSoftStart(uint8_t scenario) {
  return scenario == MC_SCENARIO_MANUAL_FORCE || scenario == MC_SCENARIO_PRIORITY_HEAT;
}

static bool mcRemotePriorityRampUsesGate(bool scenarioUsesSoftStart) {
  if (!scenarioUsesSoftStart) return false;
  const uint32_t nowGate = millis();
  const bool bootLockActive = ((uint32_t)(nowGate - pwmBootStartMs) < PWM_BOOT_LOCK_MS);
  const bool dataReady = lastMeterOk && pwmSeenValidMeterSample;
  return (configMode || bootLockActive || !dataReady);
}

static bool     mcRemotePriorityRampActive[MC_MAX_SLOTS] = {false, false, false};
static uint32_t mcRemotePriorityRampStartMs[MC_MAX_SLOTS] = {0, 0, 0};
static uint8_t  mcRemotePriorityRampScenario[MC_MAX_SLOTS] = {MC_SCENARIO_IDLE, MC_SCENARIO_IDLE, MC_SCENARIO_IDLE};
static bool     mcLocalMasterPriorityRampActive[MC_MAX_SLOTS] = {false, false, false};
static uint32_t mcLocalMasterPriorityRampStartMs[MC_MAX_SLOTS] = {0, 0, 0};
static uint8_t  mcLocalMasterPriorityRampScenario[MC_MAX_SLOTS] = {MC_SCENARIO_IDLE, MC_SCENARIO_IDLE, MC_SCENARIO_IDLE};

static void mcResetAllRemotePriorityRamps() {
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    mcRemotePriorityRampActive[i] = false;
    mcRemotePriorityRampStartMs[i] = 0;
    mcRemotePriorityRampScenario[i] = MC_SCENARIO_IDLE;
  }
}

static void mcResetAllLocalMasterPriorityRamps() {
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    mcLocalMasterPriorityRampActive[i] = false;
    mcLocalMasterPriorityRampStartMs[i] = 0;
    mcLocalMasterPriorityRampScenario[i] = MC_SCENARIO_IDLE;
  }
}

static float mcApplyPrioritySoftStartForSlotState(bool activeState[MC_MAX_SLOTS], uint32_t startMsState[MC_MAX_SLOTS], uint8_t scenarioState[MC_MAX_SLOTS],
                                                  uint8_t slotIndex, float requestedPct, uint8_t scenario) {
  if (slotIndex >= MC_MAX_SLOTS) return requestedPct;

  const bool usesSoftStart = mcRemotePriorityScenarioUsesLocalSoftStart(scenario);
  const bool gateActive = mcRemotePriorityRampUsesGate(usesSoftStart);
  if (!softStartEnabled || !usesSoftStart || gateActive || requestedPct <= 0.0f) {
    activeState[slotIndex] = false;
    scenarioState[slotIndex] = scenario;
    return requestedPct;
  }

  const uint32_t now = millis();
  if (!activeState[slotIndex] || scenarioState[slotIndex] != scenario) {
    activeState[slotIndex] = true;
    startMsState[slotIndex] = now;
    scenarioState[slotIndex] = scenario;
    float startPct = softStartStartPct;
    if (startPct < 0.0f) startPct = 0.0f;
    if (startPct > requestedPct) startPct = requestedPct;
    return startPct;
  }

  const uint32_t durMs = (uint32_t)softStartDurationSec * 1000UL;
  if (durMs == 0) return requestedPct;

  float progress = (float)(now - startMsState[slotIndex]) / (float)durMs;
  if (progress >= 1.0f) return requestedPct;
  if (progress < 0.0f) progress = 0.0f;

  float startPct = softStartStartPct;
  if (startPct < 0.0f) startPct = 0.0f;
  if (startPct > requestedPct) startPct = requestedPct;

  float cappedPct = startPct + (requestedPct - startPct) * progress;
  if (cappedPct > requestedPct) cappedPct = requestedPct;
  if (cappedPct < 0.0f) cappedPct = 0.0f;
  return cappedPct;
}

static float mcApplyRemotePrioritySoftStartForSlot(uint8_t slotIndex, float requestedPct, uint8_t scenario) {
  return mcApplyPrioritySoftStartForSlotState(mcRemotePriorityRampActive, mcRemotePriorityRampStartMs, mcRemotePriorityRampScenario,
                                              slotIndex, requestedPct, scenario);
}

static float mcApplyLocalMasterPrioritySoftStartForSlot(uint8_t slotIndex, float requestedPct, uint8_t scenario) {
  return mcApplyPrioritySoftStartForSlotState(mcLocalMasterPriorityRampActive, mcLocalMasterPriorityRampStartMs, mcLocalMasterPriorityRampScenario,
                                              slotIndex, requestedPct, scenario);
}

static uint8_t mcCountRequestedLocalPwmOutputs() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (mcSlotUsesLocalPwmRequested(i)) count++;
  }
  return count;
}

static void mcSafePinLow(int pin) {
  if (pin < 0) return;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
#ifdef SOC_GPIO_SUPPORT_RTC_INDEPENDENT
  // niets extra nodig; compat placeholder voor bestaande stijl
#endif
  gpio_pulldown_en((gpio_num_t)pin);
}

static void mcApplySlaveEmergencyPhaseGuard(float assigned[MC_MAX_SLOTS]) {
  if (!(mcEnabled && phaseGuardEnabled)) return;
  if (!(mcRole == MC_ROLE_SLAVE && mcModeUsesNetwork() && mcHasConfiguredNetworkPeers())) return;

  float phaseAssigned[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float currentPhaseAssigned[4] = {0.0f, 0.0f, 0.0f, 0.0f};

  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (!mcRuntime[i].localEnabled || !mcRuntime[i].attached) continue;
    const uint8_t ph = mcClampAssignedPhase(mcSlots[i].assignedPhase);
    float targetPct = assigned[i];
    if (targetPct < 0.0f) targetPct = 0.0f;
    phaseAssigned[ph] += targetPct;
    currentPhaseAssigned[ph] += max(0.0f, mcRuntime[i].percent);
  }

  if (!lastPhasesOk) {
    for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) assigned[i] = 0.0f;
    return;
  }

  for (uint8_t ph = 1; ph <= 3; ph++) {
    float phaseTarget = phaseAssigned[ph];
    if (phaseTarget <= 0.0f) continue;

    const float a = mcGetMeasuredPhaseCurrentA(ph);
    const float maxA = getConfiguredPhaseGuardMaxA(ph);
    if (!isfinite(a) || !isfinite(maxA) || maxA <= 0.0f) {
      for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
        if (!mcRuntime[i].localEnabled || !mcRuntime[i].attached) continue;
        if (mcClampAssignedPhase(mcSlots[i].assignedPhase) != ph) continue;
        assigned[i] = 0.0f;
      }
      continue;
    }

    const float currentAssigned = currentPhaseAssigned[ph];
    if (a >= maxA && phaseTarget > (currentAssigned + 0.05f)) {
      phaseTarget = currentAssigned;
    }
    if (a > maxA && a > 0.0f) {
      phaseTarget *= (maxA / a);
    }
    if (phaseTarget < 0.0f) phaseTarget = 0.0f;

    if (phaseTarget + 0.05f >= phaseAssigned[ph] || phaseAssigned[ph] <= 0.0f) continue;

    const float scale = phaseTarget / phaseAssigned[ph];
    for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
      if (!mcRuntime[i].localEnabled || !mcRuntime[i].attached) continue;
      if (mcClampAssignedPhase(mcSlots[i].assignedPhase) != ph) continue;
      assigned[i] *= scale;
      if (assigned[i] < 0.0f) assigned[i] = 0.0f;
    }
  }
}

static bool mcIsBoostPriceNegativeNow() {
  if (!npBoostActiveNow) return false;
  if (!timeIsValid()) return false;
  if (npTodayN <= 0) return false;

  time_t now = time(nullptr);
  if (isoDateLocal(now) != npDateToday) return false;

  int interval = (npIntervalMinToday > 0) ? npIntervalMinToday : 60;
  if (interval <= 0) interval = 60;

  struct tm tmNow;
  localtime_r(&now, &tmNow);
  int minutes = tmNow.tm_hour * 60 + tmNow.tm_min;
  int idx = minutes / interval;
  if (idx < 0 || idx >= npTodayN) return false;

  const float boostPriceCt = npBoostPriceCtKwh(npTodayCt[idx]);
  return boostPriceCt < 0.0f;
}

static uint8_t mcResolveLegacyScenario(float totalPercent) {
  if (totalPercent <= 0.0f) return MC_SCENARIO_IDLE;
  if (legRunning) return MC_SCENARIO_PRIORITY_HEAT;
  if ((uint8_t)controlMode == MC_APP_MODE_FORCE_HEAT) return MC_SCENARIO_MANUAL_FORCE;
  if (npBoostActiveNow) return mcIsBoostPriceNegativeNow() ? MC_SCENARIO_NEGATIVE : MC_SCENARIO_CHEAP;
  if (heatExecElecActiveNow()) return MC_SCENARIO_PRIORITY_HEAT;
  return MC_SCENARIO_SURPLUS;
}

uint8_t mcResolveEffectiveScenario(float totalPercent, uint8_t requestedScenario) {
  if (totalPercent <= 0.0f) return MC_SCENARIO_IDLE;

  uint8_t scenario = requestedScenario;
  if (scenario == MC_SCENARIO_IDLE) {
    scenario = mcAppScenario;
  }
  if (scenario == MC_SCENARIO_IDLE) {
    scenario = mcResolveLegacyScenario(totalPercent);
  }
  return scenario;
}

bool mcIsSlotAllowedForScenario(uint8_t slotIndex, uint8_t scenario) {
  if (slotIndex >= MC_MAX_SLOTS) return false;
  const MultiControllerSlotConfig& s = mcSlots[slotIndex];
  switch (scenario) {
    case MC_SCENARIO_CHEAP:
      return s.useOnCheap;
    case MC_SCENARIO_NEGATIVE:
      return s.useOnNegative;
    case MC_SCENARIO_MANUAL_FORCE:
    case MC_SCENARIO_PRIORITY_HEAT:
      return s.allowManualForce;
    case MC_SCENARIO_SURPLUS:
      return s.useOnSurplus;
    default:
      return false;
  }
}

float mcGetScenarioEligibleMaxPercent(uint8_t scenario) {
  float total = 0.0f;
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (!mcRuntime[i].localEnabled || !mcRuntime[i].attached) continue;
    if (!mcIsSlotAllowedForScenario(i, scenario)) continue;
    float slotMax = (float)mcSlots[i].maxPercent;
    if (slotMax < 0.0f) slotMax = 0.0f;
    total += slotMax;
  }

  // Stap 7 master-slave: voor de master moet de groepscap bij NORMAL en andere
  // scenario's ook de netwerk-slots meenemen. Anders blijft de gevraagde groeps-PWM
  // op het lokale deel steken en krijgt de planner te weinig ruimte om slaves mee te
  // laten lopen.
  if (mcRole == MC_ROLE_MASTER && mcModeUsesNetwork()) {
    for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
      if (i >= mcControllerCount) break;
      if (!mcSlotRequestsNetworkPeer(i)) continue;
      if (!mcIsSlotAllowedForScenario(i, scenario)) continue;
      float slotMax = (float)mcSlots[i].maxPercent;
      if (slotMax < 0.0f) slotMax = 0.0f;
      total += slotMax;
    }
  }

  if (total <= 0.0f && mcRuntime[0].attached) {
    total = (float)mcSlots[0].maxPercent;
  }
  return total;
}

static void mcDetachAllRuntimeSlots() {
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (mcRuntime[i].attached && mcRuntime[i].effectivePin >= 0) {
      ledcWriteChannel(mcRuntime[i].ledcChannel, 0);
      ledcDetach((uint8_t)mcRuntime[i].effectivePin);
      mcSafePinLow(mcRuntime[i].effectivePin);
    }
    mcResetRuntimeSlot(i);
  }

  mcSafePinLow(MC_PRIMARY_PWM_PIN);
  for (size_t i = 0; i < 3; i++) mcSafePinLow(MC_EXTRA_PWM_PINS[i]);
}

static bool mcAttachRequestedSlots(bool primaryOnly) {
  bool ok = true;

  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (!mcSlotUsesLocalPwmRequested(i)) continue;
    if (primaryOnly && i > 0) continue;

    const int pin = (i == 0) ? MC_PRIMARY_PWM_PIN : mcSlots[i].pwmPin;
    mcRuntime[i].localEnabled = true;
    mcRuntime[i].ledcChannel = i;
    mcRuntime[i].effectivePin = pin;

    if (ledcAttachChannel((uint8_t)pin, mcRuntimePwmFreqHz, mcRuntimePwmResolutionBits, (int8_t)i)) {
      mcRuntime[i].attached = true;
      mcRuntime[i].duty = 0;
      mcRuntime[i].percent = 0.0f;
      ledcWriteChannel(i, 0);
    } else {
      mcRuntime[i].attached = false;
      mcRuntime[i].duty = 0;
      mcRuntime[i].percent = 0.0f;
      ok = false;
      mcSafePinLow(pin);
    }
  }

  mcEffectiveLocalPwmCount = 0;
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (mcRuntime[i].localEnabled && mcRuntime[i].attached) mcEffectiveLocalPwmCount++;
  }

  return ok;
}

static float mcMaxDutyFloat() {
  const uint32_t maxDuty = (1UL << mcRuntimePwmResolutionBits) - 1UL;
  return (maxDuty > 0UL) ? (float)maxDuty : 255.0f;
}

static uint32_t mcPercentToDuty(float percent) {
  if (percent < 0.0f) percent = 0.0f;
  if (percent > 100.0f) percent = 100.0f;
  const float maxDuty = mcMaxDutyFloat();
  return (uint32_t)lroundf((percent / 100.0f) * maxDuty);
}

static float mcDutyToPercent(uint32_t duty) {
  const float maxDuty = mcMaxDutyFloat();
  if (maxDuty <= 0.0f) return 0.0f;
  if ((float)duty > maxDuty) duty = (uint32_t)maxDuty;
  return ((float)duty / maxDuty) * 100.0f;
}

static uint8_t mcCollectActiveLocalSlots(uint8_t outSlots[MC_MAX_SLOTS]) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (mcRuntime[i].localEnabled && mcRuntime[i].attached) outSlots[count++] = i;
  }
  return count;
}

static void mcApplyLocalRuntimeSlotTarget(uint8_t slotIndex, float rawRequestedPercent, float allowedPercent, bool syncRequestedToAllowed, uint8_t scenario) {
  if (slotIndex >= MC_MAX_SLOTS) return;

  float safeAllowed = allowedPercent;
  if (!isfinite(safeAllowed)) safeAllowed = 0.0f;
  if (safeAllowed < 0.0f) safeAllowed = 0.0f;
  const float slotMax = constrain((float)mcSlots[slotIndex].maxPercent, 0.0f, 100.0f);
  if (safeAllowed > slotMax) safeAllowed = slotMax;

  float rawRequested = rawRequestedPercent;
  if (!isfinite(rawRequested)) rawRequested = 0.0f;
  if (rawRequested < 0.0f) rawRequested = 0.0f;
  if (rawRequested > slotMax) rawRequested = slotMax;

  const bool tempReleased = controllerTemperatureGateIsReleased(slotIndex);
  const String tempReason = controllerTemperatureGateGetBlockReason(slotIndex);
  const bool tempGateApplies = controllerTemperatureGateAppliesToScenario(scenario);
  if (tempGateApplies && !tempReleased) {
    safeAllowed = 0.0f;
    if (syncRequestedToAllowed) rawRequested = 0.0f;
  }

  mcRuntime[slotIndex].requestedPercent = syncRequestedToAllowed ? safeAllowed : rawRequested;
  mcRuntime[slotIndex].allowedPercent = safeAllowed;

  if (mcRuntime[slotIndex].attached) {
    const uint32_t duty = mcPercentToDuty(safeAllowed);
    ledcWriteChannel(mcRuntime[slotIndex].ledcChannel, duty);
    mcRuntime[slotIndex].duty = duty;
    mcRuntime[slotIndex].percent = mcDutyToPercent(duty);
  } else {
    mcRuntime[slotIndex].duty = 0;
    mcRuntime[slotIndex].percent = 0.0f;
    if (mcRuntime[slotIndex].localEnabled && mcRuntime[slotIndex].effectivePin >= 0) {
      mcSafePinLow(mcRuntime[slotIndex].effectivePin);
    }
  }

  mcRuntime[slotIndex].blocked = false;
  mcRuntime[slotIndex].blockReason = "";
  mcRuntime[slotIndex].hasHeadroom = false;
  mcRuntime[slotIndex].headroomPercent = 0.0f;

  if (!mcRuntime[slotIndex].localEnabled) {
    mcRuntime[slotIndex].blockReason = "Geen lokale PWM-uitgang.";
    return;
  }

  if (tempGateApplies && !tempReleased) {
    mcRuntime[slotIndex].blocked = true;
    mcRuntime[slotIndex].blockReason = tempReason.length() ? tempReason : "Geblokkeerd door temperatuur.";
    return;
  }

  if (!mcRuntime[slotIndex].attached) {
    mcRuntime[slotIndex].blocked = true;
    mcRuntime[slotIndex].blockReason = "Lokale PWM-uitgang niet gekoppeld.";
    return;
  }

  if (rawRequested > (mcRuntime[slotIndex].percent + 0.2f)) {
    mcRuntime[slotIndex].blocked = true;
    mcRuntime[slotIndex].blockReason = "Lokaal begrensd door beveiliging of limiet.";
  } else if (mcRuntime[slotIndex].percent >= (slotMax - 0.2f)) {
    mcRuntime[slotIndex].blocked = true;
    mcRuntime[slotIndex].blockReason = "Slot zit op ingestelde max.";
  }

  if (!mcRuntime[slotIndex].blocked) {
    mcRuntime[slotIndex].headroomPercent = max(0.0f, slotMax - mcRuntime[slotIndex].percent);
    mcRuntime[slotIndex].hasHeadroom = (mcRuntime[slotIndex].headroomPercent > 0.2f);
  }
}

static void mcApplyMasterPlannerLocalTargets(uint8_t scenario) {
  if (!(mcRole == MC_ROLE_MASTER && mcModeUsesNetwork() && mcHasConfiguredNetworkPeers())) {
    mcResetAllLocalMasterPriorityRamps();
    return;
  }
  if (!mcGroupPriorityPlannerActive) {
    mcResetAllLocalMasterPriorityRamps();
    return;
  }

  // Bugfix step256:
  // In master/netwerk-modus werden de planner-doelen alleen lokaal toegepast voor SURPLUS.
  // Daardoor kregen remote peers bij FORCE_HEAT / PRIORITY_HEAT wel netjes hun target,
  // maar bleef de lokale master-output buiten hetzelfde plannerpad vallen en gedroeg die
  // zich anders dan de netwerkdeelnemers. Zodra de groepsplanner actief is moet de master
  // zijn EIGEN lokale slots dus voor elk actief groepsscenario uit dezelfde planner-targets
  // aansturen, niet alleen bij overschot.

  float localTargets[MC_MAX_SLOTS] = {0.0f, 0.0f, 0.0f};
  const bool localPrioritySoftStart = multiControllerScenarioUsesParallelPriorityTargets(scenario);
  for (uint8_t gi = 0; gi < mcGroupModelCount; gi++) {
    const MultiControllerGroupParticipantRuntime& gp = mcGroupModel[gi];
    if (!gp.used || !gp.local) continue;
    if (gp.sourceSlot == 0 || gp.sourceSlot > MC_MAX_SLOTS) continue;
    const uint8_t slot = gp.sourceSlot - 1U;
    float slotTarget = gp.targetPct;
    if (localPrioritySoftStart) {
      slotTarget = mcApplyLocalMasterPrioritySoftStartForSlot(slot, slotTarget, scenario);
    }
    localTargets[slot] = slotTarget;
  }

  if (!localPrioritySoftStart) {
    mcResetAllLocalMasterPriorityRamps();
  }

  mcApplyPerControllerPhaseGuard(scenario, localTargets);

  mcAssignedGroupPercent = 0.0f;
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    mcApplyLocalRuntimeSlotTarget(i, localTargets[i], localTargets[i], true, scenario);
    mcAssignedGroupPercent += mcRuntime[i].percent;
  }
}

uint8_t mcCollectScenarioEligibleLocalSlots(uint8_t scenario, uint8_t outSlots[MC_MAX_SLOTS]) {
  uint8_t active[MC_MAX_SLOTS] = {0, 0, 0};
  const uint8_t activeCount = mcCollectActiveLocalSlots(active);
  uint8_t count = 0;

  for (uint8_t i = 0; i < activeCount; i++) {
    const uint8_t slot = active[i];
    if (mcIsSlotAllowedForScenario(slot, scenario)) outSlots[count++] = slot;
  }

  if (count == 0 && activeCount > 0) {
    for (uint8_t i = 0; i < activeCount; i++) {
      if (active[i] == 0) {
        outSlots[0] = 0;
        return 1;
      }
    }
    outSlots[0] = active[0];
    return 1;
  }

  return count;
}

void multiControllerPrepareSafePins() {
  mcSafePinLow(MC_PRIMARY_PWM_PIN);
  for (size_t i = 0; i < 3; i++) mcSafePinLow(MC_EXTRA_PWM_PINS[i]);
}

bool multiControllerInitLocalPwmRuntime(uint32_t pwmFreqHz, uint8_t pwmResolutionBits) {
  mcRuntimePwmFreqHz = pwmFreqHz;
  mcRuntimePwmResolutionBits = pwmResolutionBits;
  mcRequestedLocalPwmCount = mcCountRequestedLocalPwmOutputs();
  mcEffectiveLocalPwmCount = 0;
  mcRequestedGroupPercent = 0.0f;
  mcAssignedGroupPercent = 0.0f;
  mcCurrentScenario = MC_SCENARIO_IDLE;
  mcCurrentScenarioLabel = "idle";
  mcNegativePriorityOverrideActive = false;
  mcAppScenario = MC_SCENARIO_IDLE;
  mcPeerRoundRobinIndex = 1;
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    mcResetPeerRuntime(i);
    mcResetGroupParticipant(i);
  }
  mcResetPerPhaseGuardState();
  controllerTemperatureGateRefresh();
  mcRebuildGroupModel();

  String configReason;
  mcConfigValid = mcValidateConfig(configReason);
  mcRuntimeValid = true;
  mcFallbackActive = !mcConfigValid;
  mcFallbackReason = mcConfigValid ? "Geen fallback actief" : configReason;

  mcDetachAllRuntimeSlots();

  bool ok = mcAttachRequestedSlots(mcFallbackActive);

  if (!ok && !mcFallbackActive) {
    mcRuntimeValid = false;
    mcFallbackActive = true;
    mcFallbackReason = "Lokale PWM-runtime niet volledig klaar; veilige terugval naar alleen controller 1.";
    mcDetachAllRuntimeSlots();
    ok = mcAttachRequestedSlots(true);
  }

  if (!ok) {
    mcRuntimeValid = false;
    mcFallbackActive = true;
    mcFallbackReason = "Primaire PWM-uitgang kon niet veilig worden geïnitialiseerd; PWM blijft op 0%.";
  }

  if (mcRequestedLocalPwmCount < 1) mcRequestedLocalPwmCount = 1;
  if (!mcFallbackActive && mcEffectiveLocalPwmCount != mcRequestedLocalPwmCount) {
    mcRuntimeValid = false;
    mcFallbackActive = true;
    mcFallbackReason = "Niet alle lokale PWM-uitgangen zijn actief geworden; veilige terugval naar alleen controller 1.";
    mcDetachAllRuntimeSlots();
    const bool primaryOk = mcAttachRequestedSlots(true);
    if (!primaryOk) {
      mcRuntimeValid = false;
      mcFallbackReason = "Primaire PWM-uitgang kon niet veilig worden geïnitialiseerd; PWM blijft op 0%.";
    }
  }

  if (!mcFallbackActive) {
    mcFallbackReason = "Geen fallback actief";
  }

  return (!mcFallbackActive && mcConfigValid && mcRuntimeValid);
}

void multiControllerSetAppScenario(uint8_t scenario) {
  if (mcAppScenario != scenario) {
    mcPriorityRampActive = false;
  }
  mcAppScenario = scenario;
}

void multiControllerApplyRequestedGroupPercentForScenario(float requestedPercent, uint8_t scenario) {
  controllerTemperatureGateRefresh();
  if (requestedPercent < 0.0f) requestedPercent = 0.0f;

  uint8_t effectiveScenario = mcResolveEffectiveScenario(requestedPercent, scenario);
  const float maxGroupPercent = mcGetScenarioEligibleMaxPercent(effectiveScenario);
  if (requestedPercent > maxGroupPercent) requestedPercent = maxGroupPercent;

  float desiredPct[MC_MAX_SLOTS] = {0.0f, 0.0f, 0.0f};
  float assignedPct[MC_MAX_SLOTS] = {0.0f, 0.0f, 0.0f};
  const bool masterNetworkPlannerActive = (mcRole == MC_ROLE_MASTER && mcModeUsesNetwork() && mcHasConfiguredNetworkPeers());
  const bool slaveRole = (mcRole == MC_ROLE_SLAVE && mcModeUsesNetwork() && mcHasConfiguredNetworkPeers());
  const bool localLegionellaOverrideActive = multiControllerSlaveLegionellaOverrideActive();
  const bool remoteControlActive = slaveRole && !localLegionellaOverrideActive && mcHasFreshRemoteTargets();
  const bool waitingForRemoteTargets = slaveRole && !localLegionellaOverrideActive && !remoteControlActive;
  if (remoteControlActive) {
    effectiveScenario = mcRemoteTargetsScenario;
    mcCurrentScenario = effectiveScenario;
    mcCurrentScenarioLabel = mcScenarioLabel(mcCurrentScenario);
    for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
      if (!mcSlotUsesLocalPwmRequested(i)) continue;
      float remotePct = constrain(mcRemoteTargetsPerSlot[i], 0.0f, (float)mcSlots[i].maxPercent);
      // Belangrijk: netwerk-peers in netwerkmodus moeten bij force/prioriteitswarmte
      // dezelfde lokale softstartduur respecteren als lokale prioriteitsaansturing.
      // Zonder deze stap sprong een follower-ESP direct naar zijn remote target/max.
      remotePct = mcApplyRemotePrioritySoftStartForSlot(i, remotePct, effectiveScenario);
      desiredPct[i] = remotePct;
    }
    mcRemoteTargetsReason = String("Remote targets actief van ") + (mcRemoteTargetsOwnerIp.length() ? mcRemoteTargetsOwnerIp : String("master"));
  } else if (waitingForRemoteTargets) {
    mcResetAllRemotePriorityRamps();
    mcResetAllLocalMasterPriorityRamps();
    mcCurrentScenario = MC_SCENARIO_IDLE;
    mcCurrentScenarioLabel = "slave-wacht";
    if (mcRemoteTargetsReceivedMs == 0) {
      mcRemoteTargetsReason = "Slave wacht op eerste master-doelen.";
    } else {
      mcRemoteTargetsReason = "Master-doelen verlopen; slave wacht op verse opdrachten.";
    }
  } else if (masterNetworkPlannerActive) {
    mcResetAllRemotePriorityRamps();
    mcCurrentScenario = effectiveScenario;
    mcCurrentScenarioLabel = mcScenarioLabel(mcCurrentScenario);
    mcNegativePriorityOverrideActive = false;
  } else {
    mcResetAllRemotePriorityRamps();
    mcResetAllLocalMasterPriorityRamps();
    mcBuildDesiredDistribution(requestedPercent, effectiveScenario, desiredPct);
  }

  mcRequestedGroupPercent = remoteControlActive ? mcRemoteTargetsRequestedGroupPercent : (slaveRole && !localLegionellaOverrideActive ? 0.0f : requestedPercent);

  if (!masterNetworkPlannerActive || remoteControlActive || waitingForRemoteTargets) {
    for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) assignedPct[i] = desiredPct[i];
    mcApplyPerControllerPhaseGuard(effectiveScenario, assignedPct);
    if (remoteControlActive && slaveRole && !localLegionellaOverrideActive) {
      mcApplySlaveEmergencyPhaseGuard(assignedPct);
    }

    mcAssignedGroupPercent = 0.0f;
    const bool syncLocalRequestedToAllowed = !remoteControlActive;
    for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
      mcApplyLocalRuntimeSlotTarget(i, desiredPct[i], assignedPct[i], syncLocalRequestedToAllowed, effectiveScenario);
      mcAssignedGroupPercent += mcRuntime[i].percent;
    }
  }

  mcRebuildGroupModel();

  // In netwerk-slave-modus mag de lokale planner niet meer meepraten tegen de master.
  // Deze ESP voert dan alleen remote targets uit of wacht veilig op nieuwe opdrachten,
  // behalve wanneer een expliciete lokale override actief is (bijv. legionella).
  if (slaveRole && !localLegionellaOverrideActive) {
    lastDutyPercent = multiControllerGetPrimaryAssignedPercent();
    return;
  }

  mcPlanGroupPriorityTargets(mcRequestedGroupPercent, mcCurrentScenario);
  mcApplyMasterGroupPhaseGuardToPlannerTargets(mcCurrentScenario);
  mcApplyMasterPlannerLocalTargets(mcCurrentScenario);
  mcRebuildGroupModel();
  lastDutyPercent = multiControllerGetPrimaryAssignedPercent();
}

void multiControllerApplyRequestedGroupPercent(float requestedPercent) {
  multiControllerApplyRequestedGroupPercentForScenario(requestedPercent, mcAppScenario);
}

void multiControllerApplyLocalPwmState(uint32_t primaryDuty) {
  const uint32_t maxDuty = (1UL << mcRuntimePwmResolutionBits) - 1UL;
  if (primaryDuty > maxDuty) primaryDuty = maxDuty;
  multiControllerApplyRequestedGroupPercent(mcDutyToPercent(primaryDuty));
}

void multiControllerReconfigureLocalPwmRuntime(uint32_t pwmFreqHz, uint8_t pwmResolutionBits, uint32_t primaryDuty) {
  multiControllerInitLocalPwmRuntime(pwmFreqHz, pwmResolutionBits);
  multiControllerApplyLocalPwmState(primaryDuty);
}
