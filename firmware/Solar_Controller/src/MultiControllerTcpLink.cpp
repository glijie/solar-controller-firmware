#include "MultiControllerTcpLink.h"

#include "MultiController.h"
#include "MultiControllerInternal.h"
#include "app_globals.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <math.h>
#include <string.h>

static constexpr uint16_t MC_TCP_CONTROL_PORT = 41125;
static constexpr uint32_t MC_TCP_CONNECT_RETRY_MS = 1000UL;
static constexpr uint32_t MC_TCP_COMMAND_HEARTBEAT_MS = 500UL;
static constexpr uint32_t MC_TCP_STATUS_HEARTBEAT_MS = 500UL;
static constexpr uint32_t MC_TCP_CONNECT_TIMEOUT_MS = 150UL;
static constexpr uint32_t MC_TCP_LINK_SILENCE_MS = 3000UL;
static constexpr uint32_t MC_TCP_ACK_GRACE_MS = 1500UL;
static constexpr uint32_t MC_TCP_SERVER_ACCEPT_GRACE_MS = 200UL;
static constexpr uint32_t MC_TCP_FRAME_MAGIC = 0x4D43544CUL; // MCTL
static constexpr uint8_t  MC_TCP_FRAME_VERSION = 1;
static constexpr uint8_t  MC_TCP_FRAME_TYPE_COMMAND = 1;
static constexpr uint8_t  MC_TCP_FRAME_TYPE_STATUS  = 2;
static constexpr uint8_t  MC_TCP_FLAG_FALLBACK_ACTIVE = 0x01;
static constexpr uint8_t  MC_TCP_FLAG_REMOTE_ACTIVE   = 0x02;
static constexpr uint8_t  MC_TCP_FLAG_CONFIG_OK       = 0x04;
static constexpr uint8_t  MC_TCP_FLAG_RUNTIME_OK      = 0x08;
static constexpr uint8_t  MC_TCP_FLAG_CLAMP_ACTIVE    = 0x10;

#pragma pack(push, 1)
struct McTcpControlFrame {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint8_t role;
  uint8_t controllerId;
  uint32_t seq;
  uint32_t ackSeq;
  uint32_t sentMs;
  uint32_t ttlMs;
  uint32_t uptimeSec;
  uint8_t scenario;
  uint8_t flags;
  uint8_t activeSlotMask;
  uint8_t blockedSlotMask;
  float requestedGroupPct;
  float requestedPct[MC_MAX_SLOTS];
  float targetPct[MC_MAX_SLOTS];
  float appliedPct[MC_MAX_SLOTS];
  char groupName[32];
  char fwVersion[16];
};
#pragma pack(pop)

struct McTcpMasterPeerState {
  WiFiClient client;
  bool wasConnected;
  uint32_t nextConnectMs;
  uint32_t lastTxMs;
  uint32_t lastRxMs;
  uint32_t lastAcceptedStatusMs;
  uint32_t lastSentSeq;
  uint32_t lastAckSeq;
  uint32_t lastSentMs;
  uint32_t lastRttMs;
  uint32_t reconnectCount;
  float lastTargets[MC_MAX_SLOTS];
  uint8_t lastScenario;
  bool lastTargetsValid;
};

