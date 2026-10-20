#include "MultiControllerInternal.h"
#include "ControllerTemperatureGate.h"
#include "app_globals.h"
#include "PhaseBudget.h"

#include <math.h>

static void mcSortSlotsByOrder(uint8_t slots[MC_MAX_SLOTS], uint8_t count, bool rampUp) {
  for (uint8_t i = 0; i < count; i++) {
    for (uint8_t j = i + 1; j < count; j++) {
      uint8_t a = rampUp ? mcSlots[slots[i]].rampUpOrder : mcSlots[slots[i]].rampDownOrder;
      uint8_t b = rampUp ? mcSlots[slots[j]].rampUpOrder : mcSlots[slots[j]].rampDownOrder;
      if (b < a || (b == a && slots[j] < slots[i])) {
        uint8_t tmp = slots[i];
        slots[i] = slots[j];
        slots[j] = tmp;
      }
    }
  }
}

static float mcStageOneCapForSlot(uint8_t slotIndex, bool hasNextActiveSlot) {
  const MultiControllerSlotConfig& s = mcSlots[slotIndex];
  float maxPct = (float)s.maxPercent;
  if (!hasNextActiveSlot) return maxPct;

  float thresholdPct = (float)s.startAfterPercent;
  if (thresholdPct < (float)s.minPercent) thresholdPct = (float)s.minPercent;
  if (thresholdPct > maxPct) thresholdPct = maxPct;
  return thresholdPct;
}

static void mcZeroDistribution(float assigned[MC_MAX_SLOTS]) {
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) assigned[i] = 0.0f;
}

bool mcScenarioUsesParallelPriorityTargets(uint8_t scenario) {
  return scenario == MC_SCENARIO_MANUAL_FORCE ||
         scenario == MC_SCENARIO_PRIORITY_HEAT ||
         scenario == MC_SCENARIO_CHEAP ||
         scenario == MC_SCENARIO_NEGATIVE;
}

static bool mcScenarioUsesNetworkFullParticipantMax(uint8_t scenario) {
  return scenario == MC_SCENARIO_MANUAL_FORCE ||
         scenario == MC_SCENARIO_PRIORITY_HEAT;
}

static uint8_t mcSlotsMask(const uint8_t slots[MC_MAX_SLOTS], uint8_t count) {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (slots[i] < 8) mask |= (uint8_t)(1U << slots[i]);
  }
  return mask;
}

static float mcParallelPriorityRampProgress(uint8_t scenario, const uint8_t slots[MC_MAX_SLOTS], uint8_t count) {
  if (!mcScenarioUsesParallelPriorityTargets(scenario)) return 1.0f;
  if (!softStartEnabled) return 1.0f;
  if (softStartDurationSec == 0) return 1.0f;
  if (count == 0) return 1.0f;

  const uint8_t mask = mcSlotsMask(slots, count);
  const uint32_t now = millis();
  if (!mcPriorityRampActive || mcPriorityRampScenario != scenario || mcPriorityRampSlotsMask != mask) {
    mcPriorityRampActive = true;
    mcPriorityRampStartMs = now;
    mcPriorityRampScenario = scenario;
    mcPriorityRampSlotsMask = mask;
    return 0.0f;
  }

  const uint32_t durMs = (uint32_t)softStartDurationSec * 1000UL;
  if (durMs == 0) return 1.0f;
  float p = (float)(now - mcPriorityRampStartMs) / (float)durMs;
  if (p < 0.0f) p = 0.0f;
  if (p >= 1.0f) {
    // Belangrijk: laat de ramp NIET opnieuw starten zolang scenario + slots gelijk blijven.
    // In step153 werd mcPriorityRampActive hier op false gezet, waardoor de volgende call
    // de ramp weer vanaf 0 begon. Dat gaf precies het "force lijkt elke paar seconden
    // opnieuw te starten" gedrag. Alleen bij scenario-/slotwissel of wanneer er geen
    // prioriteitsdoel meer is, resetten we de ramp-state elders bewust.
    return 1.0f;
  }
  return p;
}

static float mcScenarioSoftStartBasePercent(uint8_t scenario) {
  if (!softStartEnabled) return 0.0f;
  if (!mcScenarioUsesParallelPriorityTargets(scenario)) return 0.0f;
  float startPct = softStartStartPct;
  if (startPct < 0.0f) startPct = 0.0f;
  return startPct;
}


static void mcBuildParallelPriorityTargetDistribution(float totalPercent, uint8_t scenario, const uint8_t slots[MC_MAX_SLOTS], uint8_t count, float assigned[MC_MAX_SLOTS]) {
  mcZeroDistribution(assigned);
  if (totalPercent <= 0.0f || count == 0) {
    mcPriorityRampActive = false;
    return;
  }

  float totalMax = 0.0f;
  for (uint8_t i = 0; i < count; i++) {
    float maxPct = (float)mcSlots[slots[i]].maxPercent;
    if (maxPct < 0.0f) maxPct = 0.0f;
    totalMax += maxPct;
  }
  if (totalMax <= 0.0f) {
    mcPriorityRampActive = false;
    return;
  }

  float requested = totalPercent;
  if (requested < 0.0f) requested = 0.0f;
  if (requested > totalMax) requested = totalMax;

  const float progress = mcParallelPriorityRampProgress(scenario, slots, count);

  float sumAssigned = 0.0f;
  for (uint8_t i = 0; i < count; i++) {
    const uint8_t slot = slots[i];
    float slotMax = (float)mcSlots[slot].maxPercent;
    if (slotMax < 0.0f) slotMax = 0.0f;
    const float slotStart = min(mcScenarioSoftStartBasePercent(scenario), slotMax);
    float pct = slotStart + (slotMax - slotStart) * progress;
    if (pct > slotMax) pct = slotMax;
    if (pct < 0.0f) pct = 0.0f;
    assigned[slot] = pct;
    sumAssigned += pct;
  }

  // Alleen als een harde veiligheidslaag de groep lager cap't dan de parallelle targets,
  // schalen we tijdelijk terug naar de gevraagde groepscap. Zonder veiligheids-cap loopt
  // ieder actief slot dus gewoon direct volgens zijn eigen soft-start naar zijn eigen max.
  if (requested + 0.05f < sumAssigned && sumAssigned > 0.0f) {
    const float scale = requested / sumAssigned;
    for (uint8_t i = 0; i < count; i++) {
      const uint8_t slot = slots[i];
      assigned[slot] *= scale;
      if (assigned[slot] < 0.0f) assigned[slot] = 0.0f;
    }
  }
}


