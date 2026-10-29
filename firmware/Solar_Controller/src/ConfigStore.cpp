#include "ConfigStore.h"

#include <Preferences.h>
#include <math.h>

#include "app_globals.h"
#include "RelayControl.h"
#include "MultiController.h"
#include "ControllerTemperatureGate.h"
#include "PhaseGuard.h"
#include "sensors.h"

// ----- Globals defined in the main .ino -----
extern Preferences prefs;

extern String wifiSsid;
extern String wifiPass;
extern String deviceName;
extern String meterType;
extern String meterIp;

extern float maxBoilerPowerW;
extern float targetExportW;
extern float minStepPercent;
extern unsigned long minUpdateIntervalMs;
extern float maxOutputPercentLimit;

extern uint8_t normalAlg;
extern float stepFactor;
extern float deadbandW;
extern float maxStepPctPerUpdatePerUpdate;

extern bool   mqttEnabled;
extern String mqttHost;
extern uint16_t mqttPort;
extern String mqttUser;
extern String mqttPassStr;
extern String mqttBaseTopic;

extern uint8_t elecPriceSource;
extern float   elecFixedCtKwhAllIn;

extern bool   npEnabled;
extern String npArea;
extern int    npResMin;
extern bool   npInclTaxEnabled;
extern float  npEnergyTaxCt;
extern float  npInkoopCt;
extern float  npBtwPct;
extern bool   npBoostEnabled;
extern uint8_t npBoostPriceSource;
extern float  npBoostThresholdCt;
extern int    npBoostNBlocks;

extern bool    gasTotalEnabled;
extern uint8_t gasPriceSource;
extern float   gasFixedEurM3;
extern float   gasEnergyTaxEur;
extern float   gasInkoopEur;
extern float   gasBtwPct;

extern bool  heatAutoEnabled;
extern bool  heatExecElecWhenCheaper;
extern bool  heatCheapBlocksEnabled;
extern bool  heatCheapUseBoilerTarget;
extern int   heatCheapNBlocks;
extern uint8_t heatCheapDayParts;
extern int   heatCheapBoundary1Min;
extern int   heatCheapBoundary2Min;
extern float gasKwhPerM3;
extern float gasBoilerEff;
extern float elecBoilerEff;
extern float heatElecMarginCt;
extern int   heatMinSwitchMin;

extern uint8_t sunSchedMode;
extern int     sunWinStartMin;
extern int     sunWinEndMin;
extern int     sunCheapNBlocks;
extern int     sunDaylightMarginMin;
extern float   sunLat;
extern float   sunLon;
extern bool    dashShowSunWindowLabel;

extern bool uiDarkMode;

extern bool    phaseGuardEnabled;
extern uint8_t phaseGuardPhase;
extern float   phaseGuardMaxA;
extern float   phaseGuardMaxA_L1;
extern float   phaseGuardMaxA_L2;
extern float   phaseGuardMaxA_L3;

extern bool     softStartEnabled;
extern float    softStartStartPct;
extern uint16_t softStartDurationSec;

extern bool  legScheduleEnabled;
extern int   legDow;
extern int   legStartHour;
extern int   legStartMinute;
extern int   legDurationMin;
extern float legOutputLimitPercent;
extern bool  legUseTempSensor;
extern String legTempAddrStr;
extern float legTargetTempC;
extern int   legHoldMin;
extern bool  legRelayAutoEnabled;
extern int   legRelayStartDelayMin;
extern int   legRelayOffDelayMin;
extern uint32_t legLastRunDateInt;
extern uint32_t legLastRunEpoch;
extern uint32_t legLastSuccessEpoch;

extern bool haDiscoveryEnabled;
extern bool sysPublishEnabled;
extern int  sysPublishIntervalSec;
extern int  statusPublishIntervalSec;

extern bool fwAutoUpdateEnabled;