namespace {
WiFiServer mcTcpServer(MC_TCP_CONTROL_PORT);
bool mcTcpServerStarted = false;
bool mcTcpLowLatencyRequested = false;
WiFiClient mcTcpSlaveClient;
uint32_t mcTcpSlaveLastRxMs = 0;
uint32_t mcTcpSlaveLastStatusTxMs = 0;
uint32_t mcTcpSlaveLastAcceptedMs = 0;
uint32_t mcTcpSlaveLastCommandSeq = 0;
uint32_t mcTcpSlaveStatusSeq = 0;
McTcpMasterPeerState mcTcpMasterPeers[MC_MAX_SLOTS];

static void mcTcpZeroFrame(McTcpControlFrame& frame, uint8_t type) {
  memset(&frame, 0, sizeof(frame));
  frame.magic = MC_TCP_FRAME_MAGIC;
  frame.version = MC_TCP_FRAME_VERSION;
  frame.type = type;
  frame.role = mcRole;
  frame.controllerId = mcLocalControllerId;
  frame.scenario = mcCurrentScenario;
  frame.ttlMs = mcClampRemoteTargetTtlMs(MC_REMOTE_TARGET_TTL_MS);
  frame.uptimeSec = millis() / 1000UL;
  frame.requestedGroupPct = mcRequestedGroupPercent;
  if (mcFallbackActive) frame.flags |= MC_TCP_FLAG_FALLBACK_ACTIVE;
  if (multiControllerIsRemoteTargetControlActive()) frame.flags |= MC_TCP_FLAG_REMOTE_ACTIVE;
  if (multiControllerIsConfigValid()) frame.flags |= MC_TCP_FLAG_CONFIG_OK;
  if (multiControllerIsRuntimeValid()) frame.flags |= MC_TCP_FLAG_RUNTIME_OK;
  String group = mcGroupName;
  if (group.length() == 0) group = "solar-group";
  strncpy(frame.groupName, group.c_str(), sizeof(frame.groupName) - 1);
  strncpy(frame.fwVersion, FW_VERSION ? FW_VERSION : "", sizeof(frame.fwVersion) - 1);
}

static uint8_t mcTcpBuildActiveMask() {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (multiControllerIsLocalPwmSlotConfigured(i) && multiControllerIsLocalPwmSlotActive(i) && multiControllerIsLocalPwmSlotAttached(i)) {
      mask |= (1U << i);
    }
  }
  return mask;
}

static uint8_t mcTcpBuildBlockedMask() {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (multiControllerIsLocalPwmSlotBlocked(i)) mask |= (1U << i);
  }
  return mask;
}

static bool mcTcpWriteFrame(WiFiClient& client, const McTcpControlFrame& frame) {
  if (!client || !client.connected()) return false;
  const uint8_t* raw = reinterpret_cast<const uint8_t*>(&frame);
  const size_t want = sizeof(frame);
  size_t written = client.write(raw, want);
  return written == want;
}

static bool mcTcpReadFrame(WiFiClient& client, McTcpControlFrame& frame) {
  if (!client || !client.connected()) return false;
  const int needed = (int)sizeof(frame);
  if (client.available() < needed) return false;
  uint8_t* raw = reinterpret_cast<uint8_t*>(&frame);
  size_t got = client.read(raw, sizeof(frame));
  if (got != sizeof(frame)) return false;
  if (frame.magic != MC_TCP_FRAME_MAGIC) return false;
  if (frame.version != MC_TCP_FRAME_VERSION) return false;
  return true;
}

static void mcTcpFillStatusFrame(McTcpControlFrame& frame) {
  mcTcpZeroFrame(frame, MC_TCP_FRAME_TYPE_STATUS);
  frame.seq = ++mcTcpSlaveStatusSeq;
  frame.ackSeq = mcTcpSlaveLastCommandSeq;
  frame.sentMs = millis();
  frame.activeSlotMask = mcTcpBuildActiveMask();
  frame.blockedSlotMask = mcTcpBuildBlockedMask();

  bool clampActive = false;
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    frame.requestedPct[i] = multiControllerGetLocalPwmSlotRequestedPercent(i);
    frame.targetPct[i] = multiControllerGetLocalPwmSlotAllowedPercent(i);
    frame.appliedPct[i] = multiControllerGetLocalPwmSlotPercent(i);
    if (multiControllerIsLocalPwmSlotBlocked(i)) clampActive = true;
  }
  if (clampActive) frame.flags |= MC_TCP_FLAG_CLAMP_ACTIVE;
}

static bool mcTcpGroupMatches(const char* incoming) {
  if (!incoming || !incoming[0]) return false;
  String in = String(incoming);
  if (mcGroupName.length() == 0) return true;
  return in == mcGroupName;
}

static bool mcTcpMasterIpAllowed(const IPAddress& ip) {
  const String masterIp = multiControllerGetSlaveMasterIp();
  if (masterIp.length() == 0) return false;
  return ip.toString() == masterIp;
}

static void mcTcpApplyLowLatencyWiFi(bool enable) {
  if (enable == mcTcpLowLatencyRequested) return;
  if (WiFi.status() != WL_CONNECTED) return;
  // step308: WiFi power-save blijft permanent uit voor maximale stabiliteit.
  // Multi Controller mag WIFI_PS_NONE nog wel afdwingen, maar schakelt niet meer
  // terug naar WIFI_PS_MIN_MODEM wanneer low-latency niet nodig is.
  if (esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK) {
    mcTcpLowLatencyRequested = enable;
  }
}

