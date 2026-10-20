#include "MultiController.h"
#include "MultiControllerInternal.h"
#include "app_globals.h"

#include <math.h>

bool mcEnabled = false;
uint8_t mcRole = MC_ROLE_STANDALONE;
uint8_t mcLastActiveRole = MC_ROLE_MASTER;
uint8_t mcMode = MC_MODE_DISABLED;
uint8_t mcControllerCount = 1;
uint8_t mcLocalControllerId = 1;
String mcGroupName = "solar-group";
bool mcSlaveAllowLocalLegionellaOverride = false;
MultiControllerSlotConfig mcSlots[MC_MAX_SLOTS];

MultiControllerSlotRuntime mcRuntime[MC_MAX_SLOTS];
MultiControllerPeerRuntime mcPeerRuntime[MC_MAX_SLOTS];
MultiControllerGroupParticipantRuntime mcGroupModel[MC_MAX_SLOTS];
uint8_t mcGroupModelCount = 0;
uint8_t mcGroupModelLocalCount = 0;
uint8_t mcGroupModelRemoteCount = 0;
uint8_t mcGroupModelDiscoveredCount = 0;
bool mcGroupModelClamped = false;
String mcGroupModelReason = "Alleen lokale outputs in groepsmodel.";
uint8_t mcPeerRoundRobinIndex = 1;
uint32_t mcRuntimePwmFreqHz = 1000;
uint8_t mcRuntimePwmResolutionBits = 8;

bool mcConfigValid = true;
bool mcRuntimeValid = true;
bool mcFallbackActive = false;
String mcFallbackReason = "Geen fallback actief";
uint8_t mcRequestedLocalPwmCount = 1;
uint8_t mcEffectiveLocalPwmCount = 0;
float mcRequestedGroupPercent = 0.0f;
float mcAssignedGroupPercent = 0.0f;
uint8_t mcCurrentScenario = MC_SCENARIO_IDLE;
String mcCurrentScenarioLabel = "idle";
bool mcNegativePriorityOverrideActive = false;
uint8_t mcPhaseGuardOverCount[4] = {0, 0, 0, 0};
float mcPhaseGuardCapPct[4] = {0.0f, 0.0f, 0.0f, 0.0f};
float mcPhaseGuardLastCapMaxPct[4] = {0.0f, 0.0f, 0.0f, 0.0f};
uint32_t mcPhaseGuardLastStepMs[4] = {0, 0, 0, 0};
uint8_t mcAppScenario = MC_SCENARIO_IDLE;
bool mcInternalCoordinator = true;
String mcInternalCoordinatorReason = "Lokale controller voert de groep lokaal uit.";
bool mcGroupPriorityPlannerActive = false;
String mcGroupPriorityPlannerReason = "Groepsbrede prioriteitsplanner nog niet actief.";
bool mcPriorityRampActive = false;
uint32_t mcPriorityRampStartMs = 0;
uint8_t mcPriorityRampScenario = MC_SCENARIO_IDLE;
uint8_t mcPriorityRampSlotsMask = 0;

bool mcRemoteTargetsActive = false;
uint32_t mcRemoteTargetsTtlMs = MC_REMOTE_TARGET_TTL_MS;
uint32_t mcRemoteTargetsReceivedMs = 0;
uint8_t mcRemoteTargetsScenario = MC_SCENARIO_IDLE;
float mcRemoteTargetsRequestedGroupPercent = 0.0f;
float mcRemoteTargetsPerSlot[MC_MAX_SLOTS] = {0.0f, 0.0f, 0.0f};
String mcRemoteTargetsOwnerIp = "";
String mcRemoteTargetsReason = "Geen remote targets actief.";

bool mcIsAllowedExtraPin(int pin) {
  for (size_t i = 0; i < 3; i++) {
    if (MC_EXTRA_PWM_PINS[i] == pin) return true;
  }
  return false;
}

int mcDefaultPinForSlot(uint8_t slotIndex) {
  if (slotIndex == 0) return MC_PRIMARY_PWM_PIN;
  uint8_t extraIndex = (slotIndex - 1);
  if (extraIndex < 3) return MC_EXTRA_PWM_PINS[extraIndex];
  return MC_EXTRA_PWM_PINS[0];
}

uint8_t mcClampAssignedPhase(uint8_t phase123) {
  return (phase123 >= 1 && phase123 <= 3) ? phase123 : phaseGuardPhase;
}

bool multiControllerSlaveLegionellaOverrideEnabled() {
  return mcSlaveAllowLocalLegionellaOverride && multiControllerIsSlaveRole() && mcHasConfiguredNetworkPeers();
}

bool multiControllerSlaveLegionellaOverrideActive() {
  return multiControllerSlaveLegionellaOverrideEnabled() && legRunning;
}

float mcGetMeasuredPhaseCurrentA(uint8_t phase123) {
  switch (phase123) {
    case 1: return lastPhaseI1A;
    case 2: return lastPhaseI2A;
    case 3: return lastPhaseI3A;
    default: return NAN;
  }
}

void mcResetPerPhaseGuardState() {
  const uint32_t now = millis();
  for (uint8_t ph = 1; ph <= 3; ph++) {
    mcPhaseGuardOverCount[ph] = 0;
    mcPhaseGuardCapPct[ph] = 0.0f;
    mcPhaseGuardLastCapMaxPct[ph] = 0.0f;
    mcPhaseGuardLastStepMs[ph] = now;
  }
}

void mcResetRuntimeSlot(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return;
  mcRuntime[slotIndex].localEnabled = false;
  mcRuntime[slotIndex].attached = false;
  mcRuntime[slotIndex].ledcChannel = slotIndex;
  mcRuntime[slotIndex].effectivePin = -1;
  mcRuntime[slotIndex].duty = 0;
  mcRuntime[slotIndex].requestedPercent = 0.0f;
  mcRuntime[slotIndex].allowedPercent = 0.0f;
  mcRuntime[slotIndex].percent = 0.0f;
  mcRuntime[slotIndex].headroomPercent = 0.0f;
  mcRuntime[slotIndex].blocked = false;
  mcRuntime[slotIndex].hasHeadroom = false;
  mcRuntime[slotIndex].blockReason = "";
}
