#include "MultiController.h"
#include "MultiControllerInternal.h"

uint8_t multiControllerGetRequestedLocalPwmCount() {
  return mcRequestedLocalPwmCount;
}

uint8_t multiControllerGetEffectiveLocalPwmCount() {
  return mcEffectiveLocalPwmCount;
}

uint8_t multiControllerGetActiveLocalPwmCount() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (!mcRuntime[i].localEnabled) continue;
    if (!mcRuntime[i].attached) continue;
    count++;
  }
  return count;
}

bool multiControllerIsLocalPwmSlotConfigured(uint8_t slotIndex) {
  return mcSlotUsesLocalPwmRequested(slotIndex);
}

bool multiControllerIsLocalPwmSlotActive(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return false;
  return mcRuntime[slotIndex].localEnabled;
}

bool multiControllerIsLocalPwmSlotAttached(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return false;
  return mcRuntime[slotIndex].attached;
}

int multiControllerGetLocalPwmSlotPin(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return -1;
  return mcRuntime[slotIndex].effectivePin;
}

uint8_t multiControllerGetLocalPwmSlotChannel(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return 0;
  return mcRuntime[slotIndex].ledcChannel;
}

uint8_t multiControllerGetSlotAssignedPhase(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return 1;
  return mcClampAssignedPhase(mcSlots[slotIndex].assignedPhase);
}

void multiControllerSetSlotAssignedPhase(uint8_t slotIndex, uint8_t phase123) {
  if (slotIndex >= MC_MAX_SLOTS) return;
  mcSlots[slotIndex].assignedPhase = mcClampAssignedPhase(phase123);
}


float multiControllerGetLocalPwmSlotRequestedPercent(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return 0.0f;
  return mcRuntime[slotIndex].requestedPercent;
}

float multiControllerGetLocalPwmSlotAllowedPercent(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return 0.0f;
  return mcRuntime[slotIndex].allowedPercent;
}

float multiControllerGetLocalPwmSlotPercent(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return 0.0f;
  return mcRuntime[slotIndex].percent;
}

float multiControllerGetLocalPwmSlotHeadroomPercent(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return 0.0f;
  return mcRuntime[slotIndex].headroomPercent;
}

bool multiControllerIsLocalPwmSlotBlocked(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return false;
  return mcRuntime[slotIndex].blocked;
}

bool multiControllerLocalSlotHasHeadroom(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return false;
  return mcRuntime[slotIndex].hasHeadroom;
}

String multiControllerGetLocalPwmSlotBlockReason(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return "";
  return mcRuntime[slotIndex].blockReason;
}

float multiControllerGetRequestedGroupPercent() {
  return mcRequestedGroupPercent;
}

float multiControllerGetAllowedGroupPercent() {
  return mcAssignedGroupPercent;
}

float multiControllerGetAssignedGroupPercent() {
  return mcAssignedGroupPercent;
}

uint8_t multiControllerGetScenario() {
  return mcCurrentScenario;
}

String multiControllerGetScenarioLabel() {
  return mcCurrentScenarioLabel;
}

bool multiControllerIsNegativePriorityOverrideActive() {
  return false;
}

bool multiControllerShouldUseGroupControlForSurplus() {
  return multiControllerShouldUseGroupControlForScenario(MC_SCENARIO_SURPLUS);
}

bool multiControllerShouldUseGroupControlForScenario(uint8_t scenario) {
  if (!mcEnabled) return false;
  if (mcFallbackActive) return false;

  // Stap 7 master-slave: op een master met geconfigureerde netwerk-slots moet ook
  // NORMAL / zonne-overschot altijd via de groepsplanner lopen. Anders blijft de
  // master zijn lokale output nog als standalone controller behandelen en lopen de
  // slaves niet netjes mee op basis van de master-planning.
  if (multiControllerIsMasterRole() && mcModeUsesNetwork() && mcHasConfiguredNetworkPeers()) {
    return true;
  }

  if (!(mcMode == MC_MODE_LOCAL_PWM || mcMode == MC_MODE_HYBRID)) return false;
  if (mcEffectiveLocalPwmCount <= 1) return false;

  uint8_t eligible[MC_MAX_SLOTS] = {0, 0, 0};
  const uint8_t eligibleCount = mcCollectScenarioEligibleLocalSlots(mcResolveEffectiveScenario(1.0f, scenario), eligible);
  return eligibleCount > 1;
}

