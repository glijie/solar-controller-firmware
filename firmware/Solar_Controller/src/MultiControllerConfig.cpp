#include "MultiController.h"
#include "MultiControllerInternal.h"
#include "MultiControllerTcpLink.h"
#include "app_globals.h"

#include <stdio.h>

static uint8_t mcDetermineModeFromSlots() {
  bool hasNetwork = false;
  bool hasExtraLocal = false;
  for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
    if (i >= mcControllerCount) break;
    if (mcSlots[i].slotType == MC_SLOT_NETWORK_PEER) hasNetwork = true;
    else hasExtraLocal = true;
  }
  if (hasNetwork && hasExtraLocal) return MC_MODE_HYBRID;
  if (hasNetwork) return MC_MODE_NETWORK;
  return MC_MODE_LOCAL_PWM;
}

static int mcReadInt(WebServer& server, const String& key, int currentValue) {
  if (!server.hasArg(key)) return currentValue;
  return server.arg(key).toInt();
}

static bool mcReadBool(WebServer& server, const String& key, bool currentValue) {
  if (!server.hasArg(key)) return currentValue;
  String v = server.arg(key);
  v.trim();
  v.toLowerCase();
  return (v == "1" || v == "true" || v == "on" || v == "yes");
}

static String mcReadString(WebServer& server, const String& key, const String& currentValue) {
  if (!server.hasArg(key)) return currentValue;
  String v = server.arg(key);
  v.trim();
  return v;
}

static bool mcValidateNetworkSlotsOnlyAtEnd(String& reason) {
  reason = "";
  bool seenNetwork = false;

  for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
    if (i >= mcControllerCount) break;

    const MultiControllerSlotConfig& s = mcSlots[i];
    const bool isNetwork = (s.slotType == MC_SLOT_NETWORK_PEER);
    const bool isLocal = (s.slotType == MC_SLOT_LOCAL_PWM);

    if (!isNetwork && !isLocal) {
      reason = "Onbekend controller-type in Multi Controller-configuratie.";
      return false;
    }

    if (isNetwork) {
      seenNetwork = true;
      continue;
    }

    if (seenNetwork) {
      reason = "Netwerk-controllers mogen alleen op de laatste actieve positie(s) staan. Combinaties zoals lokaal → netwerk → lokaal zijn niet toegestaan.";
      return false;
    }
  }

  return true;
}

bool multiControllerValidateCurrentConfig(String& reason) {
  return mcValidateConfig(reason);
}

bool mcValidateConfig(String& reason) {
  reason = "";

  if (mcSlots[0].slotType != MC_SLOT_LOCAL_PWM || mcSlots[0].pwmPin != MC_PRIMARY_PWM_PIN) {
    reason = "Controller 1 moet lokaal blijven op GPIO25; veilige terugval naar alleen controller 1.";
    return false;
  }

  if (!mcEnabled || mcRole == MC_ROLE_STANDALONE) {
    return true;
  }

  if (mcControllerCount < 1 || mcControllerCount > MC_MAX_SLOTS) {
    reason = "Aantal controllers valt buiten bereik; veilige terugval naar alleen controller 1.";
    return false;
  }

  if (mcLocalControllerId < 1 || mcLocalControllerId > mcControllerCount) {
    reason = "Eigen controller-ID valt buiten de groep; veilige terugval naar alleen controller 1.";
    return false;
  }

  String orderReason;
  if (!mcValidateNetworkSlotsOnlyAtEnd(orderReason)) {
    reason = orderReason + " Veilige terugval naar alleen controller 1.";
    return false;
  }

  bool usedExtraPins[3] = {false, false, false};

  for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
    if (i >= mcControllerCount) break;
    const MultiControllerSlotConfig& s = mcSlots[i];

    if (s.slotType == MC_SLOT_NETWORK_PEER) {
      if (s.peerIp.length() == 0) {
        reason = "Netwerk-peer zonder IP-adres; veilige terugval naar alleen controller 1.";
        return false;
      }
      if (!mcIsValidIpv4(s.peerIp)) {
        reason = "Netwerk-peer heeft geen geldig IPv4-adres; veilige terugval naar alleen controller 1.";
        return false;
      }
      continue;
    }

    if (s.slotType != MC_SLOT_LOCAL_PWM) {
      reason = "Onbekend controller-type; veilige terugval naar alleen controller 1.";
      return false;
    }

    if (!mcIsAllowedExtraPin(s.pwmPin)) {
      reason = "Ongeldige extra PWM-pin gekozen; veilige terugval naar alleen controller 1.";
      return false;
    }

    int pinIdx = -1;
    for (size_t p = 0; p < 3; p++) {
      if (MC_EXTRA_PWM_PINS[p] == s.pwmPin) {
        pinIdx = (int)p;
        break;
      }
    }
    if (pinIdx < 0) {
      reason = "Extra PWM-pin kon niet worden gevalideerd; veilige terugval naar alleen controller 1.";
      return false;
    }
    if (usedExtraPins[pinIdx]) {
      reason = "Dubbele extra PWM-pin gedetecteerd; veilige terugval naar alleen controller 1.";
      return false;
    }
    usedExtraPins[pinIdx] = true;
  }

  return true;
}