static bool mcReleaseBypassAllowedByConfirmedLimit(float actualPct, float maxPct, bool blocked, bool hasHeadroom, const String& blockReason) {
  if (!blocked && hasHeadroom) return false;
  if ((actualPct + 0.2f) >= maxPct) return true;

  String reason = blockReason;
  reason.toLowerCase();
  if (reason.indexOf("max") >= 0) return true;
  if (reason.indexOf("beveilig") >= 0) return true;
  if (reason.indexOf("limiet") >= 0) return true;
  if (reason.indexOf("fase") >= 0) return true;
  if (reason.indexOf("phase") >= 0) return true;
  if (reason.indexOf("safety") >= 0) return true;
  return false;
}

static bool mcLocalSlotReachedReleaseThreshold(uint8_t slotIndex, bool hasNextActiveSlot) {
  if (slotIndex >= MC_MAX_SLOTS) return false;
  if (!hasNextActiveSlot) return true;

  const float releasePct = mcStageOneCapForSlot(slotIndex, true);
  const float actualPct = mcRuntime[slotIndex].percent;

  // Belangrijk: "Volgende mee vanaf PWM %" moet gebaseerd zijn op wat de vorige
  // controller echt toepast, niet alleen op een geplande target. Daardoor kan de
  // volgende controller pas vrijgegeven worden zodra de vorige controller de grens
  // werkelijk haalt. Alleen wanneer de vorige controller BEVESTIGD op een harde
  // veiligheids- of max-limiet vastzit, mag de keten door naar de volgende.
  if ((actualPct + 0.2f) >= releasePct) return true;
  if (mcReleaseBypassAllowedByConfirmedLimit(actualPct,
                                             (float)mcSlots[slotIndex].maxPercent,
                                             mcRuntime[slotIndex].blocked,
                                             mcRuntime[slotIndex].hasHeadroom,
                                             mcRuntime[slotIndex].blockReason)) {
    return true;
  }
  return false;
}

static void mcBuildRampUpDistribution(float totalPercent, const uint8_t slots[MC_MAX_SLOTS], uint8_t count, float assigned[MC_MAX_SLOTS]) {
  mcZeroDistribution(assigned);
  if (totalPercent <= 0.0f || count == 0) return;

  uint8_t ordered[MC_MAX_SLOTS] = {0, 0, 0};
  for (uint8_t i = 0; i < count; i++) ordered[i] = slots[i];
  mcSortSlotsByOrder(ordered, count, true);

  uint8_t unlockedCount = 1;
  while (unlockedCount < count) {
    const uint8_t previousSlot = ordered[unlockedCount - 1U];
    if (!mcLocalSlotReachedReleaseThreshold(previousSlot, true)) break;
    unlockedCount++;
  }

  float remaining = totalPercent;

  for (uint8_t pos = 0; pos < unlockedCount && remaining > 0.0f; pos++) {
    const uint8_t slot = ordered[pos];
    const MultiControllerSlotConfig& s = mcSlots[slot];
    const bool hasNext = (pos + 1U) < count;
    float cap = mcStageOneCapForSlot(slot, hasNext);
    if (cap < 0.0f) cap = 0.0f;

    float assign = 0.0f;
    if (slot == ordered[0]) {
      assign = min(remaining, cap);
    } else {
      if (remaining < (float)s.minPercent) continue;
      assign = min(remaining, cap);
      if (assign > 0.0f && assign < (float)s.minPercent) {
        assign = min((float)s.minPercent, cap);
      }
    }

    if (assign > remaining) assign = remaining;
    if (assign < 0.0f) assign = 0.0f;

    assigned[slot] = assign;
    remaining -= assign;
  }

  if (remaining <= 0.0f) return;

  uint8_t extraCount = unlockedCount;
  if (extraCount < count && extraCount > 0) extraCount--;

  for (uint8_t pos = 0; pos < extraCount && remaining > 0.0f; pos++) {
    const uint8_t slot = ordered[pos];
    const float maxPct = (float)mcSlots[slot].maxPercent;
    float extraCap = maxPct - assigned[slot];
    if (extraCap <= 0.0f) continue;
    float add = min(remaining, extraCap);
    assigned[slot] += add;
    remaining -= add;
  }
}

static float mcGetPhaseAllowedTotalPercentForSurplus(uint8_t phase123, uint8_t scenario) {
  const uint8_t ph = mcClampAssignedPhase(phase123);
  float phaseMax = 0.0f;
  float currentAssigned = 0.0f;

  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (!mcRuntime[i].localEnabled || !mcRuntime[i].attached) continue;
    if (!mcIsSlotAllowedForScenario(i, scenario)) continue;
    if (mcClampAssignedPhase(mcSlots[i].assignedPhase) != ph) continue;

    float slotMax = (float)mcSlots[i].maxPercent;
    if (slotMax < 0.0f) slotMax = 0.0f;
    phaseMax += slotMax;
    currentAssigned += mcRuntime[i].percent;
  }

  if (phaseMax <= 0.0f) return 0.0f;
  if (!lastPhasesOk) return 0.0f;

  float allowed = phaseMax;
  if (mcPhaseGuardCapPct[ph] > 0.0f && allowed > mcPhaseGuardCapPct[ph]) {
    allowed = mcPhaseGuardCapPct[ph];
  }

  const float a = mcGetMeasuredPhaseCurrentA(ph);
  const float maxA = getConfiguredPhaseGuardMaxA(ph);
  if (!isfinite(a) || !isfinite(maxA) || maxA <= 0.0f) return 0.0f;

  if (a >= maxA && allowed > currentAssigned) {
    allowed = currentAssigned;
  }
  if (a > maxA && currentAssigned > 0.0f) {
    const float scaled = currentAssigned * (maxA / a);
    if (scaled < allowed) allowed = scaled;
  }

  if (allowed < 0.0f) allowed = 0.0f;
  if (allowed > phaseMax) allowed = phaseMax;
  return allowed;
}

