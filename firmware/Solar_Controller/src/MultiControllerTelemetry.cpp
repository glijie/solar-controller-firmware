#include "MultiControllerTelemetry.h"

#include "MultiController.h"
#include "ControllerTemperatureGate.h"
#include "app_globals.h"

static const char* mcTelemetryRoleLabel() {
  if (mcRole == MC_ROLE_MASTER) return "master";
  if (mcRole == MC_ROLE_SLAVE) return "slave";
  return "standalone";
}

static void appendMultiControllerRoleStatus(JsonDocument& doc) {
  doc["mc_role"] = mcTelemetryRoleLabel();
  doc["mc_master_led"] = multiControllerIsMasterRole();
  doc["mc_slave_led"] = multiControllerIsSlaveRole();

  if (multiControllerIsMasterRole()) {
    doc["mc_network_slots_configured"] = multiControllerGetConfiguredPeerCount();
    doc["mc_network_slots_online"] = multiControllerGetOnlinePeerCount();
    doc["mc_network_slots_healthy"] = multiControllerGetHealthyPeerCount();
    doc["mc_network_slots_writeback_ok"] = multiControllerGetWritebackHealthyPeerCount();
    doc["mc_tcp_realtime"] = multiControllerTcpRealtimeAvailable();
    doc["mc_tcp_port"] = multiControllerGetTcpControlPort();
    doc["mc_control_source"] = multiControllerTcpRealtimeAvailable() ? "master_tcp_control" : "master_planner";
  } else if (multiControllerIsSlaveRole()) {
    const String masterIp = multiControllerGetSlaveMasterIp();
    doc["mc_master_ip"] = masterIp;
    doc["mc_master_link_configured"] = masterIp.length() > 0;
    doc["mc_master_link_online"] = multiControllerGetOnlinePeerCount() > 0;
    doc["mc_master_link_healthy"] = multiControllerGetHealthyPeerCount() > 0;
    doc["mc_master_link_writeback_ok"] = multiControllerGetWritebackHealthyPeerCount() > 0;
    doc["mc_tcp_realtime"] = multiControllerTcpRealtimeAvailable();
    doc["mc_tcp_server_listening"] = multiControllerTcpSlaveServerListening();
    doc["mc_tcp_port"] = multiControllerGetTcpControlPort();
    doc["mc_control_source"] = multiControllerIsRemoteTargetControlActive() ? "master_tcp_remote" : "master_wait";
  } else {
    doc["mc_control_source"] = "local";
  }
}

void appendMultiControllerCompactPeerOutputs(JsonDocument& doc) {
  JsonArray mcPeerArr = doc.createNestedArray("mc_peer_outputs");
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (!multiControllerIsLocalPwmSlotConfigured(i)) continue;
    JsonObject mcObj = mcPeerArr.createNestedObject();
    mcObj["slot"] = i + 1;
    mcObj["name"] = mcSlots[i].name.length() ? mcSlots[i].name : (String("Controller ") + String(i + 1));
    mcObj["phase"] = multiControllerGetSlotAssignedPhase(i);
    mcObj["configured"] = true;
    mcObj["active"] = multiControllerIsLocalPwmSlotActive(i);
    mcObj["attached"] = multiControllerIsLocalPwmSlotAttached(i);
    mcObj["pin"] = multiControllerGetLocalPwmSlotPin(i);
    mcObj["channel"] = multiControllerGetLocalPwmSlotChannel(i);
    mcObj["ramp_up_order"] = mcSlots[i].rampUpOrder;
    mcObj["ramp_down_order"] = mcSlots[i].rampDownOrder;
    mcObj["start_after_pct"] = mcSlots[i].startAfterPercent;
    mcObj["min_pct"] = mcSlots[i].minPercent;
    mcObj["max_pct"] = mcSlots[i].maxPercent;
    mcObj["use_on_surplus"] = mcSlots[i].useOnSurplus;
    mcObj["use_on_cheap"] = mcSlots[i].useOnCheap;
    mcObj["use_on_negative"] = mcSlots[i].useOnNegative;
    mcObj["allow_manual_force"] = mcSlots[i].allowManualForce;
    mcObj["requested_pct"] = multiControllerGetLocalPwmSlotRequestedPercent(i);
    mcObj["allowed_pct"] = multiControllerGetLocalPwmSlotAllowedPercent(i);
    mcObj["assigned_pct"] = multiControllerGetLocalPwmSlotPercent(i);
    mcObj["headroom_pct"] = multiControllerGetLocalPwmSlotHeadroomPercent(i);
    mcObj["blocked"] = multiControllerIsLocalPwmSlotBlocked(i);
    mcObj["has_headroom"] = multiControllerLocalSlotHasHeadroom(i);
    mcObj["temp_gate_enabled"] = controllerTemperatureGateIsEnabled(i);
    mcObj["temp_released"] = controllerTemperatureGateIsReleased(i);
    mcObj["availability_reason"] = controllerTemperatureGateGetBlockReason(i);
    const String blockReason = multiControllerGetLocalPwmSlotBlockReason(i);
    if (blockReason.length()) mcObj["block_reason"] = blockReason;
  }
}

