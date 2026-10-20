#include "MultiController.h"
#include "MultiControllerInternal.h"
#include "MultiControllerTcpLink.h"
#include "ControllerTemperatureGate.h"
#include "app_globals.h"

#include <WiFi.h>

void mcResetGroupParticipant(uint8_t idx) {
  if (idx >= MC_MAX_SLOTS) return;
  MultiControllerGroupParticipantRuntime& gp = mcGroupModel[idx];
  gp.used = false;
  gp.local = false;
  gp.online = false;
  gp.controllable = false;
  gp.sourceSlot = 0;
  gp.peerOutputSlot = 0;
  gp.controllerId = 0;
  gp.name = "";
  gp.peerIp = "";
  gp.phase = 1;
  gp.rampUpOrder = 0;
  gp.rampDownOrder = 0;
  gp.startAfterPercent = 0;
  gp.minPercent = 0;
  gp.maxPercent = 0;
  gp.useOnSurplus = false;
  gp.useOnCheap = false;
  gp.useOnNegative = false;
  gp.allowManualForce = false;
  gp.pin = -1;
  gp.channel = 0;
  gp.requestedPct = 0.0f;
  gp.assignedPct = 0.0f;
  gp.targetPct = 0.0f;
  gp.headroomPct = 0.0f;
  gp.blocked = false;
  gp.hasHeadroom = false;
  gp.blockReason = "";
}

static void mcResetGroupModel() {
  mcGroupModelCount = 0;
  mcGroupModelLocalCount = 0;
  mcGroupModelRemoteCount = 0;
  mcGroupModelDiscoveredCount = 0;
  mcGroupModelClamped = false;
  mcGroupModelReason = "Alleen lokale outputs in groepsmodel.";
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) mcResetGroupParticipant(i);
}

static bool mcGroupModelAddParticipant(const MultiControllerGroupParticipantRuntime& in) {
  mcGroupModelDiscoveredCount++;
  if (mcGroupModelCount >= MC_MAX_SLOTS) {
    mcGroupModelClamped = true;
    mcGroupModelReason = "Meer dan 3 groepsdeelnemers ontdekt; extra outputs worden in dit model nog genegeerd.";
    return false;
  }
  mcGroupModel[mcGroupModelCount] = in;
  mcGroupModel[mcGroupModelCount].used = true;
  if (in.local) mcGroupModelLocalCount++; else mcGroupModelRemoteCount++;
  mcGroupModelCount++;
  return true;
}

static void mcSortGroupModel() {
  for (uint8_t i = 0; i < mcGroupModelCount; i++) {
    for (uint8_t j = i + 1; j < mcGroupModelCount; j++) {
      const MultiControllerGroupParticipantRuntime& a = mcGroupModel[i];
      const MultiControllerGroupParticipantRuntime& b = mcGroupModel[j];
      bool swap = false;
      if (b.rampUpOrder < a.rampUpOrder) swap = true;
      else if (b.rampUpOrder == a.rampUpOrder) {
        if (b.controllerId < a.controllerId) swap = true;
        else if (b.controllerId == a.controllerId) {
          if (b.sourceSlot < a.sourceSlot) swap = true;
          else if (b.sourceSlot == a.sourceSlot && b.peerOutputSlot < a.peerOutputSlot) swap = true;
        }
      }
      if (swap) {
        MultiControllerGroupParticipantRuntime tmp = mcGroupModel[i];
        mcGroupModel[i] = mcGroupModel[j];
        mcGroupModel[j] = tmp;
      }
    }
  }
}