void multiControllerSetDefaults() {
  mcEnabled = false;
  mcRole = MC_ROLE_STANDALONE;
  mcLastActiveRole = MC_ROLE_MASTER;
  mcMode = MC_MODE_DISABLED;
  mcControllerCount = 1;
  mcLocalControllerId = 1;
  mcGroupName = "solar-group";
  mcSlaveAllowLocalLegionellaOverride = false;

  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    MultiControllerSlotConfig& s = mcSlots[i];
    s.slotType = (i == 0) ? MC_SLOT_LOCAL_PWM : MC_SLOT_NETWORK_PEER;
    s.name = String("Controller ") + String(i + 1);
    s.peerIp = "";
    s.pwmPin = mcDefaultPinForSlot(i);
    s.assignedPhase = phaseGuardPhase;
    s.rampUpOrder = i + 1;
    s.rampDownOrder = MC_MAX_SLOTS - i;
    s.startAfterPercent = (i == 0) ? 100 : 80;
    s.minPercent = 0;
    s.maxPercent = 100;
    s.useOnSurplus = true;
    s.useOnCheap = true;
    s.useOnNegative = true;
    s.allowManualForce = true;
    mcResetRuntimeSlot(i);
  }

  mcSlots[0].slotType = MC_SLOT_LOCAL_PWM;
  mcSlots[0].pwmPin = MC_PRIMARY_PWM_PIN;
  mcSlots[1].slotType = MC_SLOT_LOCAL_PWM;
  mcSlots[1].pwmPin = 26;
  mcSlots[2].slotType = MC_SLOT_LOCAL_PWM;
  mcSlots[2].pwmPin = 27;

  mcConfigValid = true;
  mcRuntimeValid = true;
  mcFallbackActive = false;
  mcFallbackReason = "Geen fallback actief";
  mcRequestedLocalPwmCount = 1;
  mcEffectiveLocalPwmCount = 0;
  mcRequestedGroupPercent = 0.0f;
  mcAssignedGroupPercent = 0.0f;
  mcCurrentScenario = MC_SCENARIO_IDLE;
  mcCurrentScenarioLabel = "idle";
  mcNegativePriorityOverrideActive = false;
  mcAppScenario = MC_SCENARIO_IDLE;
}