static void mcBuildRampUpDistributionPhaseAware(float totalPercent, uint8_t scenario, const uint8_t slots[MC_MAX_SLOTS], uint8_t count, float assigned[MC_MAX_SLOTS]) {
  mcZeroDistribution(assigned);
  if (totalPercent <= 0.0f || count == 0) return;

  uint8_t ordered[MC_MAX_SLOTS] = {0, 0, 0};
  for (uint8_t i = 0; i < count; i++) ordered[i] = slots[i];
  mcSortSlotsByOrder(ordered, count, true);

  float phaseAllowed[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float phaseUsed[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  for (uint8_t ph = 1; ph <= 3; ph++) {
    phaseAllowed[ph] = mcGetPhaseAllowedTotalPercentForSurplus(ph, scenario);
  }

  float remaining = totalPercent;

  for (uint8_t pos = 0; pos < count && remaining > 0.0f; pos++) {
    const uint8_t slot = ordered[pos];
    const MultiControllerSlotConfig& s = mcSlots[slot];
    const bool hasNext = (pos + 1U) < count;
    const uint8_t ph = mcClampAssignedPhase(s.assignedPhase);
    float phaseRemaining = phaseAllowed[ph] - phaseUsed[ph];
    if (phaseRemaining < 0.0f) phaseRemaining = 0.0f;

    float cap = mcStageOneCapForSlot(slot, hasNext);
    if (cap > phaseRemaining) cap = phaseRemaining;
    if (cap < 0.0f) cap = 0.0f;

    float assign = 0.0f;
    if (slot == ordered[0]) {
      assign = min(remaining, cap);
    } else {
      if (remaining < (float)s.minPercent) continue;
      assign = min(remaining, cap);
      if (assign > 0.0f && assign < (float)s.minPercent) {
        assign = min((float)s.minPercent, cap);
      }
    }

    if (assign > remaining) assign = remaining;
    if (assign < 0.0f) assign = 0.0f;

    assigned[slot] = assign;
    remaining -= assign;
    phaseUsed[ph] += assign;
  }

  if (remaining <= 0.0f) return;

  for (uint8_t pos = 0; pos < count && remaining > 0.0f; pos++) {
    const uint8_t slot = ordered[pos];
    const uint8_t ph = mcClampAssignedPhase(mcSlots[slot].assignedPhase);
    float phaseRemaining = phaseAllowed[ph] - phaseUsed[ph];
    if (phaseRemaining <= 0.0f) continue;

    float extraCap = (float)mcSlots[slot].maxPercent - assigned[slot];
    if (extraCap > phaseRemaining) extraCap = phaseRemaining;
    if (extraCap <= 0.0f) continue;

    float add = min(remaining, extraCap);
    assigned[slot] += add;
    remaining -= add;
    phaseUsed[ph] += add;
  }
}

static void mcBuildRampDownDistribution(float totalPercent, const uint8_t slots[MC_MAX_SLOTS], uint8_t count, float assigned[MC_MAX_SLOTS]) {
  mcZeroDistribution(assigned);
  if (count == 0) return;

  uint8_t ordered[MC_MAX_SLOTS] = {0, 0, 0};
  for (uint8_t i = 0; i < count; i++) ordered[i] = slots[i];

  // Voorlopig gebruiken alle scenario's exact dezelfde voorspelbare cascade-logica.
  // Er is in deze release bewust GEEN equal-share / gelijke verdeling actief.
  float currentTotal = 0.0f;
  for (uint8_t i = 0; i < count; i++) {
    const uint8_t slot = ordered[i];
    float pct = mcRuntime[slot].percent;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > (float)mcSlots[slot].maxPercent) pct = (float)mcSlots[slot].maxPercent;
    assigned[slot] = pct;
    currentTotal += pct;
  }

  if (currentTotal <= totalPercent) {
    mcBuildRampUpDistribution(totalPercent, slots, count, assigned);
    return;
  }

  mcSortSlotsByOrder(ordered, count, false);
  float excess = currentTotal - totalPercent;

  for (uint8_t i = 0; i < count && excess > 0.0f; i++) {
    const uint8_t slot = ordered[i];
    const float removable = assigned[slot];
    if (removable <= 0.0f) continue;
    const float take = min(excess, removable);
    assigned[slot] -= take;
    excess -= take;
  }

  if (excess > 0.01f) {
    mcBuildRampUpDistribution(totalPercent, slots, count, assigned);
  }
}


static bool mcIsGroupParticipantAllowedForScenario(const MultiControllerGroupParticipantRuntime& gp, uint8_t scenario) {
  switch (scenario) {
    case MC_SCENARIO_CHEAP: return gp.useOnCheap;
    case MC_SCENARIO_NEGATIVE: return gp.useOnNegative;
    case MC_SCENARIO_MANUAL_FORCE:
    case MC_SCENARIO_PRIORITY_HEAT: return gp.allowManualForce;
    case MC_SCENARIO_SURPLUS: return gp.useOnSurplus;
    default: return false;
  }
}

static uint8_t mcCollectScenarioEligibleGroupParticipants(uint8_t scenario, uint8_t outIdx[MC_MAX_SLOTS]) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < mcGroupModelCount; i++) {
    const MultiControllerGroupParticipantRuntime& gp = mcGroupModel[i];
    if (!gp.used || !gp.online || !gp.controllable) continue;
    if (!mcIsGroupParticipantAllowedForScenario(gp, scenario)) continue;
    outIdx[count++] = i;
  }

  if (count == 0) {
    for (uint8_t i = 0; i < mcGroupModelCount; i++) {
      const MultiControllerGroupParticipantRuntime& gp = mcGroupModel[i];
      if (!gp.used || !gp.online || !gp.controllable) continue;
      if (gp.local) {
        outIdx[0] = i;
        return 1;
      }
    }
  }
  return count;
}

