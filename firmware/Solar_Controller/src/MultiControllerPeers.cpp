#include "MultiController.h"
#include "MultiControllerInternal.h"
#include "MultiControllerTcpLink.h"
#include "app_globals.h"

#include <WiFi.h>
#include <HTTPClient.h>

static inline void mcCooperativeYield() {
  delay(0);
}

void mcResetPeerRuntime(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return;
  mcPeerRuntime[slotIndex].configured = false;
  mcPeerRuntime[slotIndex].online = false;
  mcPeerRuntime[slotIndex].statusOk = false;
  mcPeerRuntime[slotIndex].fw = "";
  mcPeerRuntime[slotIndex].scenario = "";
  mcPeerRuntime[slotIndex].groupName = "";
  mcPeerRuntime[slotIndex].lastError = "Nog geen peer-contact.";
  mcPeerRuntime[slotIndex].localControllerId = 0;
  mcPeerRuntime[slotIndex].lastHttpCodePing = 0;
  mcPeerRuntime[slotIndex].lastHttpCodeStatus = 0;
  mcPeerRuntime[slotIndex].activeOutputs = 0;
  mcPeerRuntime[slotIndex].effectiveOutputs = 0;
  mcPeerRuntime[slotIndex].compactOutputCount = 0;
  mcPeerRuntime[slotIndex].requestedPct = 0.0f;
  mcPeerRuntime[slotIndex].allowedPct = 0.0f;
  mcPeerRuntime[slotIndex].assignedPct = 0.0f;
  mcPeerRuntime[slotIndex].fallbackActive = false;
  mcPeerRuntime[slotIndex].uptimeSec = 0;
  mcPeerRuntime[slotIndex].lastPollMs = 0;
  mcPeerRuntime[slotIndex].lastOkMs = 0;
  mcPeerRuntime[slotIndex].nextPollDueMs = 0;
  mcPeerRuntime[slotIndex].lastWriteMs = 0;
  mcPeerRuntime[slotIndex].lastWriteOkMs = 0;
  mcPeerRuntime[slotIndex].lastHttpCodeWrite = 0;
  mcPeerRuntime[slotIndex].lastWriteError = "Nog geen target-writeback.";
  mcPeerRuntime[slotIndex].lastWritePayload = "";
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    mcPeerRuntime[slotIndex].outputs[i].slot = i + 1;
    mcPeerRuntime[slotIndex].outputs[i].name = "";
    mcPeerRuntime[slotIndex].outputs[i].phase = 1;
    mcPeerRuntime[slotIndex].outputs[i].configured = false;
    mcPeerRuntime[slotIndex].outputs[i].active = false;
    mcPeerRuntime[slotIndex].outputs[i].attached = false;
    mcPeerRuntime[slotIndex].outputs[i].pin = -1;
    mcPeerRuntime[slotIndex].outputs[i].channel = 0;
    mcPeerRuntime[slotIndex].outputs[i].rampUpOrder = 0;
    mcPeerRuntime[slotIndex].outputs[i].rampDownOrder = 0;
    mcPeerRuntime[slotIndex].outputs[i].startAfterPercent = 0;
    mcPeerRuntime[slotIndex].outputs[i].minPercent = 0;
    mcPeerRuntime[slotIndex].outputs[i].maxPercent = 0;
    mcPeerRuntime[slotIndex].outputs[i].useOnSurplus = false;
    mcPeerRuntime[slotIndex].outputs[i].useOnCheap = false;
    mcPeerRuntime[slotIndex].outputs[i].useOnNegative = false;
    mcPeerRuntime[slotIndex].outputs[i].allowManualForce = false;
    mcPeerRuntime[slotIndex].outputs[i].requestedPct = 0.0f;
    mcPeerRuntime[slotIndex].outputs[i].assignedPct = 0.0f;
    mcPeerRuntime[slotIndex].outputs[i].targetPct = 0.0f;
    mcPeerRuntime[slotIndex].outputs[i].headroomPct = 0.0f;
    mcPeerRuntime[slotIndex].outputs[i].blocked = false;
    mcPeerRuntime[slotIndex].outputs[i].hasHeadroom = false;
    mcPeerRuntime[slotIndex].outputs[i].tempReleased = true;
    mcPeerRuntime[slotIndex].outputs[i].blockReason = "";
    mcPeerRuntime[slotIndex].outputs[i].availabilityReason = "";
  }
}