static void mcTcpDisconnectMasterPeer(uint8_t slotIndex, const String& reason) {
  if (slotIndex >= MC_MAX_SLOTS) return;
  McTcpMasterPeerState& st = mcTcpMasterPeers[slotIndex];
  if (st.client) st.client.stop();
  st.wasConnected = false;
  st.lastTargetsValid = false;
  st.nextConnectMs = millis() + MC_TCP_CONNECT_RETRY_MS;
  mcMarkPeerOffline(slotIndex, reason);
}

static bool mcTcpConnectMasterPeer(uint8_t slotIndex, uint32_t nowMs) {
  if (slotIndex >= MC_MAX_SLOTS) return false;
  if (!mcSlotRequestsNetworkPeer(slotIndex)) return false;
  McTcpMasterPeerState& st = mcTcpMasterPeers[slotIndex];
  if (st.client && st.client.connected()) return true;
  if (nowMs < st.nextConnectMs) return false;

  IPAddress ip;
  if (!ip.fromString(mcSlots[slotIndex].peerIp)) {
    st.nextConnectMs = nowMs + MC_TCP_CONNECT_RETRY_MS;
    mcMarkPeerOffline(slotIndex, "Ongeldig slave-IP voor TCP link.");
    return false;
  }

  st.client.stop();
  if (!st.client.connect(ip, MC_TCP_CONTROL_PORT, (int32_t)MC_TCP_CONNECT_TIMEOUT_MS)) {
    st.reconnectCount++;
    st.nextConnectMs = nowMs + MC_TCP_CONNECT_RETRY_MS;
    mcMarkPeerOffline(slotIndex, "TCP connect naar slave mislukt.");
    return false;
  }

  st.client.setNoDelay(true);
  st.client.setTimeout(0);
  st.wasConnected = true;
  st.lastRxMs = nowMs;
  st.lastTxMs = 0;
  st.lastAcceptedStatusMs = 0;
  st.nextConnectMs = nowMs + MC_TCP_SERVER_ACCEPT_GRACE_MS;
  mcPeerRuntime[slotIndex].configured = true;
  mcPeerRuntime[slotIndex].online = true;
  mcPeerRuntime[slotIndex].statusOk = false;
  mcPeerRuntime[slotIndex].lastError = "TCP verbonden; wacht op slave-status.";
  return true;
}

static bool mcTcpBuildCommandForPeer(uint8_t slotIndex, McTcpControlFrame& frame) {
  mcTcpZeroFrame(frame, MC_TCP_FRAME_TYPE_COMMAND);
  frame.seq = ++mcTcpMasterPeers[slotIndex].lastSentSeq;
  frame.sentMs = millis();
  frame.activeSlotMask = 0x01;
  float targetPct = 0.0f;
  float requestedPct = 0.0f;

  for (uint8_t gi = 0; gi < mcGroupModelCount; gi++) {
    const MultiControllerGroupParticipantRuntime& gp = mcGroupModel[gi];
    if (gp.local) continue;
    if (gp.sourceSlot != (slotIndex + 1U)) continue;
    targetPct = gp.targetPct;
    requestedPct = gp.requestedPct;
    break;
  }

  if (!isfinite(targetPct) || targetPct < 0.0f) targetPct = 0.0f;
  if (!isfinite(requestedPct) || requestedPct < 0.0f) requestedPct = targetPct;
  if (targetPct > (float)mcSlots[slotIndex].maxPercent) targetPct = (float)mcSlots[slotIndex].maxPercent;
  if (requestedPct > (float)mcSlots[slotIndex].maxPercent) requestedPct = (float)mcSlots[slotIndex].maxPercent;

  frame.requestedPct[0] = requestedPct;
  frame.targetPct[0] = targetPct;
  frame.appliedPct[0] = mcPeerRuntime[slotIndex].assignedPct;
  return true;
}