static void appendMultiControllerPeerStatusOutputs(JsonDocument& doc) {
  JsonArray mcPeerArr = doc.createNestedArray("mc_peer_outputs");
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    if (!multiControllerIsLocalPwmSlotConfigured(i)) continue;
    JsonObject mcObj = mcPeerArr.createNestedObject();
    mcObj["slot"] = i + 1;
    mcObj["name"] = mcSlots[i].name.length() ? mcSlots[i].name : (String("Controller ") + String(i + 1));
    mcObj["phase"] = multiControllerGetSlotAssignedPhase(i);
    mcObj["configured"] = true;
    mcObj["active"] = multiControllerIsLocalPwmSlotActive(i);
    mcObj["ramp_up_order"] = mcSlots[i].rampUpOrder;
    mcObj["ramp_down_order"] = mcSlots[i].rampDownOrder;
    mcObj["start_after_pct"] = mcSlots[i].startAfterPercent;
    mcObj["min_pct"] = mcSlots[i].minPercent;
    mcObj["max_pct"] = mcSlots[i].maxPercent;
    mcObj["use_on_surplus"] = mcSlots[i].useOnSurplus;
    mcObj["use_on_cheap"] = mcSlots[i].useOnCheap;
    mcObj["use_on_negative"] = mcSlots[i].useOnNegative;
    mcObj["allow_manual_force"] = mcSlots[i].allowManualForce;
    mcObj["requested_pct"] = multiControllerGetLocalPwmSlotRequestedPercent(i);
    mcObj["allowed_pct"] = multiControllerGetLocalPwmSlotAllowedPercent(i);
    mcObj["assigned_pct"] = multiControllerGetLocalPwmSlotPercent(i);
    mcObj["headroom_pct"] = multiControllerGetLocalPwmSlotHeadroomPercent(i);
    mcObj["blocked"] = multiControllerIsLocalPwmSlotBlocked(i);
    mcObj["has_headroom"] = multiControllerLocalSlotHasHeadroom(i);
    mcObj["temp_gate_enabled"] = controllerTemperatureGateIsEnabled(i);
    mcObj["temp_released"] = controllerTemperatureGateIsReleased(i);
    mcObj["availability_reason"] = controllerTemperatureGateGetBlockReason(i);
    const String blockReason = multiControllerGetLocalPwmSlotBlockReason(i);
    if (blockReason.length()) mcObj["block_reason"] = blockReason;
  }
}

void appendMultiControllerLocalPwmArray(JsonDocument& doc) {
  JsonArray mcArr = doc.createNestedArray("mc_local_pwm");
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
    JsonObject mcObj = mcArr.createNestedObject();
    mcObj["slot"] = i + 1;
    mcObj["active"] = multiControllerIsLocalPwmSlotActive(i);
    mcObj["attached"] = multiControllerIsLocalPwmSlotAttached(i);
    mcObj["pin"] = multiControllerGetLocalPwmSlotPin(i);
    mcObj["channel"] = multiControllerGetLocalPwmSlotChannel(i);
    mcObj["phase"] = multiControllerGetSlotAssignedPhase(i);
    mcObj["requested_pct"] = multiControllerGetLocalPwmSlotRequestedPercent(i);
    mcObj["allowed_pct"] = multiControllerGetLocalPwmSlotAllowedPercent(i);
    mcObj["duty_percent"] = multiControllerGetLocalPwmSlotPercent(i);
    mcObj["temp_gate_enabled"] = controllerTemperatureGateIsEnabled(i);
    mcObj["temp_released"] = controllerTemperatureGateIsReleased(i);
    mcObj["availability_reason"] = controllerTemperatureGateGetBlockReason(i);
  }
}