bool multiControllerScenarioUsesParallelPriorityTargets(uint8_t scenario) {
  return mcScenarioUsesParallelPriorityTargets(scenario);
}

float multiControllerGetSurplusGroupMaxPercent() {
  return mcGetScenarioEligibleMaxPercent(MC_SCENARIO_SURPLUS);
}

float multiControllerGetScenarioGroupMaxPercent(uint8_t scenario) {
  return mcGetScenarioEligibleMaxPercent(mcResolveEffectiveScenario(1.0f, scenario));
}

float multiControllerGetScenarioSoftStartGroupStartPercent(uint8_t scenario, float startPctPerController) {
  uint8_t eligible[MC_MAX_SLOTS] = {0, 0, 0};
  const uint8_t effectiveScenario = mcResolveEffectiveScenario(1.0f, scenario);
  const uint8_t eligibleCount = mcCollectScenarioEligibleLocalSlots(effectiveScenario, eligible);
  if (eligibleCount == 0) return 0.0f;

  float startPct = startPctPerController;
  if (startPct < 0.0f) startPct = 0.0f;

  float total = 0.0f;
  for (uint8_t i = 0; i < eligibleCount; i++) {
    const uint8_t slot = eligible[i];
    float slotMax = (float)mcSlots[slot].maxPercent;
    if (slotMax < 0.0f) slotMax = 0.0f;
    total += min(startPct, slotMax);
  }
  return total;
}

float multiControllerGetPrimaryAssignedPercent() {
  return mcRuntime[0].percent;
}

bool multiControllerIsConfigValid() {
  return mcConfigValid;
}

bool multiControllerIsRuntimeValid() {
  return mcRuntimeValid;
}

bool multiControllerIsFallbackActive() {
  return mcFallbackActive;
}

String multiControllerGetFallbackReason() {
  return mcFallbackReason;
}

bool multiControllerHasNetworkPeersConfigured() {
  return mcHasConfiguredNetworkPeers();
}


bool multiControllerIsInternalCoordinator() {
  mcUpdateInternalCoordinatorState();
  return mcInternalCoordinator;
}

String multiControllerGetInternalCoordinatorReason() {
  mcUpdateInternalCoordinatorState();
  return mcInternalCoordinatorReason;
}

uint8_t multiControllerGetGroupModelParticipantCount() {
  return mcGroupModelCount;
}

uint8_t multiControllerGetGroupModelLocalParticipantCount() {
  return mcGroupModelLocalCount;
}

uint8_t multiControllerGetGroupModelRemoteParticipantCount() {
  return mcGroupModelRemoteCount;
}

bool multiControllerIsGroupModelClamped() {
  return mcGroupModelClamped;
}

String multiControllerGetGroupModelReason() {
  return mcGroupModelReason;
}

uint8_t multiControllerGetConfiguredPeerCount() {
  uint8_t count = 0;
  for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
    if (!mcSlotRequestsNetworkPeer(i)) continue;
    count++;
  }
  return count;
}

uint8_t multiControllerGetOnlinePeerCount() {
  uint8_t count = 0;
  for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
    if (!mcSlotRequestsNetworkPeer(i)) continue;
    if (mcPeerRuntime[i].online) count++;
  }
  return count;
}

uint8_t multiControllerGetHealthyPeerCount() {
  uint8_t count = 0;
  for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
    if (!mcSlotRequestsNetworkPeer(i)) continue;
    if (mcPeerHasUsableCachedStatus(mcPeerRuntime[i], millis())) count++;
  }
  return count;
}

uint8_t multiControllerGetWritebackHealthyPeerCount() {
  const uint32_t now = millis();
  uint8_t count = 0;
  for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
    if (!mcSlotRequestsNetworkPeer(i)) continue;
    const MultiControllerPeerRuntime& pr = mcPeerRuntime[i];
    if (!mcPeerStillUsableForWriteback(pr, now)) continue;
    if (pr.lastWriteOkMs == 0) continue;
    if ((uint32_t)(now - pr.lastWriteOkMs) > (MC_INTERNAL_REMOTE_TARGET_TTL_MS + MC_INTERNAL_PEER_POLL_INTERVAL_MS)) continue;
    count++;
  }
  return count;
}