static void mcTcpApplySlaveStatusToPeer(uint8_t slotIndex, const McTcpControlFrame& frame, uint32_t nowMs) {
  if (slotIndex >= MC_MAX_SLOTS) return;
  MultiControllerPeerRuntime& pr = mcPeerRuntime[slotIndex];
  McTcpMasterPeerState& st = mcTcpMasterPeers[slotIndex];
  pr.configured = true;
  pr.online = true;
  pr.statusOk = true;
  pr.groupName = String(frame.groupName);
  pr.fw = String(frame.fwVersion);
  pr.scenario = mcScenarioLabel(frame.scenario);
  pr.localControllerId = frame.controllerId;
  pr.activeOutputs = 0;
  pr.effectiveOutputs = 0;
  pr.compactOutputCount = 0;
  pr.requestedPct = frame.requestedPct[0];
  pr.allowedPct = frame.targetPct[0];
  pr.assignedPct = frame.appliedPct[0];
  pr.fallbackActive = (frame.flags & MC_TCP_FLAG_FALLBACK_ACTIVE) != 0;
  pr.uptimeSec = frame.uptimeSec;
  pr.lastOkMs = nowMs;
  pr.lastPollMs = nowMs;
  pr.lastHttpCodeStatus = 200;
  pr.lastError = "";
  pr.lastWriteError = "";
  pr.lastHttpCodeWrite = 200;
  if (frame.ackSeq != 0 && frame.ackSeq == st.lastSentSeq) {
    pr.lastWriteOkMs = nowMs;
    st.lastAckSeq = frame.ackSeq;
    if (nowMs >= st.lastSentMs) st.lastRttMs = nowMs - st.lastSentMs;
  }

  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    pr.outputs[i].slot = i + 1;
    pr.outputs[i].configured = ((frame.activeSlotMask & (1U << i)) != 0) || multiControllerIsLocalPwmSlotConfigured(i);
    pr.outputs[i].active = (frame.activeSlotMask & (1U << i)) != 0;
    pr.outputs[i].attached = pr.outputs[i].active;
    pr.outputs[i].requestedPct = frame.requestedPct[i];
    pr.outputs[i].assignedPct = frame.appliedPct[i];
    pr.outputs[i].targetPct = frame.targetPct[i];
    pr.outputs[i].headroomPct = max(0.0f, 100.0f - frame.appliedPct[i]);
    pr.outputs[i].blocked = (frame.blockedSlotMask & (1U << i)) != 0;
    pr.outputs[i].hasHeadroom = (pr.outputs[i].headroomPct > 0.2f);
    pr.outputs[i].name = (i == 0) ? (mcSlots[slotIndex].name.length() ? mcSlots[slotIndex].name : String("Slave controller")) : String("");
    pr.outputs[i].phase = (i == 0) ? mcClampAssignedPhase(mcSlots[slotIndex].assignedPhase) : 1;
    pr.outputs[i].minPercent = (i == 0) ? mcSlots[slotIndex].minPercent : 0;
    pr.outputs[i].maxPercent = (i == 0) ? mcSlots[slotIndex].maxPercent : 100;
    pr.outputs[i].rampUpOrder = (i == 0) ? mcSlots[slotIndex].rampUpOrder : 0;
    pr.outputs[i].rampDownOrder = (i == 0) ? mcSlots[slotIndex].rampDownOrder : 0;
    pr.outputs[i].startAfterPercent = (i == 0) ? mcSlots[slotIndex].startAfterPercent : 0;
    pr.outputs[i].useOnSurplus = (i == 0) ? mcSlots[slotIndex].useOnSurplus : false;
    pr.outputs[i].useOnCheap = (i == 0) ? mcSlots[slotIndex].useOnCheap : false;
    pr.outputs[i].useOnNegative = (i == 0) ? mcSlots[slotIndex].useOnNegative : false;
    pr.outputs[i].allowManualForce = (i == 0) ? mcSlots[slotIndex].allowManualForce : false;
    pr.outputs[i].blockReason = pr.outputs[i].blocked ? String("Slave safety clamp actief.") : String("");
    if (pr.outputs[i].configured) {
      if ((uint8_t)(i + 1) > pr.compactOutputCount) pr.compactOutputCount = (uint8_t)(i + 1);
      pr.activeOutputs++;
      if (!pr.outputs[i].blocked) pr.effectiveOutputs++;
    }
  }
  st.lastAcceptedStatusMs = nowMs;
}

static void mcTcpHandleMasterRx(uint8_t slotIndex, uint32_t nowMs) {
  if (slotIndex >= MC_MAX_SLOTS) return;
  McTcpMasterPeerState& st = mcTcpMasterPeers[slotIndex];
  if (!st.client || !st.client.connected()) return;

  uint8_t loops = 0;
  while (loops < 4) {
    McTcpControlFrame frame;
    if (!mcTcpReadFrame(st.client, frame)) break;
    loops++;
    st.lastRxMs = nowMs;
    if (frame.type != MC_TCP_FRAME_TYPE_STATUS) continue;
    if (!mcTcpGroupMatches(frame.groupName)) continue;
    mcTcpApplySlaveStatusToPeer(slotIndex, frame, nowMs);
  }
}