void multiControllerClamp() {
  if (mcLastActiveRole != MC_ROLE_MASTER && mcLastActiveRole != MC_ROLE_SLAVE) mcLastActiveRole = MC_ROLE_MASTER;

  if (!mcEnabled) {
    mcRole = MC_ROLE_STANDALONE;
  } else {
    if (mcRole != MC_ROLE_MASTER && mcRole != MC_ROLE_SLAVE) mcRole = mcLastActiveRole;
    mcLastActiveRole = mcRole;
  }

  if (mcMode > MC_MODE_HYBRID) mcMode = MC_MODE_DISABLED;
  if (!mcEnabled || mcRole == MC_ROLE_STANDALONE) mcMode = MC_MODE_DISABLED;
  if (mcControllerCount < 1) mcControllerCount = 1;
  if (mcControllerCount > MC_MAX_SLOTS) mcControllerCount = MC_MAX_SLOTS;
  if (mcLocalControllerId < 1) mcLocalControllerId = 1;
  if (mcLocalControllerId > mcControllerCount) mcLocalControllerId = 1;

  mcGroupName.trim();
  if (mcGroupName.length() == 0) mcGroupName = "solar-group";
  if (mcGroupName.length() > 32) mcGroupName = mcGroupName.substring(0, 32);

  bool usedExtra[3] = {false, false, false};

  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    MultiControllerSlotConfig& s = mcSlots[i];

    if (s.name.length() == 0) s.name = String("Controller ") + String(i + 1);
    if (s.name.length() > 32) s.name = s.name.substring(0, 32);
    s.peerIp.trim();
    if (s.peerIp.length() > 32) s.peerIp = s.peerIp.substring(0, 32);

    if (i == 0) {
      s.slotType = MC_SLOT_LOCAL_PWM;
      s.pwmPin = MC_PRIMARY_PWM_PIN;
    } else {
      if (s.slotType != MC_SLOT_LOCAL_PWM && s.slotType != MC_SLOT_NETWORK_PEER) {
        s.slotType = (mcMode == MC_MODE_NETWORK) ? MC_SLOT_NETWORK_PEER : MC_SLOT_LOCAL_PWM;
      }

      if (s.slotType == MC_SLOT_LOCAL_PWM) {
        if (!mcIsAllowedExtraPin(s.pwmPin)) {
          s.pwmPin = mcDefaultPinForSlot(i);
        }

        int pinIndex = -1;
        for (size_t p = 0; p < 3; p++) {
          if (MC_EXTRA_PWM_PINS[p] == s.pwmPin) {
            pinIndex = (int)p;
            break;
          }
        }

        if (pinIndex >= 0 && usedExtra[pinIndex]) {
          for (size_t p = 0; p < 3; p++) {
            if (!usedExtra[p]) {
              s.pwmPin = MC_EXTRA_PWM_PINS[p];
              pinIndex = (int)p;
              break;
            }
          }
        }

        if (pinIndex >= 0) usedExtra[pinIndex] = true;
      }
    }

    if (s.rampUpOrder < 1) s.rampUpOrder = 1;
    if (s.rampUpOrder > MC_MAX_SLOTS) s.rampUpOrder = MC_MAX_SLOTS;
    if (s.rampDownOrder < 1) s.rampDownOrder = 1;
    if (s.rampDownOrder > MC_MAX_SLOTS) s.rampDownOrder = MC_MAX_SLOTS;
    s.assignedPhase = mcClampAssignedPhase(s.assignedPhase);
    if (s.startAfterPercent > 100) s.startAfterPercent = 100;
    if (s.minPercent > 100) s.minPercent = 100;
    if (s.maxPercent > 100) s.maxPercent = 100;
    if (s.maxPercent < s.minPercent) s.maxPercent = s.minPercent;
  }

  mcSlots[0].slotType = MC_SLOT_LOCAL_PWM;
  mcSlots[0].pwmPin = MC_PRIMARY_PWM_PIN;

  if (!mcEnabled) {
    mcMode = MC_MODE_DISABLED;
  } else if (mcRole == MC_ROLE_SLAVE) {
    mcControllerCount = 2;
    mcLocalControllerId = 1;
    mcSlots[1].slotType = MC_SLOT_NETWORK_PEER;
    mcSlots[1].pwmPin = mcDefaultPinForSlot(1);
    mcMode = MC_MODE_NETWORK;
  } else if (mcRole == MC_ROLE_MASTER) {
    mcMode = mcDetermineModeFromSlots();
  } else {
    mcMode = MC_MODE_DISABLED;
  }
}