namespace {
constexpr const char* PREF_NAMESPACE = "boilerbox";
constexpr const char* PREF_FW_AUTO_KEY = "fw_auto";

constexpr float kDefaultMaxBoilerPowerW = 2000.0f;
constexpr float kDefaultTargetExportW = -50.0f;
constexpr float kDefaultMinStepPercent = 1.0f;
constexpr unsigned long kDefaultMinIntervalMs = 4000UL;
constexpr float kDefaultMaxOutputPercent = 100.0f;

constexpr uint8_t kDefaultNormalAlg = 1;
constexpr float kDefaultStepFactor = 0.20f;
constexpr float kDefaultDeadbandW = 30.0f;
constexpr float kDefaultMaxStepPct = 10.0f;

constexpr uint8_t kDefaultElecPriceSource = 0;
constexpr float kDefaultElecFixedCtKwhAllIn = 0.0f;

constexpr bool  kDefaultNpEnabled = true;
constexpr const char* kDefaultNpArea = "NL";
constexpr int   kDefaultNpResMin = 60;
constexpr bool  kDefaultNpInclTaxEnabled = false;
constexpr float kDefaultNpEnergyTaxCt = 0.0f;
constexpr float kDefaultNpInkoopCt = 0.0f;
constexpr float kDefaultNpBtwPct = 21.0f;
constexpr bool  kDefaultNpBoostEnabled = false;
constexpr uint8_t kDefaultNpBoostPriceSource = 0;
constexpr float kDefaultNpBoostThresholdCt = 0.0f;
constexpr int   kDefaultNpBoostNBlocks = 2;
constexpr int   kNpMaxPoints = 96;

constexpr bool  kDefaultGasTotalEnabled = false;
constexpr uint8_t kDefaultGasPriceSource = 0;
constexpr float kDefaultGasFixedEurM3 = 0.0f;
constexpr float kDefaultGasEnergyTaxEur = 0.0f;
constexpr float kDefaultGasInkoopEur = 0.0f;
constexpr float kDefaultGasBtwPct = 21.0f;

constexpr bool  kDefaultHeatAutoEnabled = false;
constexpr bool  kDefaultHeatExecElecWhenCheaper = false;
constexpr bool  kDefaultHeatCheapBlocksEnabled = false;
constexpr bool  kDefaultHeatCheapUseBoilerTarget = false;
constexpr int   kDefaultHeatCheapNBlocks = 4;
constexpr uint8_t kDefaultHeatCheapDayParts = 1;
constexpr int   kDefaultHeatCheapBoundary1Min = 7 * 60;
constexpr int   kDefaultHeatCheapBoundary2Min = 19 * 60;
constexpr float kDefaultGasKwhPerM3 = 9.77f;
constexpr float kDefaultGasBoilerEff = 0.90f;
constexpr float kDefaultElecBoilerEff = 1.00f;
constexpr float kDefaultElecMarginCt = 0.0f;
constexpr int   kDefaultHeatMinSwitchMin = 30;

constexpr uint8_t kDefaultSunSchedMode = 0;
constexpr int kDefaultSunWinStartMin = 9 * 60;
constexpr int kDefaultSunWinEndMin = 16 * 60;
constexpr int kDefaultSunCheapNBlocks = 4;
constexpr int kDefaultSunDaylightMarginMin = 0;
constexpr float kDefaultSunLat = 51.90f;
constexpr float kDefaultSunLon = 4.40f;

constexpr bool kDefaultPhaseGuardEnabled = false;
constexpr uint8_t kDefaultPhaseGuardPhase = 1;
constexpr float kDefaultPhaseGuardMaxA = 23.0f;

constexpr bool kDefaultSoftStartEnabled = true;
constexpr uint16_t kDefaultSoftStartDurationSec = 10;
constexpr float kDefaultSoftStartStartPct = 10.0f;

constexpr bool  kDefaultLegEnabled = false;
constexpr int   kDefaultLegDow = 0;
constexpr int   kDefaultLegHour = 13;
constexpr int   kDefaultLegMinute = 0;
constexpr int   kDefaultLegDurationMin = 90;
constexpr float kDefaultLegOutputLimitPct = 100.0f;
constexpr bool  kDefaultLegUseTempSensor = false;
constexpr float kDefaultLegTargetTempC = 60.0f;
constexpr int   kDefaultLegHoldMin = 15;
constexpr bool  kDefaultLegRelayAuto = false;
constexpr int   kDefaultLegRelayStartDelayMin = 0;
constexpr int   kDefaultLegRelayDelayMin = 0;

constexpr bool kDefaultSysPubEnabled = true;
constexpr int  kDefaultSysPubIntervalSec = 60;
constexpr int  kDefaultStatusPubIntervalSec = 5;

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

} // namespace