bool mcIsValidIpv4(const String& ip) {
  int parts = 0;
  int start = 0;
  while (start < (int)ip.length()) {
    int dot = ip.indexOf('.', start);
    String part = (dot >= 0) ? ip.substring(start, dot) : ip.substring(start);
    if (part.length() == 0 || part.length() > 3) return false;
    for (size_t i = 0; i < part.length(); i++) {
      if (!isDigit(part[i])) return false;
    }
    int value = part.toInt();
    if (value < 0 || value > 255) return false;
    parts++;
    if (dot < 0) break;
    start = dot + 1;
  }
  return parts == 4;
}

static String mcPeerBaseUrl(const String& ip) {
  return String("http://") + ip;
}

static bool mcHttpGetText(const String& url, uint16_t& httpCode, String& body, String& error) {
  httpCode = 0;
  body = "";
  error = "";

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout((int)MC_PEER_HTTP_TIMEOUT_MS);
  http.setTimeout((uint16_t)MC_PEER_HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    error = "HTTP begin mislukt";
    return false;
  }

  mcCooperativeYield();
  int code = http.GET();
  httpCode = (code > 0) ? (uint16_t)code : 0;
  if (code <= 0) {
    error = http.errorToString(code);
    http.end();
    return false;
  }

  body = http.getString();
  http.end();
  mcCooperativeYield();

  if (code != HTTP_CODE_OK) {
    error = String("HTTP ") + String(code);
    return false;
  }
  return true;
}



bool mcPeerHasUsableCachedStatus(const MultiControllerPeerRuntime& pr, uint32_t now) {
  if (!pr.configured) return false;
  if (pr.lastOkMs == 0) return false;
  if ((now - pr.lastOkMs) > MC_PEER_STATUS_CACHE_MAX_AGE_MS) return false;
  return (pr.compactOutputCount > 0 || pr.localControllerId > 0);
}

bool mcPeerStillUsableForWriteback(const MultiControllerPeerRuntime& pr, uint32_t now) {
  if (!pr.configured) return false;
  if (pr.online && pr.statusOk) return true;
  if (mcPeerHasUsableCachedStatus(pr, now)) return true;
  if (pr.lastWriteOkMs != 0 && (now - pr.lastWriteOkMs) <= (MC_INTERNAL_REMOTE_TARGET_TTL_MS + MC_INTERNAL_PEER_POLL_INTERVAL_MS + MC_PEER_HTTP_TIMEOUT_MS)) {
    return true;
  }
  return false;
}

static bool mcHttpPostJson(const String& url, const String& payload, uint16_t& httpCode, String& body, String& error) {
  httpCode = 0;
  body = "";
  error = "";

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout((int)MC_PEER_HTTP_TIMEOUT_MS);
  http.setTimeout((uint16_t)MC_PEER_HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    error = "HTTP begin mislukt";
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  mcCooperativeYield();
  int code = http.POST((uint8_t*)payload.c_str(), payload.length());
  httpCode = (code > 0) ? (uint16_t)code : 0;
  if (code <= 0) {
    error = http.errorToString(code);
    http.end();
    return false;
  }

  body = http.getString();
  http.end();
  mcCooperativeYield();

  if (code != HTTP_CODE_OK) {
    error = String("HTTP ") + String(code);
    return false;
  }
  return true;
}

uint32_t mcClampRemoteTargetTtlMs(uint32_t ttlMs) {
  if (ttlMs < MC_REMOTE_TARGET_TTL_MIN_MS) return MC_REMOTE_TARGET_TTL_MIN_MS;
  if (ttlMs > MC_REMOTE_TARGET_TTL_MAX_MS) return MC_REMOTE_TARGET_TTL_MAX_MS;
  return ttlMs;
}

bool mcIsConfiguredPeerIp(const String& ip) {
  if (ip.length() == 0) return false;
  for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
    if (i >= mcControllerCount) break;
    if (!mcSlotRequestsNetworkPeer(i)) continue;
    if (mcSlots[i].peerIp == ip) return true;
  }
  return false;
}

