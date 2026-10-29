#pragma once
#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "MqttParsers.h"

// Device identifiers (defined in .ino)
extern const char* DEVICE_ID;
extern const char* DEVICE_NAME;
extern const char* FW_VERSION;

// Home Assistant discovery prefix (defined in .ino)
extern const char* HA_DISCOVERY_PREFIX;

// ---------- Control mode ----------
enum ControlMode : uint8_t { MODE_NORMAL = 0, MODE_FORCE_HEAT = 1 };
extern ControlMode controlMode;
void saveConfig();
// -----------------------------------

// Shared compile-time settings used in multiple translation units (.ino + .cpp)
#ifndef MQTT_MAX_PACKET_SIZE
  #define MQTT_MAX_PACKET_SIZE 4096
#endif

#ifndef MQTT_CLIENT_BUFFER_SIZE
  #define MQTT_CLIENT_BUFFER_SIZE 4096
#endif

#ifndef USE_PUBSUBCLIENT_SETBUFFERSIZE
  #define USE_PUBSUBCLIENT_SETBUFFERSIZE 1
#endif

// ---------- MQTT core globals (defined in .ino) ----------
extern PubSubClient mqttClient;
extern unsigned long lastMqttReconnectAttempt;
extern const unsigned long MQTT_RECONNECT_INTERVAL_MS;

extern bool   mqttEnabled;
extern String meterType;
extern String meterIp;
extern long   lastNetPowerW;
extern String mqttHost;
extern uint16_t mqttPort;
extern String mqttUser;
extern String mqttPassStr;
extern String mqttBaseTopic;

extern bool   haDiscoveryEnabled;

// MQTT/status publish settings (defined in .ino)
extern bool sysPublishEnabled;
extern int  sysPublishIntervalSec;
extern int  statusPublishIntervalSec;
extern unsigned long lastSysPublishMs;
extern unsigned long lastStatusPublishMs;
extern unsigned long lastStatusActualPublishMs;

// ---------- Helper functions (defined in .ino) ----------
String mqttBase();
String mqttAvailabilityTopic();
String getMqttClientId();
void mqttCallback(char* topic, byte* payload, unsigned int length);

// ---------- Publish helpers (defined in .ino) ----------
void mqttPublishAvailability(const char* s);
void mqttPublishHADiscovery(bool force = false);
void mqttPublishForceState(bool force = false);
void mqttPublishRelayState(bool force = false);
void mqttPublishMultiControllerStates(bool force = false);
void mqttPublishCoreLiveStates(bool force = false);
void mqttPublishLegionellaConfig(bool force = false);
void mqttPublishLegionellaState(bool force = false);
void mqttPublishTuningStates(bool force = false);
void mqttPublishLimitsStates(bool force = false);
void mqttPublishNormalConfigStates(bool force = false);
void mqttPublishLegionellaPlanStates(bool force = false);
void mqttPublishSunStrategyStates(bool force = false);
void mqttPublishElecPriceStates(bool force = false);
void mqttPublishNordpoolStates(bool force = false);
void mqttPublishHeatCheapConfigStates(bool force = false);
void mqttPublishStatusJson(bool force = false);
void mqttPublishSystemJson(bool force);

// ---------- Step 7 prep: helper signatures are in MqttParsers.h ----------
void legStart(const String& reason);
void legStop(const String& reason);
// ------------------------------------------------------------------------

// Legionella state (defined in .ino)
extern bool legRunning;

// Legionella config variables (defined in .ino)
// FIXED: Renamed legStartMinuteute -> legStartMinute
extern bool  legScheduleEnabled;
extern int   legDow;
extern int   legStartHour;
extern int   legStartMinute;  // ✅ FIXED: was legStartMinuteute
extern int   legDurationMin;
extern float legOutputLimitPercent;
extern bool  legUseTempSensor;
extern float legTargetTempC;
extern int   legHoldMin;
extern bool  legRelayAutoEnabled;
extern int   legRelayStartDelayMin;
extern int   legRelayOffDelayMin;

// Helpers (defined in .ino)
void clampAll();

// Tuning helpers (defined in .ino)
void applyTuningFromJson(const String& json);

