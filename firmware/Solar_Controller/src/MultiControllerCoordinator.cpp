#include "MultiController.h"
#include "MultiControllerInternal.h"
#include "MultiControllerTcpLink.h"
#include "app_globals.h"

#include <WiFi.h>

namespace {
uint32_t mcNextNetworkServiceDueMs = 0;
uint32_t mcNextFailSafeDueMs = 0;
bool mcTcpLinkActive = false;

static void mcRunFastCoordinatorPass(bool slaveRole) {
  mcUpdateInternalCoordinatorState();
  mcRebuildGroupModel();

  if (slaveRole) {
    mcGroupPriorityPlannerActive = false;
    mcGroupPriorityPlannerReason = "Slave-modus: deze ESP wacht op master-doelen via de persistente TCP-link en plant niet zelf.";
    if (!mcHasConfiguredNetworkPeers()) {
      mcClearRemoteTargets("Slave heeft nog geen master-IP geconfigureerd.");
    } else if (!mcHasFreshRemoteTargets() && mcRemoteTargetsReceivedMs == 0) {
      mcRemoteTargetsReason = "Slave wacht op eerste master-doelen via TCP.";
    }
    return;
  }

  mcPlanGroupPriorityTargets(mcRequestedGroupPercent, mcCurrentScenario);
}

static bool mcRunNetworkServiceIfDue(uint32_t now, bool slaveRole) {
  if (!mcEnabled || !mcModeUsesNetwork() || !mcHasConfiguredNetworkPeers()) return false;
  if (mcNextNetworkServiceDueMs != 0 && (int32_t)(now - mcNextNetworkServiceDueMs) < 0) return false;
  mcNextNetworkServiceDueMs = now + MC_NETWORK_SERVICE_INTERVAL_MS;
  mcTcpLinkLoop(now, slaveRole);
  return multiControllerTcpRealtimeAvailable();
}

static void mcRunFailSafeIfDue(uint32_t now, bool forceNow = false) {
  if (!forceNow && mcNextFailSafeDueMs != 0 && (int32_t)(now - mcNextFailSafeDueMs) < 0) return;
  mcNextFailSafeDueMs = now + MC_FAILSAFE_HOUSEKEEPING_INTERVAL_MS;
  mcRunFailSafeHousekeeping();
}
}

void multiControllerLoop() {
  if (!mcEnabled) {
    mcNextNetworkServiceDueMs = 0;
    mcNextFailSafeDueMs = 0;
    mcClearRemoteTargets("Multi Controller staat uit.");
    if (mcTcpLinkActive) {
      mcTcpLinkShutdown();
      mcTcpLinkActive = false;
    }
    for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
      mcResetPeerRuntime(i);
    }
    mcUpdateInternalCoordinatorState();
    mcRebuildGroupModel();
    mcPlanGroupPriorityTargets(mcRequestedGroupPercent, mcCurrentScenario);
    return;
  }

  const bool slaveRole = (mcRole == MC_ROLE_SLAVE);
  bool anyPeer = false;
  for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
    if (i >= mcControllerCount) {
      mcResetPeerRuntime(i);
      continue;
    }
    if (mcSlotRequestsNetworkPeer(i)) {
      anyPeer = true;
      mcPeerRuntime[i].configured = true;
    } else {
      mcResetPeerRuntime(i);
    }
  }

  if (!anyPeer) {
    mcNextNetworkServiceDueMs = 0;
    mcNextFailSafeDueMs = 0;
    if (mcTcpLinkActive) {
      mcTcpLinkShutdown();
      mcTcpLinkActive = false;
    }
    mcUpdateInternalCoordinatorState();
    mcRunFailSafeHousekeeping();
    mcRebuildGroupModel();
    mcPlanGroupPriorityTargets(mcRequestedGroupPercent, mcCurrentScenario);
    return;
  }

  if (!mcTcpLinkActive) {
    mcTcpLinkInit();
    mcTcpLinkActive = true;
  }

  const uint32_t now = millis();

  mcRunFailSafeIfDue(now, false);
  mcRunFastCoordinatorPass(slaveRole);

  if (mcRunNetworkServiceIfDue(now, slaveRole)) {
    const uint32_t afterNetworkMs = millis();
    mcRunFailSafeIfDue(afterNetworkMs, true);
    mcRunFastCoordinatorPass(slaveRole);
  }
}