void mcRebuildGroupModel() {
  mcResetGroupModel();

  if (!mcEnabled) {
    mcGroupModelReason = "Multi Controller staat uit; groepsmodel blijft lokaal.";
    return;
  }

  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (i >= mcControllerCount) break;
    if (!mcSlotUsesLocalPwmRequested(i)) continue;

    MultiControllerGroupParticipantRuntime gp{};
    const bool tempReleased = controllerTemperatureGateIsReleased(i);
    const String tempReason = controllerTemperatureGateGetBlockReason(i);
    const bool tempGateApplies = controllerTemperatureGateAppliesToScenario(mcCurrentScenario);
    gp.used = true;
    gp.local = true;
    gp.online = true;
    gp.controllable = mcRuntime[i].localEnabled && mcRuntime[i].attached && (!tempGateApplies || tempReleased);
    gp.sourceSlot = i + 1U;
    gp.peerOutputSlot = i + 1U;
    gp.controllerId = mcLocalControllerId;
    gp.name = mcSlots[i].name.length() ? mcSlots[i].name : (String("Controller ") + String(i + 1));
    gp.peerIp = WiFi.localIP().toString();
    gp.phase = mcClampAssignedPhase(mcSlots[i].assignedPhase);
    gp.rampUpOrder = mcSlots[i].rampUpOrder;
    gp.rampDownOrder = mcSlots[i].rampDownOrder;
    gp.startAfterPercent = mcSlots[i].startAfterPercent;
    gp.minPercent = mcSlots[i].minPercent;
    gp.maxPercent = mcSlots[i].maxPercent;
    gp.useOnSurplus = mcSlots[i].useOnSurplus;
    gp.useOnCheap = mcSlots[i].useOnCheap;
    gp.useOnNegative = mcSlots[i].useOnNegative;
    gp.allowManualForce = mcSlots[i].allowManualForce;
    gp.pin = mcRuntime[i].effectivePin;
    gp.channel = mcRuntime[i].ledcChannel;
    gp.requestedPct = mcRuntime[i].requestedPercent;
    gp.assignedPct = mcRuntime[i].percent;
    gp.targetPct = mcRuntime[i].allowedPercent;
    gp.headroomPct = mcRuntime[i].headroomPercent;
    gp.blocked = mcRuntime[i].blocked || (tempGateApplies && !tempReleased);
    gp.hasHeadroom = (tempGateApplies && !tempReleased) ? false : mcRuntime[i].hasHeadroom;
    gp.blockReason = (tempGateApplies && !tempReleased) ? tempReason : mcRuntime[i].blockReason;
    mcGroupModelAddParticipant(gp);
  }

  if (mcRole == MC_ROLE_SLAVE) {
    mcSortGroupModel();
    mcGroupModelReason = "Slave-modus: groepsmodel blijft lokaal; de master plant netwerkdoelen en deze ESP voert alleen lokaal uit.";
    return;
  }

  bool anyRemoteDiscovered = false;
  const uint32_t now = millis();
  for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
    if (i >= mcControllerCount) break;
    if (!mcSlotRequestsNetworkPeer(i)) continue;

    // Stap 5 master-slave: op de master is elk netwerk-slot zelf al een volwaardig
    // groepsdeelnemer. De master hoeft voor planning dus niet meer te wachten op
    // compacte peer-outputstatus. Het slot op de master bepaalt de planning; de
    // slave voert alleen uit op zijn lokale controller 1.
    if (mcRole == MC_ROLE_MASTER) {
      anyRemoteDiscovered = true;
      const MultiControllerPeerRuntime& pr = mcPeerRuntime[i];
      const bool peerConfigured = (mcSlots[i].peerIp.length() > 0 && mcIsValidIpv4(mcSlots[i].peerIp));
      const bool linkFresh = multiControllerIsPeerTcpConnected(i) || (pr.lastOkMs != 0 && (now - pr.lastOkMs) <= MC_REMOTE_TARGET_TTL_MS);
      const bool tempGateApplies = controllerTemperatureGateAppliesToScenario(mcCurrentScenario);
      const bool peerTempReleased = (pr.compactOutputCount == 0) ? true : pr.outputs[0].tempReleased;
      const String peerAvailabilityReason = (pr.compactOutputCount == 0) ? String("") : pr.outputs[0].availabilityReason;
      const bool peerUsable = peerConfigured && linkFresh && mcPeerStillUsableForWriteback(pr, now) && (!tempGateApplies || peerTempReleased);
      MultiControllerGroupParticipantRuntime gp{};
      gp.used = true;
      gp.local = false;
      gp.online = peerUsable;
      gp.controllable = peerUsable;
      gp.sourceSlot = i + 1U;
      gp.peerOutputSlot = 1U;
      gp.controllerId = i + 1U;
      gp.name = mcSlots[i].name.length() ? mcSlots[i].name : (String("Netwerk-controller ") + String(i + 1));
      gp.peerIp = mcSlots[i].peerIp;
      gp.phase = mcClampAssignedPhase(mcSlots[i].assignedPhase);
      gp.rampUpOrder = mcSlots[i].rampUpOrder;
      gp.rampDownOrder = mcSlots[i].rampDownOrder;
      gp.startAfterPercent = mcSlots[i].startAfterPercent;
      gp.minPercent = mcSlots[i].minPercent;
      gp.maxPercent = mcSlots[i].maxPercent;
      gp.useOnSurplus = mcSlots[i].useOnSurplus;
      gp.useOnCheap = mcSlots[i].useOnCheap;
      gp.useOnNegative = mcSlots[i].useOnNegative;
      gp.allowManualForce = mcSlots[i].allowManualForce;
      gp.pin = -1;
      gp.channel = 0;
      gp.requestedPct = pr.requestedPct;
      gp.assignedPct = pr.assignedPct;
      gp.targetPct = pr.allowedPct;
      gp.headroomPct = max(0.0f, (float)mcSlots[i].maxPercent - gp.assignedPct);
      gp.blocked = !peerUsable;
      gp.hasHeadroom = peerUsable && (gp.headroomPct > 0.2f);
      gp.blockReason = peerUsable
        ? "Master-led netwerk-slot"
        : (peerAvailabilityReason.length() ? peerAvailabilityReason : (peerConfigured ? "Netwerk-slot wacht op bruikbare peerstatus." : "Netwerk-slot heeft geen geldig slave-IP."));

      for (uint8_t poIdx = 0; poIdx < pr.compactOutputCount; poIdx++) {
        const MultiControllerPeerOutputRuntime& po = pr.outputs[poIdx];
        if (!po.configured) continue;
        if (po.slot != gp.peerOutputSlot) continue;
        gp.assignedPct = po.assignedPct;
        gp.requestedPct = po.requestedPct;
        gp.targetPct = po.targetPct;
        gp.headroomPct = po.headroomPct;
        gp.blocked = po.blocked || (tempGateApplies && !po.tempReleased);
        gp.hasHeadroom = (tempGateApplies && !po.tempReleased) ? false : po.hasHeadroom;
        if (tempGateApplies && !po.tempReleased && po.availabilityReason.length()) gp.blockReason = po.availabilityReason;
        else if (po.blockReason.length()) gp.blockReason = po.blockReason;
        break;
      }

      mcGroupModelAddParticipant(gp);
      continue;
    }

    const MultiControllerPeerRuntime& pr = mcPeerRuntime[i];
    if (!mcPeerHasUsableCachedStatus(pr, now)) continue;
    if (pr.groupName.length() && mcGroupName.length() && pr.groupName != mcGroupName) continue;

    for (uint8_t poIdx = 0; poIdx < pr.compactOutputCount; poIdx++) {
      const MultiControllerPeerOutputRuntime& po = pr.outputs[poIdx];
      if (!po.configured) continue;
      anyRemoteDiscovered = true;
      MultiControllerGroupParticipantRuntime gp{};
      gp.used = true;
      gp.local = false;
      gp.online = true;
      const bool tempGateApplies = controllerTemperatureGateAppliesToScenario(mcCurrentScenario);
      gp.controllable = mcPeerStillUsableForWriteback(pr, now) && (!tempGateApplies || po.tempReleased);
      gp.sourceSlot = i + 1U;
      gp.peerOutputSlot = po.slot;
      gp.controllerId = pr.localControllerId;
      gp.name = po.name.length() ? po.name : (String("Peer output ") + String(po.slot));
      gp.peerIp = mcSlots[i].peerIp;
      gp.phase = po.phase;
      gp.rampUpOrder = po.rampUpOrder;
      gp.rampDownOrder = po.rampDownOrder;
      gp.startAfterPercent = po.startAfterPercent;
      gp.minPercent = po.minPercent;
      gp.maxPercent = po.maxPercent;
      gp.useOnSurplus = po.useOnSurplus;
      gp.useOnCheap = po.useOnCheap;
      gp.useOnNegative = po.useOnNegative;
      gp.allowManualForce = po.allowManualForce;
      gp.pin = po.pin;
      gp.channel = po.channel;
      gp.requestedPct = po.requestedPct;
      gp.assignedPct = po.assignedPct;
      gp.targetPct = po.targetPct;
      gp.headroomPct = po.headroomPct;
      gp.blocked = po.blocked || (tempGateApplies && !po.tempReleased);
      gp.hasHeadroom = (tempGateApplies && !po.tempReleased) ? false : po.hasHeadroom;
      gp.blockReason = (tempGateApplies && !po.tempReleased && po.availabilityReason.length()) ? po.availabilityReason : po.blockReason;
      mcGroupModelAddParticipant(gp);
    }
  }

  mcSortGroupModel();

  if (mcGroupModelClamped) return;
  if (mcRole == MC_ROLE_MASTER && mcGroupModelRemoteCount > 0) {
    mcGroupModelReason = "Master-slave groepsmodel actief: netwerk-slots op de master tellen nu direct mee als remote deelnemers; planning wacht niet meer op compacte peer-outputstatus.";
  } else if (mcGroupModelRemoteCount > 0) {
    mcGroupModelReason = "Lokale en remote outputs zijn nu samen opgenomen in één compact groepsmodel; de groepsbrede prioriteitsplanner kan hier nu doelen over berekenen.";
  } else if (anyRemoteDiscovered) {
    mcGroupModelReason = "Remote outputs zijn gezien maar pasten niet meer binnen het huidige model.";
  } else if (mcHasConfiguredNetworkPeers()) {
    mcGroupModelReason = "Netwerk-peers staan klaar; remote outputs worden toegevoegd zodra een peer online compacte outputstatus levert.";
  }
}

String mcScenarioLabel(uint8_t scenario) {
  switch (scenario) {
    case MC_SCENARIO_SURPLUS: return "zonne_overschot";
    case MC_SCENARIO_CHEAP: return "goedkope_prijs";
    case MC_SCENARIO_NEGATIVE: return "negatieve_prijs";
    case MC_SCENARIO_MANUAL_FORCE: return "handmatig_forceren";
    case MC_SCENARIO_PRIORITY_HEAT: return "prioriteitswarmte";
    default: return "idle";
  }
}