static void mcSortGroupParticipantsByOrder(uint8_t idxs[MC_MAX_SLOTS], uint8_t count, bool rampUp) {
  for (uint8_t i = 0; i < count; i++) {
    for (uint8_t j = i + 1; j < count; j++) {
      const MultiControllerGroupParticipantRuntime& a = mcGroupModel[idxs[i]];
      const MultiControllerGroupParticipantRuntime& b = mcGroupModel[idxs[j]];
      const uint8_t oa = rampUp ? a.rampUpOrder : a.rampDownOrder;
      const uint8_t ob = rampUp ? b.rampUpOrder : b.rampDownOrder;
      bool swap = false;
      if (ob < oa) swap = true;
      else if (ob == oa) {
        if (b.controllerId < a.controllerId) swap = true;
        else if (b.controllerId == a.controllerId) {
          if (b.sourceSlot < a.sourceSlot) swap = true;
          else if (b.sourceSlot == a.sourceSlot && b.peerOutputSlot < a.peerOutputSlot) swap = true;
        }
      }
      if (swap) {
        uint8_t tmp = idxs[i];
        idxs[i] = idxs[j];
        idxs[j] = tmp;
      }
    }
  }
}

static float mcGroupStageOneCapForParticipant(const MultiControllerGroupParticipantRuntime& gp, bool hasNext) {
  float maxPct = (float)gp.maxPercent;
  if (!hasNext) return maxPct;
  float thresholdPct = (float)gp.startAfterPercent;
  if (thresholdPct < (float)gp.minPercent) thresholdPct = (float)gp.minPercent;
  if (thresholdPct > maxPct) thresholdPct = maxPct;
  return thresholdPct;
}

static void mcZeroGroupDistribution(float assigned[MC_MAX_SLOTS]) {
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) assigned[i] = 0.0f;
}

static void mcBuildGroupParallelPriorityDistribution(float totalPercent, uint8_t scenario, const uint8_t idxs[MC_MAX_SLOTS], uint8_t count, float assigned[MC_MAX_SLOTS]) {
  mcZeroGroupDistribution(assigned);
  if (totalPercent <= 0.0f || count == 0) return;

  float totalMax = 0.0f;
  for (uint8_t i = 0; i < count; i++) {
    float maxPct = (float)mcGroupModel[idxs[i]].maxPercent;
    if (maxPct < 0.0f) maxPct = 0.0f;
    totalMax += maxPct;
  }
  if (totalMax <= 0.0f) return;

  float requested = totalPercent;
  if (requested < 0.0f) requested = 0.0f;
  if (requested > totalMax) requested = totalMax;

  const float progress = mcParallelPriorityRampProgress(scenario, idxs, count);
  float sumAssigned = 0.0f;
  for (uint8_t i = 0; i < count; i++) {
    const MultiControllerGroupParticipantRuntime& gp = mcGroupModel[idxs[i]];
    float slotMax = (float)gp.maxPercent;
    if (slotMax < 0.0f) slotMax = 0.0f;
    const float slotStart = min(mcScenarioSoftStartBasePercent(scenario), slotMax);
    float pct = slotStart + (slotMax - slotStart) * progress;
    if (pct > slotMax) pct = slotMax;
    if (pct < 0.0f) pct = 0.0f;
    assigned[idxs[i]] = pct;
    sumAssigned += pct;
  }

  if (requested + 0.05f < sumAssigned && sumAssigned > 0.0f) {
    const float scale = requested / sumAssigned;
    for (uint8_t i = 0; i < count; i++) {
      assigned[idxs[i]] *= scale;
      if (assigned[idxs[i]] < 0.0f) assigned[idxs[i]] = 0.0f;
    }
  }
}

static bool mcGroupParticipantGetLiveReleaseState(const MultiControllerGroupParticipantRuntime& gp,
                                                float& actualPct,
                                                bool& blocked,
                                                bool& hasHeadroom,
                                                String& blockReason) {
  actualPct = gp.assignedPct;
  blocked = gp.blocked;
  hasHeadroom = gp.hasHeadroom;
  blockReason = gp.blockReason;

  if (gp.local) return true;
  if (gp.sourceSlot == 0 || gp.sourceSlot > MC_MAX_SLOTS) return false;

  const MultiControllerPeerRuntime& pr = mcPeerRuntime[gp.sourceSlot - 1U];
  const uint32_t now = millis();
  if (!mcPeerStillUsableForWriteback(pr, now)) return false;

  // Stap 6 versnellingsplan:
  // netwerkdeelnemers mogen alleen nog achteraan in de keten staan. Daardoor hoeft
  // de planner niet meer te wachten op een extra "bevestigde" slave-terugmelding
  // NA iedere writeback om de volgende stap in de keten vrij te geven.
  // We gebruiken hier dus de laatst bruikbare cached peerstatus / groepsmodelstatus.
  if (gp.peerOutputSlot != 0 && mcPeerHasUsableCachedStatus(pr, now)) {
    for (uint8_t poIdx = 0; poIdx < pr.compactOutputCount; poIdx++) {
      const MultiControllerPeerOutputRuntime& po = pr.outputs[poIdx];
      if (!po.configured) continue;
      if (po.slot != gp.peerOutputSlot) continue;
      actualPct = po.assignedPct;
      blocked = po.blocked;
      hasHeadroom = po.hasHeadroom;
      blockReason = po.blockReason;
      return true;
    }
  }

  if (mcPeerHasUsableCachedStatus(pr, now)) actualPct = pr.assignedPct;
  return true;
}