void clampAll() {
  if (legDow < 0) legDow = 0;
  if (legDow > 6) legDow = 6;
  if (legStartHour < 0) legStartHour = 0;
  if (legStartHour > 23) legStartHour = 23;
  if (legStartMinute < 0) legStartMinute = 0;
  if (legStartMinute > 59) legStartMinute = 59;

  if (legDurationMin < 1) legDurationMin = 1;
  if (legDurationMin > 360) legDurationMin = 360;
  if (legOutputLimitPercent < 0) legOutputLimitPercent = 0;
  if (legOutputLimitPercent > 100) legOutputLimitPercent = 100;

  if (maxOutputPercentLimit < 0) maxOutputPercentLimit = 0;
  if (maxOutputPercentLimit > 100) maxOutputPercentLimit = 100;
  if (maxBoilerPowerW < 100) maxBoilerPowerW = 100;
  if (minStepPercent <= 0) minStepPercent = 0.1f;
  if (minUpdateIntervalMs > 60000UL) minUpdateIntervalMs = 60000UL;

  if (phaseGuardPhase < 1 || phaseGuardPhase > 3) phaseGuardPhase = kDefaultPhaseGuardPhase;
  if (!isfinite(phaseGuardMaxA_L1) || phaseGuardMaxA_L1 < 1.0f) phaseGuardMaxA_L1 = kDefaultPhaseGuardMaxA;
  if (!isfinite(phaseGuardMaxA_L2) || phaseGuardMaxA_L2 < 1.0f) phaseGuardMaxA_L2 = kDefaultPhaseGuardMaxA;
  if (!isfinite(phaseGuardMaxA_L3) || phaseGuardMaxA_L3 < 1.0f) phaseGuardMaxA_L3 = kDefaultPhaseGuardMaxA;
  phaseGuardMaxA_L1 = clampf(phaseGuardMaxA_L1, 1.0f, 63.0f);
  phaseGuardMaxA_L2 = clampf(phaseGuardMaxA_L2, 1.0f, 63.0f);
  phaseGuardMaxA_L3 = clampf(phaseGuardMaxA_L3, 1.0f, 63.0f);
  refreshLegacyPhaseGuardMaxA();

  if (softStartDurationSec > 30) softStartDurationSec = 30;
  if (softStartStartPct < 0.0f) softStartStartPct = 0.0f;
  if (softStartStartPct > 30.0f) softStartStartPct = 30.0f;

  if (legTargetTempC < 40) legTargetTempC = 40;
  if (legTargetTempC > 75) legTargetTempC = 75;
  if (legHoldMin < 0) legHoldMin = 0;
  if (legHoldMin > 120) legHoldMin = 120;
  if (legRelayStartDelayMin < 0) legRelayStartDelayMin = 0;
  if (legRelayStartDelayMin > 60) legRelayStartDelayMin = 60;
  if (legRelayOffDelayMin < 0) legRelayOffDelayMin = 0;
  if (legRelayOffDelayMin > 60) legRelayOffDelayMin = 60;

  if (legTempAddrStr.length() != 16) legTempAddrStr = "";

  if (meterType != "youless" && meterType != "homewizard" && meterType != "smartgateways") meterType = "youless";

  if (sysPublishIntervalSec < 10) sysPublishIntervalSec = 10;
  if (sysPublishIntervalSec > 3600) sysPublishIntervalSec = 3600;
  if (statusPublishIntervalSec < 1) statusPublishIntervalSec = 1;
  if (statusPublishIntervalSec > 3600) statusPublishIntervalSec = 3600;

  if (npResMin != 15 && npResMin != 60) npResMin = 60;
  npArea.trim();
  if (npArea.length() == 0) npArea = kDefaultNpArea;

  if (npEnergyTaxCt < 0) npEnergyTaxCt = 0;
  if (npInkoopCt < 0) npInkoopCt = 0;
  npEnergyTaxCt = clampf(npEnergyTaxCt, 0.0f, 500.0f);
  npInkoopCt    = clampf(npInkoopCt,    0.0f, 500.0f);
  npBtwPct      = clampf(npBtwPct,      0.0f, 100.0f);

  if (npBoostPriceSource > 1) npBoostPriceSource = 0;
  npBoostThresholdCt = clampf(npBoostThresholdCt, -500.0f, 500.0f);
  if (npBoostNBlocks < 0) npBoostNBlocks = 0;
  if (npBoostNBlocks > kNpMaxPoints) npBoostNBlocks = kNpMaxPoints;

  if (heatCheapNBlocks < 1) heatCheapNBlocks = 1;
  if (heatCheapNBlocks > kNpMaxPoints) heatCheapNBlocks = kNpMaxPoints;
  if (heatCheapDayParts < 1 || heatCheapDayParts > 2) heatCheapDayParts = 1;
  if (heatCheapBoundary1Min < 0) heatCheapBoundary1Min = 0;
  if (heatCheapBoundary1Min > 23 * 60 + 59) heatCheapBoundary1Min = 23 * 60 + 59;
  if (heatCheapBoundary2Min < 0) heatCheapBoundary2Min = 0;
  if (heatCheapBoundary2Min > 23 * 60 + 59) heatCheapBoundary2Min = 23 * 60 + 59;
  if (heatCheapBoundary1Min == heatCheapBoundary2Min) {
    heatCheapBoundary1Min = kDefaultHeatCheapBoundary1Min;
    heatCheapBoundary2Min = kDefaultHeatCheapBoundary2Min;
  }

  if (sunSchedMode > 3) sunSchedMode = 0;
  if (sunWinStartMin < 0) sunWinStartMin = 0;
  if (sunWinStartMin > 24 * 60) sunWinStartMin = 24 * 60;
  if (sunWinEndMin < 0) sunWinEndMin = 0;
  if (sunWinEndMin > 24 * 60) sunWinEndMin = 24 * 60;
  if (sunCheapNBlocks < 1) sunCheapNBlocks = 1;
  if (sunCheapNBlocks > kNpMaxPoints) sunCheapNBlocks = kNpMaxPoints;
  if (sunDaylightMarginMin < 0) sunDaylightMarginMin = 0;
  if (sunDaylightMarginMin > 180) sunDaylightMarginMin = 180;
  if (!isfinite(sunLat) || sunLat < -90.0f || sunLat > 90.0f) sunLat = kDefaultSunLat;
  if (!isfinite(sunLon) || sunLon < -180.0f || sunLon > 180.0f) sunLon = kDefaultSunLon;

  if (normalAlg > 1) normalAlg = 1;
  stepFactor = clampf(stepFactor, 0.01f, 2.00f);
  deadbandW  = clampf(deadbandW, 0.0f, 500.0f);
  maxStepPctPerUpdatePerUpdate = clampf(maxStepPctPerUpdatePerUpdate, 0.1f, 50.0f);

  multiControllerClamp();
  controllerTemperatureGateClampAll();
}