static bool mcTcpShouldSendCommand(uint8_t slotIndex, uint32_t nowMs, McTcpControlFrame& frame) {
  if (!mcTcpBuildCommandForPeer(slotIndex, frame)) return false;
  McTcpMasterPeerState& st = mcTcpMasterPeers[slotIndex];
  if (!st.lastTargetsValid) return true;
  if ((nowMs - st.lastTxMs) >= MC_TCP_COMMAND_HEARTBEAT_MS) return true;
  if (frame.scenario != st.lastScenario) return true;
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (fabsf(frame.targetPct[i] - st.lastTargets[i]) > 0.05f) return true;
  }
  return false;
}

static void mcTcpSendCommandIfNeeded(uint8_t slotIndex, uint32_t nowMs) {
  if (slotIndex >= MC_MAX_SLOTS) return;
  McTcpMasterPeerState& st = mcTcpMasterPeers[slotIndex];
  if (!st.client || !st.client.connected()) return;

  McTcpControlFrame frame;
  if (!mcTcpShouldSendCommand(slotIndex, nowMs, frame)) return;
  if (!mcTcpWriteFrame(st.client, frame)) {
    mcTcpDisconnectMasterPeer(slotIndex, "TCP write naar slave mislukt.");
    return;
  }
  st.lastTxMs = nowMs;
  st.lastSentMs = nowMs;
  st.lastScenario = frame.scenario;
  st.lastTargetsValid = true;
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) st.lastTargets[i] = frame.targetPct[i];
  MultiControllerPeerRuntime& pr = mcPeerRuntime[slotIndex];
  pr.lastWriteMs = nowMs;
  pr.lastWritePayload = String("TCP seq ") + String(frame.seq) + " -> " + String(frame.targetPct[0], 1) + "%";
}

static void mcTcpResetSlaveClient(const String& reason) {
  if (mcTcpSlaveClient) mcTcpSlaveClient.stop();
  mcTcpSlaveLastRxMs = 0;
  mcTcpSlaveLastAcceptedMs = 0;
  mcTcpSlaveLastStatusTxMs = 0;
  mcTcpSlaveLastCommandSeq = 0;
  if (multiControllerIsSlaveRole() && !multiControllerSlaveLegionellaOverrideActive()) {
    mcClearRemoteTargets(reason.length() ? reason : String("TCP master-link weggevallen; veilige terugval."));
  }
  if (multiControllerIsSlaveRole() && mcControllerCount >= 2) {
    mcMarkPeerOffline(1, reason, 0, 0);
  }
}

static void mcTcpAcceptSlaveClientIfNeeded(uint32_t nowMs) {
  if (!mcTcpServerStarted) return;
  WiFiClient incoming = mcTcpServer.available();
  if (!incoming) return;
  incoming.setNoDelay(true);
  incoming.setTimeout(0);
  if (!mcTcpMasterIpAllowed(incoming.remoteIP())) {
    incoming.stop();
    return;
  }
  if (mcTcpSlaveClient) mcTcpSlaveClient.stop();
  mcTcpSlaveClient = incoming;
  mcTcpSlaveLastRxMs = nowMs;
  mcTcpSlaveLastAcceptedMs = nowMs;
  if (multiControllerIsSlaveRole() && mcControllerCount >= 2) {
    MultiControllerPeerRuntime& pr = mcPeerRuntime[1];
    pr.configured = true;
    pr.online = true;
    pr.statusOk = false;
    pr.lastOkMs = nowMs;
    pr.lastError = "TCP master verbonden; wacht op eerste commandoframe.";
  }
}