static bool mcGroupParticipantReachedReleaseThreshold(const MultiControllerGroupParticipantRuntime& gp, bool hasNextActiveParticipant) {
  if (!hasNextActiveParticipant) return true;

  const float releasePct = mcGroupStageOneCapForParticipant(gp, true);
  float actualPct = gp.assignedPct;
  bool blocked = gp.blocked;
  bool hasHeadroom = gp.hasHeadroom;
  String blockReason = gp.blockReason;
  if (!mcGroupParticipantGetLiveReleaseState(gp, actualPct, blocked, hasHeadroom, blockReason)) return false;
  if ((actualPct + 0.2f) >= releasePct) return true;
  if (mcReleaseBypassAllowedByConfirmedLimit(actualPct,
                                             (float)gp.maxPercent,
                                             blocked,
                                             hasHeadroom,
                                             blockReason)) {
    return true;
  }
  return false;
}

static void mcBuildGroupRampUpDistribution(float totalPercent, const uint8_t idxs[MC_MAX_SLOTS], uint8_t count, float assigned[MC_MAX_SLOTS]) {
  mcZeroGroupDistribution(assigned);
  if (totalPercent <= 0.0f || count == 0) return;

  uint8_t ordered[MC_MAX_SLOTS] = {0, 0, 0};
  for (uint8_t i = 0; i < count; i++) ordered[i] = idxs[i];
  mcSortGroupParticipantsByOrder(ordered, count, true);

  uint8_t unlockedCount = 1;
  while (unlockedCount < count) {
    const MultiControllerGroupParticipantRuntime& previousGp = mcGroupModel[ordered[unlockedCount - 1U]];
    if (!mcGroupParticipantReachedReleaseThreshold(previousGp, true)) break;
    unlockedCount++;
  }

  float remaining = totalPercent;
  for (uint8_t pos = 0; pos < unlockedCount && remaining > 0.0f; pos++) {
    const MultiControllerGroupParticipantRuntime& gp = mcGroupModel[ordered[pos]];
    const bool hasNext = (pos + 1U) < count;
    float cap = mcGroupStageOneCapForParticipant(gp, hasNext);
    if (cap < 0.0f) cap = 0.0f;

    float assign = 0.0f;
    if (pos == 0) {
      assign = min(remaining, cap);
    } else {
      if (remaining < (float)gp.minPercent) continue;
      assign = min(remaining, cap);
      if (assign > 0.0f && assign < (float)gp.minPercent) assign = min((float)gp.minPercent, cap);
    }

    if (assign > remaining) assign = remaining;
    if (assign < 0.0f) assign = 0.0f;
    assigned[ordered[pos]] = assign;
    remaining -= assign;
  }

  if (remaining <= 0.0f) return;

  uint8_t extraCount = unlockedCount;
  if (extraCount < count && extraCount > 0) extraCount--;

  for (uint8_t pos = 0; pos < extraCount && remaining > 0.0f; pos++) {
    const MultiControllerGroupParticipantRuntime& gp = mcGroupModel[ordered[pos]];
    float extraCap = (float)gp.maxPercent - assigned[ordered[pos]];
    if (extraCap <= 0.0f) continue;
    float add = min(remaining, extraCap);
    assigned[ordered[pos]] += add;
    remaining -= add;
  }
}

static void mcBuildGroupRampDownDistribution(float totalPercent, const uint8_t idxs[MC_MAX_SLOTS], uint8_t count, float assigned[MC_MAX_SLOTS]) {
  mcZeroGroupDistribution(assigned);
  if (count == 0) return;

  uint8_t ordered[MC_MAX_SLOTS] = {0, 0, 0};
  float currentTotal = 0.0f;
  for (uint8_t i = 0; i < count; i++) {
    ordered[i] = idxs[i];
    float pct = mcGroupModel[idxs[i]].assignedPct;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > (float)mcGroupModel[idxs[i]].maxPercent) pct = (float)mcGroupModel[idxs[i]].maxPercent;
    assigned[idxs[i]] = pct;
    currentTotal += pct;
  }

  if (currentTotal <= totalPercent) {
    mcBuildGroupRampUpDistribution(totalPercent, idxs, count, assigned);
    return;
  }

  mcSortGroupParticipantsByOrder(ordered, count, false);
  float excess = currentTotal - totalPercent;
  for (uint8_t i = 0; i < count && excess > 0.0f; i++) {
    const float removable = assigned[ordered[i]];
    if (removable <= 0.0f) continue;
    const float take = min(excess, removable);
    assigned[ordered[i]] -= take;
    excess -= take;
  }

  if (excess > 0.01f) mcBuildGroupRampUpDistribution(totalPercent, idxs, count, assigned);
}