bool multiControllerApplyRemoteTargetsDocument(const DynamicJsonDocument& doc, String& errOut) {
  errOut = "";
  if (!mcEnabled) {
    errOut = "mc_disabled";
    return false;
  }
  const String incomingGroup = doc["group_name"].as<String>();
  if (incomingGroup.length() == 0) {
    errOut = "missing_group_name";
    return false;
  }
  if (mcGroupName.length() && incomingGroup != mcGroupName) {
    errOut = "group_name_mismatch";
    return false;
  }
  const String fromIp = doc["from_ip"].as<String>();
  if (mcModeUsesNetwork() && mcHasConfiguredNetworkPeers()) {
    if (fromIp.length() == 0) {
      errOut = "missing_from_ip";
      return false;
    }
    if (mcRole != MC_ROLE_SLAVE) {
      errOut = "remote_targets_only_for_slave";
      return false;
    }
    const String masterIp = mcGetMasterLinkIp();
    if (masterIp.length() == 0) {
      errOut = "missing_master_ip";
      return false;
    }
    if (fromIp != masterIp) {
      errOut = "from_ip_not_master";
      return false;
    }
  }
  JsonArrayConst targets = doc["targets"].as<JsonArrayConst>();
  if (targets.isNull() || targets.size() == 0) {
    errOut = "missing_targets";
    return false;
  }
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) mcRemoteTargetsPerSlot[i] = 0.0f;
  for (JsonObjectConst t : targets) {
    uint8_t slot = t["slot"] | 0;
    if (slot < 1 || slot > MC_MAX_SLOTS) continue;
    const uint8_t idx = slot - 1U;
    if (!mcSlotUsesLocalPwmRequested(idx)) continue;
    float targetPct = t["target_pct"] | 0.0f;
    if (targetPct < 0.0f) targetPct = 0.0f;
    if (targetPct > (float)mcSlots[idx].maxPercent) targetPct = (float)mcSlots[idx].maxPercent;
    mcRemoteTargetsPerSlot[idx] = targetPct;
  }
  mcRemoteTargetsActive = true;
  mcRemoteTargetsReceivedMs = millis();
  mcRemoteTargetsTtlMs = mcClampRemoteTargetTtlMs(doc["ttl_ms"] | MC_REMOTE_TARGET_TTL_MS);
  mcRemoteTargetsScenario = doc["scenario"] | MC_SCENARIO_IDLE;
  mcRemoteTargetsRequestedGroupPercent = doc["requested_group_pct"] | 0.0f;
  mcRemoteTargetsOwnerIp = fromIp;
  mcRemoteTargetsReason = String("Ontvangen van ") + (mcRemoteTargetsOwnerIp.length() ? mcRemoteTargetsOwnerIp : String("master"));
  if (mcRole == MC_ROLE_SLAVE && mcControllerCount >= 2 && fromIp == mcSlots[1].peerIp) {
    MultiControllerPeerRuntime& pr = mcPeerRuntime[1];
    pr.configured = true;
    pr.online = true;
    pr.statusOk = true;
    pr.lastOkMs = mcRemoteTargetsReceivedMs;
    pr.lastWriteOkMs = mcRemoteTargetsReceivedMs;
    pr.lastError = "";
    pr.lastHttpCodeWrite = 200;
    pr.nextPollDueMs = mcRemoteTargetsReceivedMs + MC_PEER_POLL_INTERVAL_MS;
  }
  return true;
}

bool multiControllerIsMasterRole() {
  return (mcEnabled && mcRole == MC_ROLE_MASTER);
}

bool multiControllerIsSlaveRole() {
  return (mcEnabled && mcRole == MC_ROLE_SLAVE);
}

String multiControllerGetSlaveMasterIp() {
  if (!(mcEnabled && mcRole == MC_ROLE_SLAVE)) return String("");
  if (mcControllerCount < 2) return String("");
  return mcSlots[1].peerIp;
}

bool multiControllerIsRemoteTargetControlActive() {
  if (multiControllerSlaveLegionellaOverrideActive()) return false;
  return mcHasFreshRemoteTargets();
}

uint32_t multiControllerGetRemoteTargetAgeMs() {
  if (multiControllerSlaveLegionellaOverrideActive()) return 0;
  if (!mcHasFreshRemoteTargets()) return 0;
  return (millis() - mcRemoteTargetsReceivedMs);
}

String multiControllerGetRemoteTargetReason() {
  if (multiControllerSlaveLegionellaOverrideActive()) return String("Lokaal overruld door legionella.");
  return mcRemoteTargetsReason;
}

String multiControllerGetRemoteTargetOwnerIp() {
  return mcRemoteTargetsOwnerIp;
}