void appendMultiControllerMqttStatus(JsonDocument& doc) {
  doc["mc_enabled"] = mcEnabled;
  doc["mc_mode"] = mcMode;
  doc["mc_controller_count"] = mcControllerCount;
  doc["mc_self_id"] = mcLocalControllerId;
  appendMultiControllerRoleStatus(doc);
  doc["mc_group"] = mcGroupName;
  doc["mc_config_ok"] = multiControllerIsConfigValid();
  doc["mc_runtime_ok"] = multiControllerIsRuntimeValid();
  doc["mc_fallback_active"] = multiControllerIsFallbackActive();
  doc["mc_fallback_reason"] = multiControllerGetFallbackReason();
  doc["mc_local_pwm_outputs_requested"] = multiControllerGetRequestedLocalPwmCount();
  doc["mc_local_pwm_outputs_effective"] = multiControllerGetEffectiveLocalPwmCount();
  doc["mc_local_pwm_outputs_active"] = multiControllerGetActiveLocalPwmCount();
  doc["mc_group_pwm_requested_pct"] = multiControllerGetRequestedGroupPercent();
  doc["mc_group_pwm_allowed_pct"] = multiControllerGetAllowedGroupPercent();
  doc["mc_group_pwm_assigned_pct"] = multiControllerGetAssignedGroupPercent();
  doc["mc_scenario"] = multiControllerGetScenarioLabel();
  doc["mc_negative_override_active"] = multiControllerIsNegativePriorityOverrideActive();
  doc["mc_surplus_group_max_pct"] = multiControllerGetSurplusGroupMaxPercent();
  doc["mc_group_model_count"] = multiControllerGetGroupModelParticipantCount();
  doc["mc_group_model_local_count"] = multiControllerGetGroupModelLocalParticipantCount();
  doc["mc_group_model_remote_count"] = multiControllerGetGroupModelRemoteParticipantCount();
  doc["mc_group_model_clamped"] = multiControllerIsGroupModelClamped();
  doc["mc_group_model_reason"] = multiControllerGetGroupModelReason();
  doc["mc_peers_configured"] = multiControllerGetConfiguredPeerCount();
  doc["mc_peers_online"] = multiControllerGetOnlinePeerCount();
  doc["mc_peers_healthy"] = multiControllerGetHealthyPeerCount();
  doc["mc_peers_writeback_ok"] = multiControllerGetWritebackHealthyPeerCount();
  doc["mc_internal_coordinator"] = multiControllerIsInternalCoordinator();
  doc["mc_internal_coordinator_reason"] = multiControllerGetInternalCoordinatorReason();
  doc["mc_remote_target_active"] = multiControllerIsRemoteTargetControlActive();
  doc["mc_remote_target_age_ms"] = multiControllerGetRemoteTargetAgeMs();
  doc["mc_remote_target_reason"] = multiControllerGetRemoteTargetReason();
  doc["mc_remote_target_owner_ip"] = multiControllerGetRemoteTargetOwnerIp();
  // MQTT status compact houden: per-slot en groepsdetails lopen al via eigen mc/... state-topics.
  // Door de zware arrays hier weg te laten blijft <base>/status ook in Multi Controller-modus
  // bruikbaar voor Home Assistant value_json-sensoren en algemene status/debug.
}