bool mcHasFreshRemoteTargets() {
  if (!mcRemoteTargetsActive) return false;
  if (mcRemoteTargetsReceivedMs == 0) return false;
  return ((millis() - mcRemoteTargetsReceivedMs) <= mcRemoteTargetsTtlMs);
}

void mcClearRemoteTargets(const String& reason) {
  mcRemoteTargetsActive = false;
  mcRemoteTargetsReceivedMs = 0;
  mcRemoteTargetsTtlMs = MC_REMOTE_TARGET_TTL_MS;
  mcRemoteTargetsScenario = MC_SCENARIO_IDLE;
  mcRemoteTargetsRequestedGroupPercent = 0.0f;
  mcRemoteTargetsOwnerIp = "";
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) mcRemoteTargetsPerSlot[i] = 0.0f;
  mcRemoteTargetsReason = reason.length() ? reason : "Geen remote targets actief.";
}

void mcRunFailSafeHousekeeping() {
  const uint32_t now = millis();

  if (mcModeUsesNetwork() && mcHasConfiguredNetworkPeers()) {
    for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
      if (i >= mcControllerCount) break;
      if (!mcSlotRequestsNetworkPeer(i)) continue;
      MultiControllerPeerRuntime& pr = mcPeerRuntime[i];
      if (!pr.configured) continue;
      if (pr.online && pr.lastOkMs != 0 && (now - pr.lastOkMs) > MC_PEER_STALE_OFFLINE_MS) {
        mcMarkPeerOffline(i, "Peerstatus verlopen; veilige uitsluiting actief.");
      }
    }
  }

  if (mcRemoteTargetsActive && !mcHasFreshRemoteTargets()) {
    mcClearRemoteTargets("Remote targets verlopen; veilige terugval naar lokale verdeling.");
  }

  if (mcRemoteTargetsActive && mcRemoteTargetsOwnerIp.length() && mcModeUsesNetwork() && mcHasConfiguredNetworkPeers()) {
    if (!mcIsConfiguredPeerIp(mcRemoteTargetsOwnerIp)) {
      mcClearRemoteTargets("Remote targets kwamen van onbekende peer; veilige terugval naar lokale verdeling.");
    }
  }

  if (mcRemoteTargetsActive && mcInternalCoordinator) {
    mcClearRemoteTargets("Deze ESP is nu master/leidend; ontvangen remote targets gewist.");
  }

  if (mcRemoteTargetsActive && (!mcModeUsesNetwork() || !mcHasConfiguredNetworkPeers())) {
    mcClearRemoteTargets("Geen geldige netwerk-koppeling meer; ontvangen remote targets gewist.");
  }
}

String mcPeerStatusLabel(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return "onbekend";
  const MultiControllerPeerRuntime& pr = mcPeerRuntime[slotIndex];
  if (!pr.configured) return "niet geconfigureerd";
  if (pr.online && pr.statusOk) return "online";
  if (pr.online) return "online, status deels bekend";
  return "offline";
}

String mcPeerLastSeenLabel(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return "-";
  const MultiControllerPeerRuntime& pr = mcPeerRuntime[slotIndex];
  if (pr.lastOkMs == 0) return "nog geen succesvolle heartbeat";
  const uint32_t ageSec = (millis() - pr.lastOkMs) / 1000UL;
  return String(ageSec) + "s geleden";
}

void mcMarkPeerOffline(uint8_t slotIndex, const String& error, uint16_t pingCode, uint16_t statusCode) {
  if (slotIndex >= MC_MAX_SLOTS) return;
  MultiControllerPeerRuntime& pr = mcPeerRuntime[slotIndex];
  pr.configured = mcSlotRequestsNetworkPeer(slotIndex);
  pr.online = false;
  pr.statusOk = false;
  pr.activeOutputs = 0;
  pr.effectiveOutputs = 0;
  pr.compactOutputCount = 0;
  pr.requestedPct = 0.0f;
  pr.assignedPct = 0.0f;
  if (error.length()) pr.lastError = error;
  pr.lastHttpCodePing = pingCode;
  pr.lastHttpCodeStatus = statusCode;
  pr.nextPollDueMs = millis() + MC_PEER_POLL_INTERVAL_MS;
  if (pr.lastWriteError.length() == 0) pr.lastWriteError = "Peer offline.";
}