static void mcTcpApplyIncomingCommand(const McTcpControlFrame& frame, uint32_t nowMs, const String& remoteIp) {
  if (!multiControllerIsSlaveRole()) return;
  if (!mcTcpGroupMatches(frame.groupName)) return;
  if (remoteIp != multiControllerGetSlaveMasterIp()) return;

  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    float targetPct = frame.targetPct[i];
    if (!isfinite(targetPct) || targetPct < 0.0f) targetPct = 0.0f;
    if (targetPct > (float)mcSlots[i].maxPercent) targetPct = (float)mcSlots[i].maxPercent;
    if (mcSlotUsesLocalPwmRequested(i)) mcRemoteTargetsPerSlot[i] = targetPct;
    else mcRemoteTargetsPerSlot[i] = 0.0f;
  }
  mcRemoteTargetsActive = true;
  mcRemoteTargetsReceivedMs = nowMs;
  mcRemoteTargetsTtlMs = mcClampRemoteTargetTtlMs(frame.ttlMs);
  mcRemoteTargetsScenario = frame.scenario;
  mcRemoteTargetsRequestedGroupPercent = frame.requestedGroupPct;
  mcRemoteTargetsOwnerIp = remoteIp;
  mcRemoteTargetsReason = String("TCP master-link actief van ") + remoteIp;
  mcTcpSlaveLastCommandSeq = frame.seq;
  mcTcpSlaveLastAcceptedMs = nowMs;

  if (mcControllerCount >= 2) {
    MultiControllerPeerRuntime& pr = mcPeerRuntime[1];
    pr.configured = true;
    pr.online = true;
    pr.statusOk = true;
    pr.lastOkMs = nowMs;
    pr.lastWriteOkMs = nowMs;
    pr.lastHttpCodeWrite = 200;
    pr.lastError = "";
    pr.lastWriteError = "";
  }
}

static void mcTcpHandleSlaveRx(uint32_t nowMs) {
  if (!mcTcpSlaveClient || !mcTcpSlaveClient.connected()) return;
  uint8_t loops = 0;
  while (loops < 4) {
    McTcpControlFrame frame;
    if (!mcTcpReadFrame(mcTcpSlaveClient, frame)) break;
    loops++;
    mcTcpSlaveLastRxMs = nowMs;
    if (frame.type != MC_TCP_FRAME_TYPE_COMMAND) continue;
    mcTcpApplyIncomingCommand(frame, nowMs, mcTcpSlaveClient.remoteIP().toString());
  }
}

static void mcTcpSendSlaveStatusIfNeeded(uint32_t nowMs) {
  if (!mcTcpSlaveClient || !mcTcpSlaveClient.connected()) return;
  if (mcTcpSlaveLastAcceptedMs == 0) return;
  if (mcTcpSlaveLastStatusTxMs != 0 && (nowMs - mcTcpSlaveLastStatusTxMs) < MC_TCP_STATUS_HEARTBEAT_MS && mcTcpSlaveLastAcceptedMs != nowMs) return;
  McTcpControlFrame frame;
  mcTcpFillStatusFrame(frame);
  if (!mcTcpWriteFrame(mcTcpSlaveClient, frame)) {
    mcTcpResetSlaveClient("TCP status naar master mislukt.");
    return;
  }
  mcTcpSlaveLastStatusTxMs = nowMs;
}
}

void mcTcpLinkInit() {
  mcTcpLinkConfigChanged();
}

void mcTcpLinkShutdown() {
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (mcTcpMasterPeers[i].client) mcTcpMasterPeers[i].client.stop();
    mcTcpMasterPeers[i].wasConnected = false;
    mcTcpMasterPeers[i].lastTargetsValid = false;
  }
  mcTcpResetSlaveClient("TCP link gestopt.");
  if (mcTcpServerStarted) {
    mcTcpServer.stop();
    mcTcpServerStarted = false;
  }
  mcTcpApplyLowLatencyWiFi(false);
}

void mcTcpLinkConfigChanged() {
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    mcTcpMasterPeers[i].nextConnectMs = 0;
    mcTcpMasterPeers[i].lastTargetsValid = false;
    mcTcpMasterPeers[i].lastScenario = MC_SCENARIO_IDLE;
    mcTcpMasterPeers[i].lastSentSeq = 0;
    mcTcpMasterPeers[i].lastAckSeq = 0;
    mcTcpMasterPeers[i].lastRttMs = 0;
  }
}