// Tuning setters (defined in .ino)
void setTuningAlgorithm(const String& payload);
void setTuningStepFactor(const String& payload);
void setTuningDeadband(const String& payload);
void setTuningMaxStep(const String& payload);

// Config apply helper (defined in ApiRoutes.cpp)
bool apiApplyConfigFromJson(const JsonDocument& in, bool& rebootRequired, bool& mqttParamsChanged);

// ---------- Nordpool globals (defined in .ino) ----------
// FIXED: Removed duplicate declarations
extern bool   npEnabled;
extern String npArea;
extern int    npResMin;
extern bool   npInclTaxEnabled;
extern uint8_t npBoostPriceSource;
extern bool   npBoostEnabled;
extern float  npBoostThresholdCt;
extern int    npBoostNBlocks;

// Sun / schedule globals (defined in .ino)
extern uint8_t sunSchedMode;
extern int     sunWinStartMin;
extern int     sunWinEndMin;
extern int     sunCheapNBlocks;
extern int     sunDaylightMarginMin;
extern float   sunLat;
extern float   sunLon;
extern String  sunWinDate;
extern int     sunDaylightStartMin;
extern int     sunDaylightEndMin;
extern int     sunCheapestStartMin;
extern int     sunCheapestEndMin;

// ---------- Shared runtime helpers (defined in .ino) ----------
extern bool configMode;
extern bool softStartEnabled;
extern float softStartStartPct;
extern uint16_t softStartDurationSec;
extern bool lastMeterOk;
extern float minStepPercent;
extern unsigned long minUpdateIntervalMs;
extern float maxOutputPercentLimit;
extern unsigned long lastUpdateMs;
extern float lastDutyPercent;
extern const uint32_t PWM_BOOT_LOCK_MS;
extern uint32_t pwmBootStartMs;
extern bool pwmSeenValidMeterSample;
String meterLabel();
bool readMeterPower(long &powerW);
bool readMeterPhases(float &i1, float &i2, float &i3);
bool timeIsValid();
float appGetPhaseCurrentA(uint8_t phase123);
float appApplyPrioritySoftStartWithStart(float requestedPct, bool prioActive, bool gateActive, float startPct, float currentAppliedPct, bool syncToCurrentApplied);
bool appPriorityHeatingActiveNow();
bool appNormalAllowedByScheduleNow();
uint8_t appMultiControllerCurrentAppScenario();
void appRefreshSmartGatewaysPhasesForGuard();
uint32_t dateIntLocal(time_t t);
time_t computeNextLegionellaEpoch();
bool tempIsValid(float c);
void updateTempIfNeeded();
extern bool ds18Available;
extern float lastTempC;
void setLegionellaOutput(bool on);
void setBoilerDutyPercent(float pct);
void setBoilerDutyPercentForScenario(float pct, uint8_t forcedScenario);
void setBoilerGroupDutyPercent(float pct);
float multiControllerGroupTargetMaxForScenario(uint8_t scenario);
bool pwmInit();
void pwmWriteDuty(uint32_t duty);

// ---------- Legionella globals (defined in .ino) ----------
// FIXED: Renamed legStartMinuteute -> legStartMinute
extern bool  legScheduleEnabled;  // Already declared above - keeping for compatibility
extern int   legDow;              // Already declared above - keeping for compatibility
extern int   legStartHour;        // Already declared above - keeping for compatibility
extern int   legStartMinute;      // ✅ FIXED: was legStartMinuteute
extern int   legDurationMin;      // Already declared above - keeping for compatibility
extern float legOutputLimitPercent; // Already declared above - keeping for compatibility
extern bool  legUseTempSensor;    // Already declared above - keeping for compatibility
extern float legTargetTempC;      // Already declared above - keeping for compatibility
extern int   legHoldMin;          // Already declared above - keeping for compatibility
extern bool  legRelayAutoEnabled; // Already declared above - keeping for compatibility
extern int   legRelayStartDelayMin; // Already declared above - keeping for compatibility
extern int   legRelayOffDelayMin; // Already declared above - keeping for compatibility
extern String legTempAddrStr;
extern uint32_t legLastRunDateInt;
extern uint32_t legLastRunEpoch;
extern uint32_t legLastSuccessEpoch;
extern bool   legRunning;         // Already declared above - keeping for compatibility
extern bool   legHoldActive;
extern time_t legHoldEndEpoch;
extern time_t legEndEpoch;
extern String legReason;