void multiControllerLoadConfig(Preferences& prefs) {
  if (!prefs.isKey("mc_en")) {
    multiControllerSetDefaults();
    return;
  }

  mcEnabled = prefs.getBool("mc_en", false);
  mcLastActiveRole = prefs.getUChar("mc_lastrole", MC_ROLE_MASTER);
  mcRole = prefs.isKey("mc_role") ? prefs.getUChar("mc_role", MC_ROLE_STANDALONE)
                                   : (mcEnabled ? mcLastActiveRole : MC_ROLE_STANDALONE);
  mcMode = prefs.getUChar("mc_mode", MC_MODE_DISABLED);
  mcControllerCount = prefs.getUChar("mc_cnt", 1);
  mcLocalControllerId = prefs.getUChar("mc_self", 1);
  mcGroupName = prefs.getString("mc_group", "solar-group");
  mcSlaveAllowLocalLegionellaOverride = prefs.getBool("mc_legovr", false);

  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    MultiControllerSlotConfig& s = mcSlots[i];
    char key[16];

    snprintf(key, sizeof(key), "mc%u_type", (unsigned)(i + 1));
    s.slotType = prefs.getUChar(key, (i == 0) ? MC_SLOT_LOCAL_PWM : MC_SLOT_NETWORK_PEER);

    snprintf(key, sizeof(key), "mc%u_name", (unsigned)(i + 1));
    s.name = prefs.getString(key, String("Controller ") + String(i + 1));

    snprintf(key, sizeof(key), "mc%u_ip", (unsigned)(i + 1));
    s.peerIp = prefs.getString(key, "");

    snprintf(key, sizeof(key), "mc%u_pin", (unsigned)(i + 1));
    s.pwmPin = prefs.getInt(key, mcDefaultPinForSlot(i));

    snprintf(key, sizeof(key), "mc%u_ph", (unsigned)(i + 1));
    s.assignedPhase = prefs.getUChar(key, phaseGuardPhase);

    snprintf(key, sizeof(key), "mc%u_up", (unsigned)(i + 1));
    s.rampUpOrder = prefs.getUChar(key, i + 1);

    snprintf(key, sizeof(key), "mc%u_down", (unsigned)(i + 1));
    s.rampDownOrder = prefs.getUChar(key, MC_MAX_SLOTS - i);

    snprintf(key, sizeof(key), "mc%u_start", (unsigned)(i + 1));
    s.startAfterPercent = prefs.getUChar(key, (i == 0) ? 100 : 80);

    snprintf(key, sizeof(key), "mc%u_min", (unsigned)(i + 1));
    s.minPercent = prefs.getUChar(key, 0);

    snprintf(key, sizeof(key), "mc%u_max", (unsigned)(i + 1));
    s.maxPercent = prefs.getUChar(key, 100);

    snprintf(key, sizeof(key), "mc%u_sun", (unsigned)(i + 1));
    s.useOnSurplus = prefs.getBool(key, true);

    snprintf(key, sizeof(key), "mc%u_chp", (unsigned)(i + 1));
    s.useOnCheap = prefs.getBool(key, true);

    snprintf(key, sizeof(key), "mc%u_neg", (unsigned)(i + 1));
    s.useOnNegative = prefs.getBool(key, true);

    snprintf(key, sizeof(key), "mc%u_frc", (unsigned)(i + 1));
    s.allowManualForce = prefs.getBool(key, true);
  }

  multiControllerClamp();
  mcTcpLinkConfigChanged();
}

void multiControllerSaveConfig(Preferences& prefs) {
  prefs.putBool("mc_en", mcEnabled);
  prefs.putUChar("mc_role", mcRole);
  prefs.putUChar("mc_lastrole", mcLastActiveRole);
  prefs.putUChar("mc_mode", mcMode);
  prefs.putUChar("mc_cnt", mcControllerCount);
  prefs.putUChar("mc_self", mcLocalControllerId);
  prefs.putString("mc_group", mcGroupName);
  prefs.putBool("mc_legovr", mcSlaveAllowLocalLegionellaOverride);

  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    const MultiControllerSlotConfig& s = mcSlots[i];
    char key[16];

    snprintf(key, sizeof(key), "mc%u_type", (unsigned)(i + 1));
    prefs.putUChar(key, s.slotType);

    snprintf(key, sizeof(key), "mc%u_name", (unsigned)(i + 1));
    prefs.putString(key, s.name);

    snprintf(key, sizeof(key), "mc%u_ip", (unsigned)(i + 1));
    prefs.putString(key, s.peerIp);

    snprintf(key, sizeof(key), "mc%u_pin", (unsigned)(i + 1));
    prefs.putInt(key, s.pwmPin);

    snprintf(key, sizeof(key), "mc%u_ph", (unsigned)(i + 1));
    prefs.putUChar(key, s.assignedPhase);

    snprintf(key, sizeof(key), "mc%u_up", (unsigned)(i + 1));
    prefs.putUChar(key, s.rampUpOrder);

    snprintf(key, sizeof(key), "mc%u_down", (unsigned)(i + 1));
    prefs.putUChar(key, s.rampDownOrder);

    snprintf(key, sizeof(key), "mc%u_start", (unsigned)(i + 1));
    prefs.putUChar(key, s.startAfterPercent);

    snprintf(key, sizeof(key), "mc%u_min", (unsigned)(i + 1));
    prefs.putUChar(key, s.minPercent);

    snprintf(key, sizeof(key), "mc%u_max", (unsigned)(i + 1));
    prefs.putUChar(key, s.maxPercent);

    snprintf(key, sizeof(key), "mc%u_sun", (unsigned)(i + 1));
    prefs.putBool(key, s.useOnSurplus);

    snprintf(key, sizeof(key), "mc%u_chp", (unsigned)(i + 1));
    prefs.putBool(key, s.useOnCheap);

    snprintf(key, sizeof(key), "mc%u_neg", (unsigned)(i + 1));
    prefs.putBool(key, s.useOnNegative);

    snprintf(key, sizeof(key), "mc%u_frc", (unsigned)(i + 1));
    prefs.putBool(key, s.allowManualForce);
  }
}