void loadConfig() {
  prefs.begin(PREF_NAMESPACE, true);

  wifiSsid  = prefs.getString("wifi_ssid", "");
  wifiPass  = prefs.getString("wifi_pass", "");
  deviceName = prefs.getString("device_name", "");
  deviceName.trim();

  meterType = prefs.getString("meter_type", "");
  meterIp   = prefs.getString("meter_ip", "");
  if (meterIp.length() == 0) meterIp = prefs.getString("p1_ip", "");
  if (meterType.length() == 0) meterType = "youless";

  uint8_t m = prefs.getUChar("ctrl_mode", (uint8_t)0);
  controlMode = (ControlMode)((m == 1) ? 1 : 0);

  maxBoilerPowerW       = prefs.getFloat("max_power",     kDefaultMaxBoilerPowerW);
  targetExportW         = prefs.getFloat("target_export", kDefaultTargetExportW);
  minStepPercent        = prefs.getFloat("min_step",      kDefaultMinStepPercent);
  minUpdateIntervalMs   = prefs.getULong("min_interval",  kDefaultMinIntervalMs);
  maxOutputPercentLimit = prefs.getFloat("max_out_pct",   kDefaultMaxOutputPercent);

  normalAlg   = prefs.getUChar("n_alg", kDefaultNormalAlg);
  stepFactor  = prefs.getFloat("n_sf",  kDefaultStepFactor);
  deadbandW   = prefs.getFloat("n_dbw", kDefaultDeadbandW);
  maxStepPctPerUpdatePerUpdate = prefs.getFloat("n_msp", kDefaultMaxStepPct);

  mqttEnabled   = prefs.getBool("mqtt_enabled", false);
  mqttHost      = prefs.getString("mqtt_host", "");
  mqttPort      = prefs.getUShort("mqtt_port", 1883);
  mqttUser      = prefs.getString("mqtt_user", "");
  mqttPassStr   = prefs.getString("mqtt_pass", "");
  mqttBaseTopic = prefs.getString("mqtt_topic", DEVICE_ID);

  elecPriceSource     = prefs.getUChar("elec_src", kDefaultElecPriceSource);
  elecFixedCtKwhAllIn = prefs.getFloat("elec_fix_ct", kDefaultElecFixedCtKwhAllIn);

  npEnabled = prefs.getBool("np_en", kDefaultNpEnabled);
  npArea    = prefs.getString("np_area", kDefaultNpArea);
  npResMin  = prefs.getInt("np_res", kDefaultNpResMin);
  npInclTaxEnabled = prefs.getBool("np_tax_en", kDefaultNpInclTaxEnabled);
  npEnergyTaxCt    = prefs.getFloat("np_tax_eb", kDefaultNpEnergyTaxCt);
  npInkoopCt       = prefs.getFloat("np_tax_in", kDefaultNpInkoopCt);
  npBtwPct         = prefs.getFloat("np_tax_btw", kDefaultNpBtwPct);

  gasTotalEnabled = prefs.getBool("gas_tot_en", kDefaultGasTotalEnabled);
  gasPriceSource  = (uint8_t)prefs.getUChar("gas_src", kDefaultGasPriceSource);
  gasFixedEurM3   = prefs.getFloat("gas_fix", kDefaultGasFixedEurM3);
  gasEnergyTaxEur = prefs.getFloat("gas_tax_eb", kDefaultGasEnergyTaxEur);
  gasInkoopEur    = prefs.getFloat("gas_tax_in", kDefaultGasInkoopEur);
  gasBtwPct       = prefs.getFloat("gas_tax_btw", kDefaultGasBtwPct);

  heatAutoEnabled           = prefs.getBool("heat_auto", kDefaultHeatAutoEnabled);
  heatExecElecWhenCheaper   = prefs.getBool("heat_exec_el", kDefaultHeatExecElecWhenCheaper);
  heatCheapBlocksEnabled    = prefs.getBool("heat_ch_en", kDefaultHeatCheapBlocksEnabled);
  heatCheapUseBoilerTarget = prefs.getBool("heat_ch_bt", kDefaultHeatCheapUseBoilerTarget);
  heatCheapNBlocks          = prefs.getInt ("heat_ch_n",  kDefaultHeatCheapNBlocks);
  heatCheapDayParts         = prefs.getUChar("heat_ch_dp", kDefaultHeatCheapDayParts);
  heatCheapBoundary1Min     = prefs.getInt ("heat_ch_t1", kDefaultHeatCheapBoundary1Min);
  heatCheapBoundary2Min     = prefs.getInt ("heat_ch_t2", kDefaultHeatCheapBoundary2Min);
  gasKwhPerM3               = prefs.getFloat("heat_kwhm3", kDefaultGasKwhPerM3);
  gasBoilerEff              = prefs.getFloat("heat_geff",  kDefaultGasBoilerEff);
  elecBoilerEff             = prefs.getFloat("heat_eeff",  kDefaultElecBoilerEff);
  heatElecMarginCt          = prefs.getFloat("heat_hyst",  kDefaultElecMarginCt);
  heatMinSwitchMin          = prefs.getInt  ("heat_minsw", kDefaultHeatMinSwitchMin);

  sunSchedMode         = prefs.getUChar("sun_s_mode", kDefaultSunSchedMode);
  sunWinStartMin       = prefs.getInt("sun_w_s", kDefaultSunWinStartMin);
  sunWinEndMin         = prefs.getInt("sun_w_e", kDefaultSunWinEndMin);
  sunCheapNBlocks      = prefs.getInt("sun_c_n", kDefaultSunCheapNBlocks);
  sunDaylightMarginMin = prefs.getInt("sun_marg", kDefaultSunDaylightMarginMin);
  sunLat               = prefs.getFloat("sun_lat", kDefaultSunLat);
  sunLon               = prefs.getFloat("sun_lon", kDefaultSunLon);

  dashShowSunWindowLabel = prefs.getBool("dash_sun_lbl", true);
  uiDarkMode = prefs.getBool("ui_dark", false);

  phaseGuardEnabled = prefs.getBool("pg_en", kDefaultPhaseGuardEnabled);
  phaseGuardPhase   = (uint8_t)prefs.getUChar("pg_ph", kDefaultPhaseGuardPhase);
  phaseGuardMaxA    = prefs.getFloat("pg_maxa", kDefaultPhaseGuardMaxA);
  phaseGuardMaxA_L1 = prefs.getFloat("pg_maxa_l1", phaseGuardMaxA);
  phaseGuardMaxA_L2 = prefs.getFloat("pg_maxa_l2", phaseGuardMaxA);
  phaseGuardMaxA_L3 = prefs.getFloat("pg_maxa_l3", phaseGuardMaxA);

  softStartEnabled     = prefs.getBool("ss_en", kDefaultSoftStartEnabled);
  softStartDurationSec = (uint16_t)prefs.getUInt("ss_dur", kDefaultSoftStartDurationSec);
  softStartStartPct    = prefs.getFloat("ss_start", kDefaultSoftStartStartPct);

  if (softStartDurationSec > 30) softStartDurationSec = 30;
  if (softStartStartPct < 0.0f) softStartStartPct = 0.0f;
  if (softStartStartPct > 30.0f) softStartStartPct = 30.0f;

  if (phaseGuardPhase < 1 || phaseGuardPhase > 3) phaseGuardPhase = kDefaultPhaseGuardPhase;
  if (!isfinite(phaseGuardMaxA_L1) || phaseGuardMaxA_L1 < 1.0f) phaseGuardMaxA_L1 = kDefaultPhaseGuardMaxA;
  if (!isfinite(phaseGuardMaxA_L2) || phaseGuardMaxA_L2 < 1.0f) phaseGuardMaxA_L2 = kDefaultPhaseGuardMaxA;
  if (!isfinite(phaseGuardMaxA_L3) || phaseGuardMaxA_L3 < 1.0f) phaseGuardMaxA_L3 = kDefaultPhaseGuardMaxA;
  refreshLegacyPhaseGuardMaxA();

  npBoostEnabled     = prefs.getBool("np_boost_en", kDefaultNpBoostEnabled);
  npBoostPriceSource = (uint8_t)prefs.getUChar("np_boost_src", kDefaultNpBoostPriceSource);
  npBoostThresholdCt = prefs.getFloat("np_boost_thr", kDefaultNpBoostThresholdCt);
  npBoostNBlocks     = prefs.getInt("np_boost_n", kDefaultNpBoostNBlocks);

  haDiscoveryEnabled = prefs.getBool("ha_disc", false);

  sysPublishEnabled = prefs.getBool("sys_pub", kDefaultSysPubEnabled);
  sysPublishIntervalSec = prefs.getInt("sys_int", kDefaultSysPubIntervalSec);
  statusPublishIntervalSec = prefs.getInt("st_int", kDefaultStatusPubIntervalSec);

  legScheduleEnabled    = prefs.getBool("leg_en", kDefaultLegEnabled);
  legDow                = prefs.getInt("leg_dow", kDefaultLegDow);
  legStartHour          = prefs.getInt("leg_hr",  kDefaultLegHour);
  legStartMinute        = prefs.getInt("leg_min", kDefaultLegMinute);
  legDurationMin        = prefs.getInt("leg_dur", kDefaultLegDurationMin);
  legOutputLimitPercent = prefs.getFloat("leg_pct", kDefaultLegOutputLimitPct);

  legUseTempSensor      = prefs.getBool("leg_ts", kDefaultLegUseTempSensor);
  legTempAddrStr        = prefs.getString("leg_taddr", "");
  legTargetTempC        = prefs.getFloat("leg_tc", kDefaultLegTargetTempC);
  legHoldMin            = prefs.getInt("leg_hold", kDefaultLegHoldMin);
  legRelayAutoEnabled   = prefs.getBool("leg_rel_auto", kDefaultLegRelayAuto);
  legRelayStartDelayMin = prefs.getInt("leg_rel_start", kDefaultLegRelayStartDelayMin);
  legRelayOffDelayMin   = prefs.getInt("leg_rel_delay", kDefaultLegRelayDelayMin);

  legLastRunDateInt     = prefs.getUInt("leg_lrdi", 0);
  legLastRunEpoch       = prefs.getUInt("leg_lrep", 0);
  legLastSuccessEpoch   = prefs.getUInt("leg_lsuc", 0);

  tempSensorMapJson    = prefs.getString("ts_map", "");
  tempControlAddrStr   = prefs.getString("ts_ctrl", "");
  tempPublishNamesToHA = prefs.getBool("ts_pub", true);

  fwAutoUpdateEnabled = prefs.getBool(PREF_FW_AUTO_KEY, false);

  relayLoadConfig(prefs);
  multiControllerLoadConfig(prefs);
  controllerTemperatureGateLoadConfig(prefs);

  prefs.end();
  clampAll();
}