void appendMultiControllerApiStatus(JsonDocument& doc) {
  doc["mc_enabled"] = mcEnabled;
  doc["mc_mode"] = mcMode;
  doc["mc_controller_count"] = mcControllerCount;
  doc["mc_self_id"] = mcLocalControllerId;
  appendMultiControllerRoleStatus(doc);
  doc["mc_group"] = mcGroupName;
  doc["mc_config_ok"] = multiControllerIsConfigValid();
  doc["mc_runtime_ok"] = multiControllerIsRuntimeValid();
  doc["mc_fallback_active"] = multiControllerIsFallbackActive();
  doc["mc_fallback_reason"] = multiControllerGetFallbackReason();
  doc["mc_local_pwm_outputs_requested"] = multiControllerGetRequestedLocalPwmCount();
  doc["mc_local_pwm_outputs_effective"] = multiControllerGetEffectiveLocalPwmCount();
  doc["mc_local_pwm_outputs_active"] = multiControllerGetActiveLocalPwmCount();
  doc["mc_group_pwm_requested_pct"] = multiControllerGetRequestedGroupPercent();
  doc["mc_group_pwm_allowed_pct"] = multiControllerGetAllowedGroupPercent();
  doc["mc_group_pwm_assigned_pct"] = multiControllerGetAssignedGroupPercent();
  doc["mc_scenario"] = multiControllerGetScenarioLabel();
  doc["mc_negative_override_active"] = multiControllerIsNegativePriorityOverrideActive();
  doc["mc_surplus_group_max_pct"] = multiControllerGetSurplusGroupMaxPercent();
  appendMultiControllerLocalPwmArray(doc);
}

void appendMultiControllerPeerCompactStatus(JsonDocument& doc) {
  doc["ok"] = true;
  doc["mc_role"] = mcTelemetryRoleLabel();
  doc["mc_fallback_active"] = multiControllerIsFallbackActive();
  doc["mc_group_name"] = mcGroupName;
  doc["mc_local_controller_id"] = mcLocalControllerId;
  doc["mc_local_pwm_outputs_active"] = multiControllerGetActiveLocalPwmCount();
  doc["mc_local_pwm_outputs_effective"] = multiControllerGetEffectiveLocalPwmCount();
  doc["mc_group_pwm_requested_pct"] = multiControllerGetRequestedGroupPercent();
  doc["mc_group_pwm_allowed_pct"] = multiControllerGetAllowedGroupPercent();
  doc["mc_group_pwm_assigned_pct"] = multiControllerGetAssignedGroupPercent();
  doc["mc_scenario"] = multiControllerGetScenarioLabel();
  appendMultiControllerPeerStatusOutputs(doc);
}

void appendMultiControllerApiStatusLight(JsonDocument& doc) {
  doc["mc_fallback_active"] = multiControllerIsFallbackActive();
  doc["mc_group_name"] = mcGroupName;
  doc["mc_local_controller_id"] = mcLocalControllerId;
  appendMultiControllerRoleStatus(doc);
  doc["mc_internal_coordinator"] = multiControllerIsInternalCoordinator();
  doc["mc_internal_coordinator_reason"] = multiControllerGetInternalCoordinatorReason();
  doc["mc_local_pwm_outputs_requested"] = multiControllerGetRequestedLocalPwmCount();
  doc["mc_local_pwm_outputs_effective"] = multiControllerGetEffectiveLocalPwmCount();
  doc["mc_local_pwm_outputs_active"] = multiControllerGetActiveLocalPwmCount();
  doc["mc_group_pwm_requested_pct"] = multiControllerGetRequestedGroupPercent();
  doc["mc_group_pwm_allowed_pct"] = multiControllerGetAllowedGroupPercent();
  doc["mc_group_pwm_assigned_pct"] = multiControllerGetAssignedGroupPercent();
  doc["mc_scenario"] = multiControllerGetScenarioLabel();
  doc["mc_negative_override_active"] = multiControllerIsNegativePriorityOverrideActive();
  doc["mc_surplus_group_max_pct"] = multiControllerGetSurplusGroupMaxPercent();
  doc["mc_group_model_reason"] = multiControllerGetGroupModelReason();
  doc["mc_peers_configured"] = multiControllerGetConfiguredPeerCount();
  doc["mc_peers_online"] = multiControllerGetOnlinePeerCount();
  doc["mc_peers_healthy"] = multiControllerGetHealthyPeerCount();
  doc["mc_peers_writeback_ok"] = multiControllerGetWritebackHealthyPeerCount();
  doc["mc_remote_target_active"] = multiControllerIsRemoteTargetControlActive();
  doc["mc_remote_target_age_ms"] = multiControllerGetRemoteTargetAgeMs();
  doc["mc_remote_target_reason"] = multiControllerGetRemoteTargetReason();
  doc["mc_remote_target_owner_ip"] = multiControllerGetRemoteTargetOwnerIp();
  appendMultiControllerCompactPeerOutputs(doc);
}