// --- Nordpool runtime/state (used by nordpool.cpp) ---
// FIXED: Removed duplicate declarations that were already declared above
extern float npEnergyTaxCt;
extern float npInkoopCt;
extern float npBtwPct;
// npBoostEnabled and npBoostNBlocks already declared in Nordpool globals section above
extern String npDateToday;
extern String npDateTomorrow;
extern String npUpdatedAt;
extern unsigned long npLastPollMs;
extern unsigned long npLastFetchMs;
extern bool  npLastOkToday;
extern bool  npLastOkTomorrow;
extern int   npLastHttp;
extern String npLastError;
extern int   npTodayN;
extern int   npTomorrowN;
extern float npTodayCt[];
extern float npTomorrowCt[];
extern int   npIntervalMinToday;
extern int   npIntervalMinTomorrow;
extern int   npBoostSelectedN;
extern int   npBoostEligibleN;
extern int   npBoostNegativeN;
extern bool  npBoostActiveNow;
extern int   npBoostIdx[];
float npBoostPriceCtKwh(float spotCtKwh);

// Heat comparison runtime (defined in .ino)
extern bool  heatAutoEnabled;
extern bool  heatExecElecWhenCheaper;
extern bool  heatCheapBlocksEnabled;
extern bool  heatCheapUseBoilerTarget;
extern int   heatCheapNBlocks;
extern uint8_t heatCheapDayParts;
extern int   heatCheapBoundary1Min;
extern int   heatCheapBoundary2Min;
extern bool  heatRecommendGasNow;
extern bool  heatPriceDecisionValidNow;
extern uint8_t elecPriceSource;
extern float elecFixedCtKwhAllIn;
extern float targetExportW;
extern float maxBoilerPowerW;
extern float deadbandW;
extern float stepFactor;
extern float maxStepPctPerUpdatePerUpdate;
extern float lastErrorW;
extern float lastOvershootW;
extern uint8_t normalAlg;
extern bool  gasTotalEnabled;
extern float gasKwhPerM3;
extern float gasBoilerEff;
extern float elecBoilerEff;
extern float heatGasCtPerKwhHeatNow;
extern float heatElecCtPerKwhHeatNow;
extern float heatElecMarginCt;
extern int   heatMinSwitchMin;
extern unsigned long heatLastSwitchMs;
extern bool   heatCheapPeriodLatched;
extern bool   heatCheapPlanValid;
extern String heatCheapPlanStartDate;
extern String heatCheapPlanEndDate;
extern int    heatCheapPlanStartIdx;
extern int    heatCheapPlanEndIdxExcl;
extern int    heatCheapPlanIntervalMin;
extern int    heatCheapPlanSelectedBlocks;
extern bool   phaseGuardEnabled;
extern uint8_t phaseGuardPhase;
extern float  phaseGuardMaxA;
extern float  phaseGuardMaxA_L1;
extern float  phaseGuardMaxA_L2;
extern float  phaseGuardMaxA_L3;
extern bool   phaseGuardModeActive;
extern bool   phaseGuardWouldLimit;
extern float  phaseGuardSelectedA;
extern float  phaseGuardOverA;
extern bool   dashShowSunWindowLabel;
float getConfiguredPhaseGuardMaxA(uint8_t phase123);
extern bool   lastPhasesOk;
extern float  lastPhaseI1A;
extern float  lastPhaseI2A;
extern float  lastPhaseI3A;
extern unsigned long lastPhasesUpdateMs;
extern const unsigned long PHASES_STALE_MS;
// Helpers provided by main firmware (.ino)
String isoDateLocal(time_t t);
String dmyDateLocal(time_t t);
String gasDayIsoLocal(time_t t);
String isoDateAddDays(const String& iso, int deltaDays);
String fmtDateDMYFromIso(const String& iso);
void recomputeCheapestDaylightWindowIfNeeded();
String fmtTimeLocal(time_t t);
int isoDeltaMinutes(const String& isoStart, const String& isoEnd);
float elecCtKwhNow();
float gasEurM3Now(bool total);
float gasEurM3ForControlNow(bool total);
float gasEurM3ForControlAtLocal(const String& localDate, int minuteOfDay, bool total);