void saveConfig() {
  prefs.begin(PREF_NAMESPACE, false);

  prefs.putString("wifi_ssid", wifiSsid);
  prefs.putString("wifi_pass", wifiPass);
  prefs.putString("device_name", deviceName);

  prefs.putString("meter_type", meterType);
  prefs.putString("meter_ip",   meterIp);
  prefs.putString("p1_ip",      meterIp);

  prefs.putUChar("ctrl_mode", (uint8_t)controlMode);

  prefs.putFloat("max_power",     maxBoilerPowerW);
  prefs.putFloat("target_export", targetExportW);
  prefs.putFloat("min_step",      minStepPercent);
  prefs.putULong("min_interval",  minUpdateIntervalMs);
  prefs.putFloat("max_out_pct",   maxOutputPercentLimit);

  prefs.putBool ("pg_en",      phaseGuardEnabled);
  prefs.putUChar("pg_ph",      phaseGuardPhase);
  prefs.putFloat("pg_maxa",    getConfiguredPhaseGuardMaxA(phaseGuardPhase));
  prefs.putFloat("pg_maxa_l1", phaseGuardMaxA_L1);
  prefs.putFloat("pg_maxa_l2", phaseGuardMaxA_L2);
  prefs.putFloat("pg_maxa_l3", phaseGuardMaxA_L3);

  prefs.putBool ("ss_en",    softStartEnabled);
  prefs.putUInt ("ss_dur",   (uint32_t)softStartDurationSec);
  prefs.putFloat("ss_start", softStartStartPct);

  prefs.putUChar("n_alg", normalAlg);
  prefs.putFloat("n_sf",  stepFactor);
  prefs.putFloat("n_dbw", deadbandW);
  prefs.putFloat("n_msp", maxStepPctPerUpdatePerUpdate);

  prefs.putBool("mqtt_enabled", mqttEnabled);
  prefs.putString("mqtt_host",  mqttHost);
  prefs.putUShort("mqtt_port",  mqttPort);
  prefs.putString("mqtt_user",  mqttUser);
  prefs.putString("mqtt_pass",  mqttPassStr);
  prefs.putString("mqtt_topic", mqttBaseTopic);

  prefs.putUChar("elec_src", elecPriceSource);
  prefs.putFloat("elec_fix_ct", elecFixedCtKwhAllIn);

  prefs.putBool("np_en",  npEnabled);
  prefs.putString("np_area", npArea);
  prefs.putInt("np_res",  npResMin);
  prefs.putBool ("np_tax_en",  npInclTaxEnabled);
  prefs.putFloat("np_tax_eb",  npEnergyTaxCt);
  prefs.putFloat("np_tax_in",  npInkoopCt);
  prefs.putFloat("np_tax_btw", npBtwPct);

  prefs.putBool ("gas_tot_en", gasTotalEnabled);
  prefs.putUChar("gas_src",   (uint8_t)gasPriceSource);
  prefs.putFloat("gas_fix",   gasFixedEurM3);
  prefs.putFloat("gas_tax_eb", gasEnergyTaxEur);
  prefs.putFloat("gas_tax_in", gasInkoopEur);
  prefs.putFloat("gas_tax_btw", gasBtwPct);

  prefs.putBool ("heat_auto",   heatAutoEnabled);
  prefs.putBool ("heat_exec_el", heatExecElecWhenCheaper);
  prefs.putBool ("heat_ch_en",   heatCheapBlocksEnabled);
  prefs.putBool ("heat_ch_bt",   heatCheapUseBoilerTarget);
  prefs.putInt  ("heat_ch_n",    heatCheapNBlocks);
  prefs.putUChar("heat_ch_dp",   heatCheapDayParts);
  prefs.putInt  ("heat_ch_t1",   heatCheapBoundary1Min);
  prefs.putInt  ("heat_ch_t2",   heatCheapBoundary2Min);
  prefs.putFloat("heat_kwhm3",  gasKwhPerM3);
  prefs.putFloat("heat_geff",   gasBoilerEff);
  prefs.putFloat("heat_eeff",   elecBoilerEff);
  prefs.putFloat("heat_hyst",   heatElecMarginCt);
  prefs.putInt  ("heat_minsw",  heatMinSwitchMin);

  prefs.putBool ("np_boost_en",  npBoostEnabled);
  prefs.putUChar("np_boost_src", (uint8_t)npBoostPriceSource);
  prefs.putFloat("np_boost_thr", npBoostThresholdCt);
  prefs.putInt  ("np_boost_n",   npBoostNBlocks);

  prefs.putUChar("sun_s_mode", (uint8_t)sunSchedMode);
  prefs.putInt  ("sun_w_s",    sunWinStartMin);
  prefs.putInt  ("sun_w_e",    sunWinEndMin);
  prefs.putInt  ("sun_c_n",    sunCheapNBlocks);
  prefs.putInt  ("sun_marg",   sunDaylightMarginMin);
  prefs.putFloat("sun_lat",    sunLat);
  prefs.putFloat("sun_lon",    sunLon);

  prefs.putBool ("dash_sun_lbl", dashShowSunWindowLabel);
  prefs.putBool("ui_dark", uiDarkMode);

  prefs.putBool("ha_disc", haDiscoveryEnabled);

  prefs.putBool("sys_pub", sysPublishEnabled);
  prefs.putInt("sys_int",  sysPublishIntervalSec);
  prefs.putInt("st_int",   statusPublishIntervalSec);

  prefs.putBool("leg_en",   legScheduleEnabled);
  prefs.putInt ("leg_dow",  legDow);
  prefs.putInt ("leg_hr",   legStartHour);
  prefs.putInt ("leg_min",  legStartMinute);
  prefs.putInt ("leg_dur",  legDurationMin);
  prefs.putFloat("leg_pct", legOutputLimitPercent);

  prefs.putBool("leg_ts",   legUseTempSensor);
  prefs.putString("leg_taddr", legTempAddrStr);
  prefs.putFloat("leg_tc",  legTargetTempC);
  prefs.putInt ("leg_hold", legHoldMin);
  prefs.putBool("leg_rel_auto", legRelayAutoEnabled);
  prefs.putInt ("leg_rel_start", legRelayStartDelayMin);
  prefs.putInt ("leg_rel_delay", legRelayOffDelayMin);

  prefs.putUInt("leg_lrdi", legLastRunDateInt);
  prefs.putUInt("leg_lrep", legLastRunEpoch);
  prefs.putUInt("leg_lsuc", legLastSuccessEpoch);

  prefs.putString("ts_map",  tempSensorMapJson);
  prefs.putString("ts_ctrl", tempControlAddrStr);
  prefs.putBool  ("ts_pub",  tempPublishNamesToHA);

  prefs.putBool(PREF_FW_AUTO_KEY, fwAutoUpdateEnabled);

  relaySaveConfig(prefs);
  multiControllerSaveConfig(prefs);
  controllerTemperatureGateSaveConfig(prefs);

  prefs.end();
}