void mcApplyMasterGroupPhaseGuardToPlannerTargets(uint8_t scenario) {
  (void)scenario;
  if (!(mcEnabled && phaseGuardEnabled)) return;
  if (!(mcRole == MC_ROLE_MASTER && mcModeUsesNetwork() && mcHasConfiguredNetworkPeers())) return;
  if (!mcGroupPriorityPlannerActive) return;

  // Stap 5 cleanup:
  // de master gebruikt deze guard nu alleen nog als lichte netwerk-writeback gate
  // voor REMOTE deelnemers. Lokale master-slots blijven daarna onder hun eigen
  // lokale fase/loadbalancinglaag vallen. Zo voorkomen we dubbele begrenzing op
  // dezelfde lokale output en blijft de regelcyclus sneller en voorspelbaarder.
  float phaseTargetRemote[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float phaseCurrentRemote[4] = {0.0f, 0.0f, 0.0f, 0.0f};

  for (uint8_t i = 0; i < mcGroupModelCount; i++) {
    const MultiControllerGroupParticipantRuntime& gp = mcGroupModel[i];
    if (!gp.used || !gp.controllable || gp.local) continue;
    const uint8_t ph = mcClampAssignedPhase(gp.phase);
    float targetPct = gp.targetPct;
    if (targetPct < 0.0f) targetPct = 0.0f;
    phaseTargetRemote[ph] += targetPct;

    float actualPct = gp.assignedPct;
    if (actualPct < 0.0f) actualPct = 0.0f;
    if (actualPct > (float)gp.maxPercent) actualPct = (float)gp.maxPercent;
    phaseCurrentRemote[ph] += actualPct;
  }

  static constexpr float kReserveA = 0.3f;

  if (!phaseBudgetSnapshotFresh(PHASES_STALE_MS)) {
    for (uint8_t i = 0; i < mcGroupModelCount; i++) {
      MultiControllerGroupParticipantRuntime& gp = mcGroupModel[i];
      if (!gp.used || !gp.controllable || gp.local) continue;
      const float actualPct = max(0.0f, gp.assignedPct);
      if (gp.targetPct > actualPct) gp.targetPct = actualPct;
      gp.blocked = true;
      gp.hasHeadroom = false;
      gp.blockReason = "Geen verse fasegegevens op master; remote target-groei tijdelijk geblokkeerd.";
    }
    return;
  }

  for (uint8_t ph = 1; ph <= 3; ph++) {
    const float phasePlanned = phaseTargetRemote[ph];
    if (phasePlanned <= 0.0f) continue;

    const float currentAssigned = phaseCurrentRemote[ph];
    float phaseAllowed = phasePlanned;
    const float a = mcGetMeasuredPhaseCurrentA(ph);
    const float maxA = getConfiguredPhaseGuardMaxA(ph);

    if (!isfinite(a) || !isfinite(maxA) || maxA <= 0.0f) {
      phaseAllowed = currentAssigned;
    } else if (a >= (maxA - kReserveA) && phaseAllowed > (currentAssigned + 0.05f)) {
      // Alleen extra opregelen blokkeren op remote netwerkdeelnemers.
      // Actieve neerwaartse correctie blijft bewust lokaal: op de master voor
      // lokale slots en op de slave als noodrem voor remote slots.
      phaseAllowed = currentAssigned;
    }

    if (phaseAllowed < 0.0f) phaseAllowed = 0.0f;
    if (phaseAllowed + 0.05f >= phasePlanned || phasePlanned <= 0.0f) continue;

    const float scale = phaseAllowed / phasePlanned;
    for (uint8_t i = 0; i < mcGroupModelCount; i++) {
      MultiControllerGroupParticipantRuntime& gp = mcGroupModel[i];
      if (!gp.used || !gp.controllable || gp.local) continue;
      if (mcClampAssignedPhase(gp.phase) != ph) continue;
      gp.targetPct *= scale;
      if (gp.targetPct < 0.0f) gp.targetPct = 0.0f;
      gp.blocked = true;
      gp.hasHeadroom = false;
      gp.blockReason = String("Master-fase L") + String(ph) + " houdt remote target-groei tegen.";
    }
  }
}

void mcPlanGroupPriorityTargets(float totalPercent, uint8_t scenario) {
  mcGroupPriorityPlannerActive = false;
  mcGroupPriorityPlannerReason = "Groepsbrede prioriteitsplanner nog niet actief.";
  for (uint8_t i = 0; i < mcGroupModelCount; i++) mcGroupModel[i].targetPct = mcGroupModel[i].requestedPct;

  if (!mcEnabled) {
    mcGroupPriorityPlannerReason = "Multi Controller staat uit; groepsplanner blijft uit.";
    return;
  }
  if (!mcModeUsesNetwork()) {
    mcGroupPriorityPlannerReason = "Geen netwerkmodus; lokale verdeling blijft ongewijzigd.";
    return;
  }
  if (mcRole == MC_ROLE_SLAVE) {
    mcGroupPriorityPlannerReason = "Slave-modus: deze ESP plant niet zelf; master bepaalt de doelen.";
    return;
  }
  if (!mcHasConfiguredNetworkPeers() || mcGroupModelRemoteCount == 0) {
    mcGroupPriorityPlannerReason = (mcRole == MC_ROLE_MASTER)
      ? "Master-modus zonder actieve netwerk-slots; groepsplanner plant alleen lokale outputs."
      : "Nog geen bruikbare remote deelnemers; groepsplanner wacht op online peers.";
    return;
  }
  uint8_t effectiveScenario = mcResolveEffectiveScenario(totalPercent, scenario);
  uint8_t eligible[MC_MAX_SLOTS] = {0, 0, 0};
  const uint8_t eligibleCount = mcCollectScenarioEligibleGroupParticipants(effectiveScenario, eligible);
  if (eligibleCount == 0) {
    mcGroupPriorityPlannerReason = "Geen scenario-geschikte groepsdeelnemers gevonden voor de planner.";
    return;
  }

  float requested = totalPercent;
  float totalMax = 0.0f;
  for (uint8_t i = 0; i < eligibleCount; i++) totalMax += (float)mcGroupModel[eligible[i]].maxPercent;
  if (requested > totalMax) requested = totalMax;
  if (requested < 0.0f) requested = 0.0f;

  float planned[MC_MAX_SLOTS] = {0.0f, 0.0f, 0.0f};
  if (eligibleCount <= 1) {
    planned[eligible[0]] = requested;
  } else if (mcScenarioUsesNetworkFullParticipantMax(effectiveScenario)) {
    // Belangrijk voor netwerk-peers in netwerkmodus:
    // bij handmatig forceren en prioriteitswarmte moet elke deelnemende controller
    // zich gedragen alsof hij lokaal zelf in die modus staat.
    // Dus NIET de groepsvraag verdelen over master + peers, maar per deelnemer het
    // eigen ingestelde slot-maximum aanvragen. De bestaande lokale veiligheidslagen
    // (fasebewaking, softstart, caps) blijven daarna op elke ESP afzonderlijk leidend.
    for (uint8_t i = 0; i < eligibleCount; i++) {
      const uint8_t idx = eligible[i];
      float slotMax = (float)mcGroupModel[idx].maxPercent;
      if (slotMax < 0.0f) slotMax = 0.0f;
      planned[idx] = slotMax;
    }
  } else if (mcScenarioUsesParallelPriorityTargets(effectiveScenario)) {
    mcBuildGroupParallelPriorityDistribution(requested, effectiveScenario, eligible, eligibleCount, planned);
  } else {
    float currentTotal = 0.0f;
    for (uint8_t i = 0; i < eligibleCount; i++) currentTotal += mcGroupModel[eligible[i]].assignedPct;
    if (requested + 0.05f < currentTotal) mcBuildGroupRampDownDistribution(requested, eligible, eligibleCount, planned);
    else mcBuildGroupRampUpDistribution(requested, eligible, eligibleCount, planned);
  }

  for (uint8_t i = 0; i < mcGroupModelCount; i++) mcGroupModel[i].targetPct = 0.0f;
  for (uint8_t i = 0; i < eligibleCount; i++) {
    uint8_t idx = eligible[i];
    mcGroupModel[idx].targetPct = planned[idx];
  }
  mcGroupPriorityPlannerActive = true;
  if (mcScenarioUsesNetworkFullParticipantMax(effectiveScenario)) {
    mcGroupPriorityPlannerReason = "Groepsbrede force/prioriteitsaansturing actief: iedere toegestane lokale en remote deelnemer krijgt nu zijn eigen ingestelde max; lokale veiligheidslagen blijven leidend.";
  } else {
    mcGroupPriorityPlannerReason = "Groepsbrede prioriteitsplanner actief: de bekende opregel-/terugregelvolgorde wordt nu met de vereenvoudigde netwerk-achteraan keten over lokale en bruikbare remote deelnemers berekend; remote targets worden daarna teruggeschreven naar peers.";
  }
}

void mcBuildDesiredDistribution(float totalPercent, uint8_t scenario, float assigned[MC_MAX_SLOTS]) {
  mcZeroDistribution(assigned);

  mcCurrentScenario = mcResolveEffectiveScenario(totalPercent, scenario);
  mcCurrentScenarioLabel = mcScenarioLabel(mcCurrentScenario);
  mcNegativePriorityOverrideActive = false;

  if (totalPercent <= 0.0f) return;

  uint8_t eligible[MC_MAX_SLOTS] = {0, 0, 0};
  const uint8_t eligibleCount = mcCollectScenarioEligibleLocalSlots(mcCurrentScenario, eligible);
  if (eligibleCount == 0) return;

  if (mcFallbackActive || eligibleCount <= 1) {
    assigned[eligible[0]] = totalPercent;
    return;
  }

  if (mcScenarioUsesParallelPriorityTargets(mcCurrentScenario)) {
    mcBuildParallelPriorityTargetDistribution(totalPercent, mcCurrentScenario, eligible, eligibleCount, assigned);
    return;
  }

  float currentTotal = 0.0f;
  for (uint8_t i = 0; i < eligibleCount; i++) currentTotal += mcRuntime[eligible[i]].percent;

  if (totalPercent + 0.05f < currentTotal) {
    mcBuildRampDownDistribution(totalPercent, eligible, eligibleCount, assigned);
  } else if (mcEnabled && phaseGuardEnabled && mcCurrentScenario == MC_SCENARIO_SURPLUS) {
    // Alleen bij Multi Controller + zonne-overschot maken we de cascade fasebewust.
    // Als een eerdere controller tijdelijk door fasebeveiliging "vol" zit, mag de
    // resterende zonne-ruimte naar volgende controllers op andere fases doorstromen.
    mcBuildRampUpDistributionPhaseAware(totalPercent, mcCurrentScenario, eligible, eligibleCount, assigned);
  } else {
    mcBuildRampUpDistribution(totalPercent, eligible, eligibleCount, assigned);
  }
}

static bool mcShouldApplyPerControllerPhaseGuard(uint8_t scenario) {
  (void)scenario;
  if (!(mcEnabled && phaseGuardEnabled)) return false;

  // In netwerk-slave-modus bewaakt de master de fasegrenzen en verdeelt hij
  // de groeps-PWM. De slave mag het ontvangen target dan niet nogmaals lokaal
  // afknijpen met een eigen per-fase cap-state, anders kan hij onterecht op
  // bijvoorbeeld 40% blijven hangen terwijl de master nog ruimte ziet.
  if (mcRole == MC_ROLE_SLAVE && mcModeUsesNetwork() && mcHasConfiguredNetworkPeers() && !multiControllerSlaveLegionellaOverrideActive()) {
    return false;
  }

  return true;
}

void mcApplyPerControllerPhaseGuard(uint8_t scenario, float assigned[MC_MAX_SLOTS]) {
  if (!mcShouldApplyPerControllerPhaseGuard(scenario)) {
    mcResetPerPhaseGuardState();
    return;
  }

  float phaseAssigned[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float phaseMax[4] = {0.0f, 0.0f, 0.0f, 0.0f};

  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (!mcRuntime[i].localEnabled || !mcRuntime[i].attached) continue;
    if (!mcIsSlotAllowedForScenario(i, scenario)) continue;
    const uint8_t ph = mcClampAssignedPhase(mcSlots[i].assignedPhase);
    phaseAssigned[ph] += assigned[i];
    float slotMax = (float)mcSlots[i].maxPercent;
    if (slotMax < 0.0f) slotMax = 0.0f;
    phaseMax[ph] += slotMax;
  }

  const uint32_t now = millis();
  if (!lastPhasesOk) {
    for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) assigned[i] = 0.0f;
    for (uint8_t ph = 1; ph <= 3; ph++) {
      mcPhaseGuardOverCount[ph] = 0;
      mcPhaseGuardCapPct[ph] = 0.0f;
      mcPhaseGuardLastCapMaxPct[ph] = phaseMax[ph];
      mcPhaseGuardLastStepMs[ph] = now;
    }
    return;
  }

  static const float MARGIN_A = 0.3f;
  static const uint8_t OVER_N = 3;
  static const float STEP_DOWN_PCT = 5.0f;
  static const float STEP_UP_PCT = 5.0f;

  for (uint8_t ph = 1; ph <= 3; ph++) {
    const float capMaxPercent = phaseMax[ph];
    float currentPhaseAssigned = 0.0f;
    for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
      if (!mcRuntime[i].localEnabled || !mcRuntime[i].attached) continue;
      if (!mcIsSlotAllowedForScenario(i, scenario)) continue;
      if (mcClampAssignedPhase(mcSlots[i].assignedPhase) != ph) continue;
      currentPhaseAssigned += mcRuntime[i].percent;
    }
    if (capMaxPercent <= 0.0f) {
      mcPhaseGuardOverCount[ph] = 0;
      mcPhaseGuardCapPct[ph] = 0.0f;
      mcPhaseGuardLastCapMaxPct[ph] = 0.0f;
      mcPhaseGuardLastStepMs[ph] = now;
      continue;
    }

    if (fabsf(mcPhaseGuardLastCapMaxPct[ph] - capMaxPercent) > 0.01f) {
      if (capMaxPercent > mcPhaseGuardLastCapMaxPct[ph]) {
        mcPhaseGuardCapPct[ph] = capMaxPercent;
      } else if (mcPhaseGuardCapPct[ph] > capMaxPercent) {
        mcPhaseGuardCapPct[ph] = capMaxPercent;
      }
      if (mcPhaseGuardCapPct[ph] <= 0.0f) mcPhaseGuardCapPct[ph] = capMaxPercent;
      mcPhaseGuardLastCapMaxPct[ph] = capMaxPercent;
    }

    float phaseTarget = phaseAssigned[ph];
    if (phaseTarget <= 0.0f) {
      if (mcPhaseGuardCapPct[ph] <= 0.0f) mcPhaseGuardCapPct[ph] = capMaxPercent;
      continue;
    }

    const float a = mcGetMeasuredPhaseCurrentA(ph);
    const float maxA = getConfiguredPhaseGuardMaxA(ph);
    if (!isfinite(a) || !isfinite(maxA) || maxA <= 0.0f) {
      for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
        if (mcClampAssignedPhase(mcSlots[i].assignedPhase) == ph) assigned[i] = 0.0f;
      }
      mcPhaseGuardOverCount[ph] = 0;
      mcPhaseGuardCapPct[ph] = 0.0f;
      mcPhaseGuardLastCapMaxPct[ph] = capMaxPercent;
      mcPhaseGuardLastStepMs[ph] = now;
      continue;
    }

    // Als deze fase al op/over de limiet zit, mag de totale PWM op deze fase
    // niet verder omhoog. Bestaande PWM mag wel door de limiter worden verlaagd.
    if (a >= maxA && phaseTarget > (currentPhaseAssigned + 0.05f)) {
      phaseTarget = currentPhaseAssigned;
    }

    if (a > maxA) {
      phaseTarget *= (maxA / a);
    }

    if ((uint32_t)(now - mcPhaseGuardLastStepMs[ph]) >= 1000UL) {
      const bool over = (a > (maxA + MARGIN_A));
      const bool under = (a < (maxA - MARGIN_A));

      if (over) {
        if (mcPhaseGuardOverCount[ph] < 255) mcPhaseGuardOverCount[ph]++;
        if (mcPhaseGuardOverCount[ph] >= OVER_N) {
          mcPhaseGuardCapPct[ph] -= STEP_DOWN_PCT;
          if (mcPhaseGuardCapPct[ph] < 0.0f) mcPhaseGuardCapPct[ph] = 0.0f;
          mcPhaseGuardOverCount[ph] = OVER_N;
        }
      } else {
        mcPhaseGuardOverCount[ph] = 0;
        if (under && mcPhaseGuardCapPct[ph] < capMaxPercent) {
          if (capMaxPercent > 100.0f) mcPhaseGuardCapPct[ph] = capMaxPercent;
          else {
            mcPhaseGuardCapPct[ph] += STEP_UP_PCT;
            if (mcPhaseGuardCapPct[ph] > capMaxPercent) mcPhaseGuardCapPct[ph] = capMaxPercent;
          }
        }
      }

      mcPhaseGuardLastStepMs[ph] = now;
    }

    if (mcPhaseGuardCapPct[ph] > 0.0f && phaseTarget > mcPhaseGuardCapPct[ph]) {
      phaseTarget = mcPhaseGuardCapPct[ph];
    }

    if (phaseTarget < 0.0f) phaseTarget = 0.0f;

    if (phaseTarget + 0.05f < phaseAssigned[ph] && phaseAssigned[ph] > 0.0f) {
      const float scale = phaseTarget / phaseAssigned[ph];
      for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
        if (!mcRuntime[i].localEnabled || !mcRuntime[i].attached) continue;
        if (mcClampAssignedPhase(mcSlots[i].assignedPhase) != ph) continue;
        assigned[i] *= scale;
        if (assigned[i] < 0.0f) assigned[i] = 0.0f;
      }
    }
  }
}