bool mcPollPeerStatus(uint8_t slotIndex) {
  if (multiControllerTcpRealtimeAvailable()) {
    if (slotIndex < MC_MAX_SLOTS && mcSlotRequestsNetworkPeer(slotIndex)) {
      mcPeerRuntime[slotIndex].lastError = "Realtime peer-status loopt via TCP-control; HTTP poll niet meer gebruikt.";
    }
    return false;
  }
  if (!mcSlotRequestsNetworkPeer(slotIndex)) {
    mcResetPeerRuntime(slotIndex);
    return false;
  }

  MultiControllerPeerRuntime& pr = mcPeerRuntime[slotIndex];
  pr.configured = true;
  pr.lastPollMs = millis();

  const String ip = mcSlots[slotIndex].peerIp;
  if (!mcIsValidIpv4(ip)) {
    mcMarkPeerOffline(slotIndex, "Ongeldig peer IP-adres.");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    mcMarkPeerOffline(slotIndex, "WiFi niet verbonden.");
    return false;
  }

  String body;
  String error;
  uint16_t httpCode = 0;
  if (!mcHttpGetText(mcPeerBaseUrl(ip) + "/api/mc/peer_status", httpCode, body, error)) {
    mcMarkPeerOffline(slotIndex, error.length() ? error : "Peerstatus mislukt.", 0, httpCode);
    return false;
  }

  pr.online = true;
  pr.lastHttpCodePing = 0;
  pr.lastHttpCodeStatus = httpCode;
  pr.nextPollDueMs = millis() + MC_PEER_POLL_INTERVAL_MS;

  DynamicJsonDocument doc(2560);
  DeserializationError jsonErr = deserializeJson(doc, body);
  if (jsonErr) {
    pr.statusOk = false;
    pr.lastOkMs = millis();
    pr.lastError = String("Peer online, status JSON ongeldig: ") + jsonErr.c_str();
    return true;
  }

  pr.statusOk = true;
  pr.fw = doc["fw"].as<String>();
  pr.scenario = doc["mc_scenario"].as<String>();
  pr.groupName = doc["mc_group_name"].as<String>();
  pr.localControllerId = doc["mc_local_controller_id"] | 0;
  pr.activeOutputs = doc["mc_local_pwm_outputs_active"] | 0;
  pr.effectiveOutputs = doc["mc_local_pwm_outputs_effective"] | 0;
  pr.compactOutputCount = 0;
  pr.requestedPct = doc["mc_group_pwm_requested_pct"] | 0.0f;
  pr.allowedPct = doc["mc_group_pwm_allowed_pct"] | (doc["mc_group_pwm_assigned_pct"] | 0.0f);
  pr.assignedPct = doc["mc_group_pwm_assigned_pct"] | 0.0f;
  pr.fallbackActive = doc["mc_fallback_active"] | false;
  pr.uptimeSec = doc["uptime_s"] | 0UL;
  JsonArray outputs = doc["mc_peer_outputs"].as<JsonArray>();
  if (!outputs.isNull()) {
    uint8_t outIdx = 0;
    for (JsonObject out : outputs) {
      if (outIdx >= MC_MAX_SLOTS) break;
      MultiControllerPeerOutputRuntime& po = pr.outputs[outIdx];
      po.slot = out["slot"] | (outIdx + 1);
      po.name = out["name"].as<String>();
      po.phase = out["phase"] | 1;
      po.configured = out["configured"] | false;
      po.active = out["active"] | false;
      po.attached = false;
      po.pin = -1;
      po.channel = 0;
      po.rampUpOrder = out["ramp_up_order"] | 0;
      po.rampDownOrder = out["ramp_down_order"] | 0;
      po.startAfterPercent = out["start_after_pct"] | 0;
      po.minPercent = out["min_pct"] | 0;
      po.maxPercent = out["max_pct"] | 0;
      po.useOnSurplus = out["use_on_surplus"] | false;
      po.useOnCheap = out["use_on_cheap"] | false;
      po.useOnNegative = out["use_on_negative"] | false;
      po.allowManualForce = out["allow_manual_force"] | false;
      po.requestedPct = out["requested_pct"] | 0.0f;
      po.assignedPct = out["assigned_pct"] | 0.0f;
      po.targetPct = out["allowed_pct"] | po.assignedPct;
      po.headroomPct = out["headroom_pct"] | 0.0f;
      po.blocked = out["blocked"] | false;
      po.hasHeadroom = out["has_headroom"] | false;
      po.tempReleased = out["temp_released"] | true;
      po.blockReason = out["block_reason"].as<String>();
      po.availabilityReason = out["availability_reason"].as<String>();
      outIdx++;
    }
    pr.compactOutputCount = outIdx;
  }
  pr.lastError = "";
  pr.lastOkMs = millis();
  return true;
}