void multiControllerApplySettings(WebServer& server) {
  String section = server.hasArg("_section") ? server.arg("_section") : "";
  section.trim();
  section.toLowerCase();
  if (section != "multi") return;

  const uint8_t roleFallback = (mcLastActiveRole == MC_ROLE_SLAVE) ? MC_ROLE_SLAVE : MC_ROLE_MASTER;
  // Multi Controller aan/uit moet zich gedragen als een echte toggle binnen de
  // multi-sectie: checkbox aanwezig = aan, checkbox afwezig = uit. De generieke
  // mcReadBool()-helper houdt bij afwezige velden juist de oude waarde vast, wat
  // goed is voor verborgen/optionele checkboxes maar hier fout uitpakt. Daardoor
  // sprong de ESP na "uit + opslaan" direct terug naar de vorige master/slave-rol.
  mcEnabled = server.hasArg("mc_en");
  mcLastActiveRole = (uint8_t)mcReadInt(server, "mc_role", roleFallback);
  if (mcLastActiveRole != MC_ROLE_SLAVE) mcLastActiveRole = MC_ROLE_MASTER;
  mcRole = mcEnabled ? mcLastActiveRole : MC_ROLE_STANDALONE;
  if (mcRole == MC_ROLE_SLAVE) mcMode = MC_MODE_NETWORK;
  mcControllerCount = (uint8_t)mcReadInt(server, "mc_cnt", mcControllerCount);
  mcLocalControllerId = (uint8_t)mcReadInt(server, "mc_self", mcLocalControllerId);
  mcGroupName = mcReadString(server, "mc_group", mcGroupName);
  mcSlaveAllowLocalLegionellaOverride = mcReadBool(server, "mc_legovr", mcSlaveAllowLocalLegionellaOverride);

  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    MultiControllerSlotConfig& s = mcSlots[i];
    String prefix = String("mc_s") + String(i + 1) + String("_");

    if (i > 0) s.slotType = (uint8_t)mcReadInt(server, prefix + "type", s.slotType);
    s.name = mcReadString(server, prefix + "name", s.name);
    s.peerIp = mcReadString(server, prefix + "ip", s.peerIp);
    s.pwmPin = mcReadInt(server, prefix + "pin", s.pwmPin);
    s.rampUpOrder = (uint8_t)mcReadInt(server, prefix + "up", s.rampUpOrder);
    s.rampDownOrder = (uint8_t)mcReadInt(server, prefix + "down", s.rampDownOrder);
    s.startAfterPercent = (uint8_t)mcReadInt(server, prefix + "start", s.startAfterPercent);
    s.minPercent = (uint8_t)mcReadInt(server, prefix + "min", s.minPercent);
    s.maxPercent = (uint8_t)mcReadInt(server, prefix + "max", s.maxPercent);
    s.useOnSurplus = mcReadBool(server, prefix + "sun", s.useOnSurplus);
    s.useOnCheap = mcReadBool(server, prefix + "cheap", s.useOnCheap);
    s.useOnNegative = mcReadBool(server, prefix + "neg", s.useOnNegative);
    s.allowManualForce = mcReadBool(server, prefix + "force", s.allowManualForce);
  }

  multiControllerClamp();
  mcTcpLinkConfigChanged();
}
