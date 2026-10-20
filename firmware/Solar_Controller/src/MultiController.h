#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WebServer.h>

static constexpr uint8_t MC_MAX_SLOTS = 3;
static constexpr uint8_t MC_ROLE_STANDALONE = 0;
static constexpr uint8_t MC_ROLE_MASTER = 1;
static constexpr uint8_t MC_ROLE_SLAVE = 2;

static constexpr uint8_t MC_MODE_DISABLED = 0;
static constexpr uint8_t MC_MODE_LOCAL_PWM = 1;
static constexpr uint8_t MC_MODE_NETWORK = 2;
static constexpr uint8_t MC_MODE_HYBRID = 3;

static constexpr uint8_t MC_SLOT_LOCAL_PWM = 1;
static constexpr uint8_t MC_SLOT_NETWORK_PEER = 2;

static constexpr uint8_t MC_SCENARIO_IDLE = 0;
static constexpr uint8_t MC_SCENARIO_SURPLUS = 1;
static constexpr uint8_t MC_SCENARIO_CHEAP = 2;
static constexpr uint8_t MC_SCENARIO_NEGATIVE = 3;
static constexpr uint8_t MC_SCENARIO_MANUAL_FORCE = 4;
static constexpr uint8_t MC_SCENARIO_PRIORITY_HEAT = 5;

struct MultiControllerSlotConfig {
  uint8_t slotType;
  String  name;
  String  peerIp;
  int     pwmPin;
  uint8_t assignedPhase;
  uint8_t rampUpOrder;
  uint8_t rampDownOrder;
  uint8_t startAfterPercent;
  uint8_t minPercent;
  uint8_t maxPercent;
  bool    useOnSurplus;
  bool    useOnCheap;
  bool    useOnNegative;
  bool    allowManualForce;
};

extern bool mcEnabled;
extern uint8_t mcRole;
extern uint8_t mcLastActiveRole;
extern uint8_t mcMode;
extern uint8_t mcControllerCount;
extern uint8_t mcLocalControllerId;
extern String mcGroupName;
extern bool mcSlaveAllowLocalLegionellaOverride;
extern MultiControllerSlotConfig mcSlots[MC_MAX_SLOTS];

void multiControllerSetDefaults();
void multiControllerClamp();
void multiControllerLoadConfig(Preferences& prefs);
void multiControllerSaveConfig(Preferences& prefs);
void multiControllerApplySettings(WebServer& server);
bool multiControllerValidateCurrentConfig(String& reason);
String multiControllerBuildSettingsSection();

void multiControllerPrepareSafePins();
void multiControllerLoop();
bool multiControllerInitLocalPwmRuntime(uint32_t pwmFreqHz, uint8_t pwmResolutionBits);
void multiControllerApplyLocalPwmState(uint32_t primaryDuty);
void multiControllerReconfigureLocalPwmRuntime(uint32_t pwmFreqHz, uint8_t pwmResolutionBits, uint32_t primaryDuty);
uint8_t multiControllerGetRequestedLocalPwmCount();
uint8_t multiControllerGetEffectiveLocalPwmCount();
uint8_t multiControllerGetActiveLocalPwmCount();
bool multiControllerIsLocalPwmSlotConfigured(uint8_t slotIndex);
bool multiControllerIsLocalPwmSlotActive(uint8_t slotIndex);
bool multiControllerIsLocalPwmSlotAttached(uint8_t slotIndex);
int multiControllerGetLocalPwmSlotPin(uint8_t slotIndex);
uint8_t multiControllerGetLocalPwmSlotChannel(uint8_t slotIndex);
float multiControllerGetLocalPwmSlotRequestedPercent(uint8_t slotIndex);
float multiControllerGetLocalPwmSlotAllowedPercent(uint8_t slotIndex);
float multiControllerGetLocalPwmSlotPercent(uint8_t slotIndex);
float multiControllerGetLocalPwmSlotHeadroomPercent(uint8_t slotIndex);
bool multiControllerIsLocalPwmSlotBlocked(uint8_t slotIndex);
bool multiControllerLocalSlotHasHeadroom(uint8_t slotIndex);
String multiControllerGetLocalPwmSlotBlockReason(uint8_t slotIndex);
float multiControllerGetRequestedGroupPercent();
float multiControllerGetAllowedGroupPercent();
float multiControllerGetAssignedGroupPercent();
uint8_t multiControllerGetScenario();
String multiControllerGetScenarioLabel();
bool multiControllerIsNegativePriorityOverrideActive();
bool multiControllerShouldUseGroupControlForSurplus();
bool multiControllerShouldUseGroupControlForScenario(uint8_t scenario);
bool multiControllerScenarioUsesParallelPriorityTargets(uint8_t scenario);
float multiControllerGetSurplusGroupMaxPercent();
float multiControllerGetScenarioGroupMaxPercent(uint8_t scenario);
float multiControllerGetScenarioSoftStartGroupStartPercent(uint8_t scenario, float startPctPerController);
void multiControllerSetAppScenario(uint8_t scenario);
void multiControllerApplyRequestedGroupPercent(float requestedPercent);
void multiControllerApplyRequestedGroupPercentForScenario(float requestedPercent, uint8_t scenario);
float multiControllerGetPrimaryAssignedPercent();

uint8_t multiControllerGetSlotAssignedPhase(uint8_t slotIndex);
void multiControllerSetSlotAssignedPhase(uint8_t slotIndex, uint8_t phase123);

bool multiControllerIsConfigValid();
bool multiControllerIsRuntimeValid();
bool multiControllerIsFallbackActive();
String multiControllerGetFallbackReason();
bool multiControllerHasNetworkPeersConfigured();
bool multiControllerIsInternalCoordinator();
String multiControllerGetInternalCoordinatorReason();
uint8_t multiControllerGetGroupModelParticipantCount();
uint8_t multiControllerGetGroupModelLocalParticipantCount();
uint8_t multiControllerGetGroupModelRemoteParticipantCount();
bool multiControllerIsGroupModelClamped();
String multiControllerGetGroupModelReason();
uint8_t multiControllerGetConfiguredPeerCount();
uint8_t multiControllerGetOnlinePeerCount();
uint8_t multiControllerGetHealthyPeerCount();
uint8_t multiControllerGetWritebackHealthyPeerCount();

bool multiControllerApplyRemoteTargetsDocument(const DynamicJsonDocument& doc, String& errOut);
bool multiControllerIsRemoteTargetControlActive();
uint32_t multiControllerGetRemoteTargetAgeMs();
String multiControllerGetRemoteTargetReason();
String multiControllerGetRemoteTargetOwnerIp();
bool multiControllerIsMasterRole();
bool multiControllerIsSlaveRole();
bool multiControllerSlaveLegionellaOverrideEnabled();
bool multiControllerSlaveLegionellaOverrideActive();
String multiControllerGetSlaveMasterIp();

bool multiControllerTcpRealtimeAvailable();
bool multiControllerTcpSlaveServerListening();
uint16_t multiControllerGetTcpControlPort();
bool multiControllerIsPeerTcpConnected(uint8_t slotIndex);
uint32_t multiControllerGetPeerTcpAgeMs(uint8_t slotIndex);
uint32_t multiControllerGetPeerTcpRoundTripMs(uint8_t slotIndex);
uint32_t multiControllerGetPeerTcpReconnectCount(uint8_t slotIndex);