bool mcBuildPeerTargetWritePayload(uint8_t peerSlotIndex, String& outJson, String& errOut) {
  outJson = "";
  errOut = "";
  if (peerSlotIndex == 0 || peerSlotIndex >= MC_MAX_SLOTS) {
    errOut = "ongeldige peer slotindex";
    return false;
  }
  if (!mcSlotRequestsNetworkPeer(peerSlotIndex)) {
    errOut = "slot is geen netwerk-peer";
    return false;
  }
  DynamicJsonDocument doc(2048);
  doc["group_name"] = mcGroupName;
  doc["from_ip"] = WiFi.localIP().toString();
  doc["from_controller_id"] = mcLocalControllerId;
  doc["from_role"] = (mcRole == MC_ROLE_MASTER) ? "master" : ((mcRole == MC_ROLE_SLAVE) ? "slave" : "standalone");
  doc["master_led"] = (mcRole == MC_ROLE_MASTER);
  doc["scenario"] = mcCurrentScenario;
  doc["requested_group_pct"] = mcRequestedGroupPercent;
  doc["ttl_ms"] = MC_REMOTE_TARGET_TTL_MS;
  JsonArray targets = doc.createNestedArray("targets");
  uint8_t targetCount = 0;
  for (uint8_t i = 0; i < mcGroupModelCount; i++) {
    const MultiControllerGroupParticipantRuntime& gp = mcGroupModel[i];
    if (!gp.used || gp.local) continue;
    if (gp.sourceSlot != (peerSlotIndex + 1U)) continue;
    JsonObject t = targets.createNestedObject();
    t["slot"] = gp.peerOutputSlot;
    t["target_pct"] = gp.targetPct;
    targetCount++;
  }
  if (targetCount == 0) {
    errOut = "geen remote targets voor deze peer";
    return false;
  }
  String out;
  serializeJson(doc, out);
  outJson = out;
  return true;
}

bool mcWritePeerTargetsIfDue() {
  if (multiControllerTcpRealtimeAvailable()) {
    for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
      if (!mcSlotRequestsNetworkPeer(i)) continue;
      mcPeerRuntime[i].lastWriteError = "Realtime target-writeback loopt via TCP-control; HTTP writeback uitgeschakeld.";
    }
    return false;
  }
  if (!mcEnabled || !mcModeUsesNetwork() || !mcHasConfiguredNetworkPeers()) return false;
  if (mcRole != MC_ROLE_MASTER) return false;
  if (!mcGroupPriorityPlannerActive) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  const uint32_t now = millis();
  for (uint8_t scan = 0; scan < MC_MAX_SLOTS; scan++) {
    uint8_t idx = mcPeerRoundRobinIndex;
    mcPeerRoundRobinIndex++;
    if (mcPeerRoundRobinIndex >= MC_MAX_SLOTS) mcPeerRoundRobinIndex = 1;
    if (idx == 0 || idx >= mcControllerCount) continue;
    if (!mcSlotRequestsNetworkPeer(idx)) continue;
    MultiControllerPeerRuntime& pr = mcPeerRuntime[idx];
    pr.configured = true;
    if (mcSlots[idx].peerIp.length() == 0 || !mcIsValidIpv4(mcSlots[idx].peerIp)) continue;
    if (pr.lastWriteMs != 0 && (now - pr.lastWriteMs) < MC_PEER_WRITEBACK_INTERVAL_MS) continue;

    String payload;
    String err;
    if (!mcBuildPeerTargetWritePayload(idx, payload, err)) {
      pr.lastWriteMs = now;
      if (err.length()) pr.lastWriteError = err;
      continue;
    }

    // Stap 4 versnellingsplan:
    // identieke targets niet steeds opnieuw pushen. Alleen na een ruimere refresh
    // sturen we dezelfde payload nog eens om de remote TTL gezond te houden.
    if (payload == pr.lastWritePayload && pr.lastWriteOkMs != 0) {
      if ((now - pr.lastWriteOkMs) < MC_PEER_UNCHANGED_WRITE_REFRESH_MS) {
        continue;
      }
    }

    String body;
    uint16_t httpCode = 0;
    pr.lastWriteMs = now;
    if (!mcHttpPostJson(mcPeerBaseUrl(mcSlots[idx].peerIp) + "/api/mc/targets", payload, httpCode, body, err)) {
      pr.lastHttpCodeWrite = httpCode;
      pr.lastWriteError = err.length() ? err : "Target-writeback mislukt.";
      if (httpCode == 0 && pr.lastWriteError.length()) {
        pr.lastError = String("Master-push tijdelijk mislukt: ") + pr.lastWriteError;
      }
      return true;
    }

    pr.online = true;
    pr.statusOk = true;
    pr.lastHttpCodeWrite = httpCode;
    pr.lastWriteOkMs = now;
    pr.lastWritePayload = payload;
    pr.lastWriteError = "";
    // Houd de status relatief vlot bij zonder meteen een tweede HTTP-call in exact
    // dezelfde service-tik te forceren.
    pr.nextPollDueMs = now + MC_NETWORK_SERVICE_INTERVAL_MS;
    return true;
  }

  return false;
}