void mcTcpLinkLoop(uint32_t nowMs, bool slaveRole) {
  const bool useTcp = mcEnabled && mcModeUsesNetwork() && mcHasConfiguredNetworkPeers() && WiFi.status() == WL_CONNECTED;
  mcTcpApplyLowLatencyWiFi(useTcp);
  if (!useTcp) {
    mcTcpLinkShutdown();
    return;
  }

  if (slaveRole) {
    if (!mcTcpServerStarted) {
      mcTcpServer.begin();
      mcTcpServer.setNoDelay(true);
      mcTcpServerStarted = true;
    }
    mcTcpAcceptSlaveClientIfNeeded(nowMs);
    if (mcTcpSlaveClient && !mcTcpMasterIpAllowed(mcTcpSlaveClient.remoteIP())) {
      mcTcpResetSlaveClient("TCP client komt niet van geconfigureerde master.");
      return;
    }
    if (mcTcpSlaveClient && !mcTcpSlaveClient.connected()) {
      mcTcpResetSlaveClient("TCP master-link verbroken.");
      return;
    }
    mcTcpHandleSlaveRx(nowMs);
    mcTcpSendSlaveStatusIfNeeded(nowMs);
    if (mcTcpSlaveLastRxMs != 0 && (nowMs - mcTcpSlaveLastRxMs) > MC_TCP_LINK_SILENCE_MS) {
      mcTcpResetSlaveClient("TCP master-link stilgevallen.");
    }
    return;
  }

  for (uint8_t idx = 1; idx < MC_MAX_SLOTS; idx++) {
    if (idx >= mcControllerCount) continue;
    if (!mcSlotRequestsNetworkPeer(idx)) continue;
    if (!mcTcpConnectMasterPeer(idx, nowMs)) continue;
    mcTcpHandleMasterRx(idx, nowMs);
    mcTcpSendCommandIfNeeded(idx, nowMs);
    McTcpMasterPeerState& st = mcTcpMasterPeers[idx];
    if (st.client && !st.client.connected()) {
      mcTcpDisconnectMasterPeer(idx, "TCP link naar slave verbroken.");
      continue;
    }
    if (st.lastRxMs != 0 && (nowMs - st.lastRxMs) > MC_TCP_LINK_SILENCE_MS) {
      mcTcpDisconnectMasterPeer(idx, "Geen slave-status meer op TCP link.");
      continue;
    }
    if (st.lastSentMs != 0 && st.lastAckSeq != st.lastSentSeq && (nowMs - st.lastSentMs) > MC_TCP_ACK_GRACE_MS) {
      mcPeerRuntime[idx].lastWriteError = "Slave ack bleef uit op TCP link.";
    }
  }
}

bool multiControllerTcpRealtimeAvailable() {
  if (!mcEnabled || !mcModeUsesNetwork() || !mcHasConfiguredNetworkPeers()) return false;
  if (multiControllerIsSlaveRole()) return mcTcpSlaveClient && mcTcpSlaveClient.connected();
  for (uint8_t i = 1; i < MC_MAX_SLOTS; i++) {
    if (!mcSlotRequestsNetworkPeer(i)) continue;
    if (mcTcpMasterPeers[i].client && mcTcpMasterPeers[i].client.connected()) return true;
  }
  return false;
}

bool multiControllerTcpSlaveServerListening() {
  return mcTcpServerStarted;
}

uint16_t multiControllerGetTcpControlPort() {
  return MC_TCP_CONTROL_PORT;
}

bool multiControllerIsPeerTcpConnected(uint8_t slotIndex) {
  if (multiControllerIsSlaveRole()) return mcTcpSlaveClient && mcTcpSlaveClient.connected();
  if (slotIndex >= MC_MAX_SLOTS) return false;
  return mcTcpMasterPeers[slotIndex].client && mcTcpMasterPeers[slotIndex].client.connected();
}

uint32_t multiControllerGetPeerTcpAgeMs(uint8_t slotIndex) {
  const uint32_t nowMs = millis();
  if (multiControllerIsSlaveRole()) {
    if (mcTcpSlaveLastRxMs == 0 || nowMs < mcTcpSlaveLastRxMs) return 0;
    return nowMs - mcTcpSlaveLastRxMs;
  }
  if (slotIndex >= MC_MAX_SLOTS) return 0;
  if (mcTcpMasterPeers[slotIndex].lastRxMs == 0 || nowMs < mcTcpMasterPeers[slotIndex].lastRxMs) return 0;
  return nowMs - mcTcpMasterPeers[slotIndex].lastRxMs;
}

uint32_t multiControllerGetPeerTcpRoundTripMs(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return 0;
  return mcTcpMasterPeers[slotIndex].lastRttMs;
}

uint32_t multiControllerGetPeerTcpReconnectCount(uint8_t slotIndex) {
  if (slotIndex >= MC_MAX_SLOTS) return 0;
  return mcTcpMasterPeers[slotIndex].reconnectCount;
}