bool mcSlotUsesLocalPwmRequested(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return false;
  if (slotIndex == 0) return true;
  if (!mcEnabled) return false;
  if (!(mcMode == MC_MODE_LOCAL_PWM || mcMode == MC_MODE_HYBRID)) return false;
  if (slotIndex >= mcControllerCount) return false;
  return (mcSlots[slotIndex].slotType == MC_SLOT_LOCAL_PWM);
}

bool mcSlotRequestsNetworkPeer(uint8_t slotIndex) {
  if (slotIndex == 0 || slotIndex >= MC_MAX_SLOTS) return false;
  if (!mcEnabled) return false;
  if (slotIndex >= mcControllerCount) return false;
  return (mcSlots[slotIndex].slotType == MC_SLOT_NETWORK_PEER);
}

static uint8_t mcCountRequestedLocalPwmOutputs() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (mcSlotUsesLocalPwmRequested(i)) count++;
  }
  return count;
}

bool mcModeUsesNetwork() {
  return (mcMode == MC_MODE_NETWORK || mcMode == MC_MODE_HYBRID);
}

bool mcHasConfiguredNetworkPeers() {
  if (!mcEnabled) return false;
  if (!mcModeUsesNetwork()) return false;
  for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
    if (i >= mcControllerCount) break;
    if (mcSlotRequestsNetworkPeer(i)) return true;
  }
  return false;
}


String mcGetMasterLinkIp() {
  if (!(mcEnabled && mcRole == MC_ROLE_SLAVE)) return String("");
  if (mcControllerCount < 2) return String("");
  return mcSlots[1].peerIp;
}

void mcUpdateInternalCoordinatorState() {
  if (!mcEnabled) {
    mcInternalCoordinator = true;
    mcInternalCoordinatorReason = "Multi Controller staat uit; lokale controller blijft leidend.";
    return;
  }

  if (mcRole == MC_ROLE_SLAVE) {
    mcInternalCoordinator = false;
    mcInternalCoordinatorReason = mcHasConfiguredNetworkPeers()
      ? String("Slave-modus: master-led; deze ESP luistert alleen naar opdrachten van ") + mcSlots[1].peerIp + "."
      : String("Slave-modus: wacht op geconfigureerd master-IP.");
    return;
  }

  if (mcRole == MC_ROLE_MASTER) {
    mcInternalCoordinator = true;
    mcInternalCoordinatorReason = mcHasConfiguredNetworkPeers()
      ? String("Master-modus: deze ESP is leidend en pusht setpoints naar de geconfigureerde netwerk-slots.")
      : String("Master-modus: geen netwerk-slots actief; deze ESP plant alleen lokale outputs.");
    return;
  }

  mcInternalCoordinator = true;
  mcInternalCoordinatorReason = "Standalone/lokaal: deze ESP regelt alleen zijn lokale outputs.";
}

