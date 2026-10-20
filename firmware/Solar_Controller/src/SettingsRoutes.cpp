#include "SettingsRoutes.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>
#include <esp_system.h>

#include "app_globals.h"
#include "ConfigStore.h"
#include "HomeAssistantDiscovery.h"
#include "MeterManager.h"
#include "MultiController.h"
#include "NetworkManager.h"
#include "PhaseGuard.h"
#include "RelayControl.h"
#include "ScenarioLogic.h"
#include "TimeSchedule.h"
#include "WebRoutes.h"
#include "WebUtils.h"
#include "mqtt.h"
#include "nordpool.h"
#include "sensors.h"

extern WebServer server;

extern String wifiSsid;
extern String wifiPass;
extern String deviceName;
extern bool uiDarkMode;
extern bool dashShowSunWindowLabel;

extern uint8_t elecPriceSource;
extern float elecFixedCtKwhAllIn;
extern uint8_t gasPriceSource;
extern float gasFixedEurM3;
extern float gasEnergyTaxEur;
extern float gasInkoopEur;
extern float gasBtwPct;
extern int gasDayCount;

extern unsigned long lastPollMs;
extern const int PWM_FREQ_HZ;
extern const int PWM_RES_BITS;

bool gasFetch15Days();
void mqttPublishCoreLiveStates(bool forceNow);

static void mqttPublishSettingsStateBundle(bool forceNow) {
  if (!mqttEnabled || !mqttClient.connected()) return;

  mqttPublishAvailability("online");
  mqttPublishForceState(forceNow);
  mqttPublishRelayState(forceNow);
  mqttPublishMultiControllerStates(forceNow);
  mqttPublishLegionellaConfig(forceNow);
  mqttPublishLegionellaPlanStates(forceNow);
  mqttPublishLegionellaState(forceNow);
  mqttPublishTuningStates(forceNow);
  mqttPublishLimitsStates(forceNow);
  mqttPublishNormalConfigStates(forceNow);
  mqttPublishSunStrategyStates(forceNow);
  mqttPublishElecPriceStates(forceNow);
  mqttPublishNordpoolStates(forceNow);
  mqttPublishHeatCheapConfigStates(forceNow);
  mqttPublishCoreLiveStates(forceNow);
  mqttPublishStatusJson(forceNow);
  mqttPublishSystemJson(forceNow);
}

static bool readBoolSec(const char* key, bool currentValue, const char* sectionName) {
  // Determine which section is being saved
  String sec = server.hasArg("_section") ? server.arg("_section") : "";
  sec.toLowerCase();

  String want = String(sectionName);
  want.toLowerCase();

  // If this save is not for the requested section, keep the current value
  if (sec != want) return currentValue;

  // If the explicit value is posted (preferred), parse it
  if (server.hasArg(key)) {
    String v = server.arg(key);
    v.trim(); v.toLowerCase();
    return (v == "1" || v == "true" || v == "on" || v == "yes");
  }

  // If only a checkbox presence is posted (legacy), treat presence as ON
  String chk = String(key) + "_chk";
  if (server.hasArg(chk.c_str())) return true;

  // Otherwise (same section + missing) means OFF
  return false;
}
// ----------------------------------------------------------------------------

void handleSettingsSaveReboot() {
  const bool wasMqttEnabled = mqttEnabled;
  const bool wasHaDiscoveryEnabled = haDiscoveryEnabled;

  if (server.hasArg("wifi_ssid")) {
    wifiSsid = server.arg("wifi_ssid"); wifiSsid.trim();
  }
  if (server.hasArg("wifi_pass")) {
    String p = server.arg("wifi_pass");
    if (p.length() > 0) wifiPass = p;
  }

  // Apparaatnaam (mDNS hostname)
  // - Leeg = automatische fallback-hostname; Homey discovery blijft actief
  // - Niet leeg = moet voldoen aan: a-z, 0-9 en '-' (max 20 tekens)
  if (server.hasArg("device_name")) {
    String raw = server.arg("device_name");
    raw.trim();

    if (raw.length() == 0) {
      deviceName = "";
    } else {
      String hn = sanitizeHostname(raw);
      if (!isValidHostname(hn)) {
        server.send(400, "text/plain",
          "Ongeldige Apparaatnaam. Gebruik alleen a-z, 0-9 en '-' (max 20 tekens).\n"
          "Voorbeeld: boiler, zolder, garage.\n"
          "Laat leeg voor een automatisch gegenereerde .local naam.");
        return;
      }
      deviceName = hn;
    }
  }

if (server.hasArg("meter_type")) {
    meterType = server.arg("meter_type"); meterType.trim();
  }
  if (server.hasArg("meter_ip")) {
    meterIp = server.arg("meter_ip"); meterIp.trim();
  }

  if (server.hasArg("target_export")) targetExportW = server.arg("target_export").toFloat();
  if (server.hasArg("max_boiler"))    maxBoilerPowerW = server.arg("max_boiler").toFloat();
  if (server.hasArg("min_step"))      minStepPercent = server.arg("min_step").toFloat();
  if (server.hasArg("min_interval")) {
    float sec = server.arg("min_interval").toFloat();
    if (sec < 0) sec = 0;
    minUpdateIntervalMs = (unsigned long)(sec * 1000.0f);
  }

  // NORMAL tuning
  if (server.hasArg("n_alg"))  normalAlg = (uint8_t)server.arg("n_alg").toInt();
  if (server.hasArg("n_sf"))   stepFactor = server.arg("n_sf").toFloat();
  if (server.hasArg("n_dbw"))  deadbandW  = server.arg("n_dbw").toFloat();
  if (server.hasArg("n_msp"))  maxStepPctPerUpdatePerUpdate = server.arg("n_msp").toFloat();

  // Elektriciteit prijsbron
  if (server.hasArg("elec_src"))    elecPriceSource     = (uint8_t)server.arg("elec_src").toInt();
  if (server.hasArg("elec_fix_ct")) {
    // NL/BE gebruikers typen vaak een komma als decimaalteken. Ondersteun beide.
    String v = server.arg("elec_fix_ct");
    v.replace(',', '.');
    elecFixedCtKwhAllIn = v.toFloat();
  }

  // Nordpool
  npEnabled = readBoolSec("np_en", npEnabled, "nordpool");
if (server.hasArg("np_area")) { npArea = server.arg("np_area"); npArea.trim(); }
  if (server.hasArg("np_res"))  npResMin = server.arg("np_res").toInt();

  // Nordpool belastingen/kosten (ct/kWh + BTW %)
  npInclTaxEnabled = readBoolSec("np_tax_en", npInclTaxEnabled, "nordpool");
  if (server.hasArg("np_tax_eb"))  { String s = server.arg("np_tax_eb"); s.replace(',', '.'); npEnergyTaxCt = s.toFloat(); }
  if (server.hasArg("np_tax_in"))  { String s = server.arg("np_tax_in"); s.replace(',', '.'); npInkoopCt    = s.toFloat(); }
  if (server.hasArg("np_tax_btw")) { String s = server.arg("np_tax_btw"); s.replace(',', '.'); npBtwPct      = s.toFloat(); }

  // Gas (EUR/m³)
  gasTotalEnabled = readBoolSec("gas_tot_en", gasTotalEnabled, "gas");
  if (server.hasArg("gas_src")) gasPriceSource = (uint8_t)server.arg("gas_src").toInt();
  if (server.hasArg("gas_fix"))  { String s = server.arg("gas_fix");  s.replace(',', '.'); gasFixedEurM3    = s.toFloat(); }
  if (server.hasArg("gas_tax_eb"))  { String s = server.arg("gas_tax_eb");  s.replace(',', '.'); gasEnergyTaxEur = s.toFloat(); }
  if (server.hasArg("gas_tax_in"))  { String s = server.arg("gas_tax_in");  s.replace(',', '.'); gasInkoopEur    = s.toFloat(); }
  if (server.hasArg("gas_tax_btw")) { String s = server.arg("gas_tax_btw"); s.replace(',', '.'); gasBtwPct       = s.toFloat(); }

// Verwarmkeuze (Gas vs Elektra)
heatAutoEnabled = readBoolSec("heat_auto", heatAutoEnabled, "gas");
heatExecElecWhenCheaper = readBoolSec("heat_exec_el", heatExecElecWhenCheaper, "gas");
heatCheapBlocksEnabled = readBoolSec("heat_ch_en", heatCheapBlocksEnabled, "gas");
heatCheapUseBoilerTarget = readBoolSec("heat_ch_bt", heatCheapUseBoilerTarget, "gas");
if (server.hasArg("heat_ch_n")) heatCheapNBlocks = server.arg("heat_ch_n").toInt();
if (server.hasArg("heat_kwhm3")) { String s = server.arg("heat_kwhm3"); s.replace(',', '.'); gasKwhPerM3 = s.toFloat(); }
if (server.hasArg("heat_geff"))  { String s = server.arg("heat_geff");  s.replace(',', '.'); gasBoilerEff = s.toFloat(); }
if (server.hasArg("heat_eeff"))  { String s = server.arg("heat_eeff");  s.replace(',', '.'); elecBoilerEff = s.toFloat(); elecBoilerEff = constrain(elecBoilerEff, 0.0f, 10.0f); }
if (server.hasArg("heat_hyst"))  { String s = server.arg("heat_hyst");  s.replace(',', '.'); heatElecMarginCt = constrain(s.toFloat(), 0.0f, 99.0f); }
if (server.hasArg("heat_minsw")) heatMinSwitchMin = server.arg("heat_minsw").toInt();

  // Nordpool prijs-boost (PWM MAX op goedkope/negatieve blokken vandaag)
  npBoostEnabled = readBoolSec("np_boost_en", npBoostEnabled, "nordpool");
  if (server.hasArg("np_boost_src")) npBoostPriceSource = (uint8_t)server.arg("np_boost_src").toInt();
  if (server.hasArg("np_boost_thr")) { String s = server.arg("np_boost_thr"); s.replace(',', '.'); npBoostThresholdCt = s.toFloat(); }
  if (server.hasArg("np_boost_n"))   npBoostNBlocks     = server.arg("np_boost_n").toInt();

// Energy Strategy (stand-alone) - zonregeling schema
if (server.hasArg("sun_s_mode")) sunSchedMode = (uint8_t)server.arg("sun_s_mode").toInt();

// Handmatig tijdvenster (HH:MM)
if (server.hasArg("sun_w_s")) {
  String t = server.arg("sun_w_s"); t.trim();
  int c = t.indexOf(':');
  if (c > 0) {
    int h = t.substring(0,c).toInt();
    int m = t.substring(c+1).toInt();
    sunWinStartMin = constrain(h,0,23)*60 + constrain(m,0,59);
  }
}
if (server.hasArg("sun_w_e")) {
  String t = server.arg("sun_w_e"); t.trim();
  int c = t.indexOf(':');
  if (c > 0) {
    int h = t.substring(0,c).toInt();
    int m = t.substring(c+1).toInt();
    sunWinEndMin = constrain(h,0,23)*60 + constrain(m,0,59);
  }
}

// Slim overdag (goedkoopste opeenvolgende blokken)
if (server.hasArg("sun_c_n"))  sunCheapNBlocks        = server.arg("sun_c_n").toInt();
if (server.hasArg("sun_marg")) sunDaylightMarginMin   = server.arg("sun_marg").toInt();

// Locatie (stand-alone sunrise/sunset)
if (server.hasArg("sun_lat"))  sunLat = server.arg("sun_lat").toFloat();
if (server.hasArg("sun_lon"))  sunLon = server.arg("sun_lon").toFloat();

// Dashboard label
dashShowSunWindowLabel = readBoolSec("dash_sun_lbl", dashShowSunWindowLabel, "energy");





  mqttEnabled = readBoolSec("mqtt_enabled", mqttEnabled, "mqtt");
  if (server.hasArg("mqtt_host")) { mqttHost = server.arg("mqtt_host"); mqttHost.trim(); }
  if (server.hasArg("mqtt_port")) {
    int p = server.arg("mqtt_port").toInt();
    if (p <= 0 || p > 65535) p = 1883;
    mqttPort = (uint16_t)p;
  }
  if (server.hasArg("mqtt_user")) { mqttUser = server.arg("mqtt_user"); mqttUser.trim(); }
  if (server.hasArg("mqtt_pass")) {
    String pw = server.arg("mqtt_pass");
    if (pw.length() > 0) mqttPassStr = pw;
  }
  if (server.hasArg("mqtt_topic")) { mqttBaseTopic = server.arg("mqtt_topic"); mqttBaseTopic.trim(); }

  haDiscoveryEnabled = readBoolSec("ha_disc", haDiscoveryEnabled, "mqtt");

  sysPublishEnabled = readBoolSec("sys_pub", sysPublishEnabled, "mqtt");
  if (server.hasArg("sys_int")) sysPublishIntervalSec = server.arg("sys_int").toInt();
  if (server.hasArg("st_int")) statusPublishIntervalSec = server.arg("st_int").toInt();

  const bool oldMcEnabled = mcEnabled;
  const uint8_t oldMcRole = mcRole;
  const uint8_t oldMcLastActiveRole = mcLastActiveRole;
  const uint8_t oldMcMode = mcMode;
  const uint8_t oldMcControllerCount = mcControllerCount;
  const uint8_t oldMcLocalControllerId = mcLocalControllerId;
  const String oldMcGroupName = mcGroupName;
  const bool oldMcSlaveAllowLocalLegionellaOverride = mcSlaveAllowLocalLegionellaOverride;
  MultiControllerSlotConfig oldMcSlots[MC_MAX_SLOTS];
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) oldMcSlots[i] = mcSlots[i];

  multiControllerApplySettings(server);

  String mcConfigReason;
  if (!multiControllerValidateCurrentConfig(mcConfigReason)) {
    mcEnabled = oldMcEnabled;
    mcRole = oldMcRole;
    mcLastActiveRole = oldMcLastActiveRole;
    mcMode = oldMcMode;
    mcControllerCount = oldMcControllerCount;
    mcLocalControllerId = oldMcLocalControllerId;
    mcGroupName = oldMcGroupName;
    mcSlaveAllowLocalLegionellaOverride = oldMcSlaveAllowLocalLegionellaOverride;
    for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) mcSlots[i] = oldMcSlots[i];
    server.send(400, "text/plain; charset=utf-8", mcConfigReason);
    return;
  }

  const bool shouldCleanupHaDiscovery =
      wasMqttEnabled &&
      mqttClient.connected() &&
      (!mqttEnabled || (wasHaDiscoveryEnabled && !haDiscoveryEnabled));
  if (shouldCleanupHaDiscovery) {
    mqttCleanupHADiscovery();
    delay(900);
  }

  clampAll();
  saveConfig();

  // v3.11.29 FIX: Na opslaan direct prijs-boost plan herberekenen en meteen evalueren
  // zodat PWM MAX binnen hetzelfde tijdblok al actief kan worden (geen wachten op volgend blok).
  if (npEnabled) {
    npRecomputeBoostPlanToday();
    npBoostActiveNow = npIsBoostActiveNow();
  }
  // Forceer een directe control-cycle bij terugkeer uit Settings
  lastPollMs = 0;

  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "text/html; charset=utf-8",
              htmlAutoReturnAfterReboot("Opgeslagen", "Reboot… (je gaat zo terug naar het dashboard)", "/"));

  delay(1200);
  ESP.restart();
}

void handleSettingsSave() {

// Robust boolean parsing:
// - checkbox sends value like "on"
// - some UI blocks use hidden 0/1 fields
// - if key not posted at all, keep current value (prevents accidental resets)
auto readBool = [&](const char* key, bool cur)->bool {
  if (server.hasArg(key)) {
    String v = server.arg(key);
    v.trim(); v.toLowerCase();
    if (v == "1" || v == "true" || v == "on" || v == "yes") return true;
    if (v == "0" || v == "false" || v == "off" || v == "no") return false;
    // empty/unknown -> treat as true when present (legacy checkbox)
    return true;
  }
  String k2 = String(key) + "_chk";
  if (server.hasArg(k2)) return true;
  return cur;
};



// Section-scoped OFF handling:
// Many pages use styled checkboxes (toggles). Unchecked checkboxes are not posted by the browser.
// We only want to interpret "missing" as OFF for the page that is actually being saved.
String _sec = server.hasArg("_section") ? server.arg("_section") : "";
_sec.trim(); _sec.toLowerCase();

auto readBoolSec = [&](const char* key, bool cur, const char* secName)->bool {
  if (_sec == String(secName)) {
    // For this section, missing key means OFF
    if (server.hasArg(key)) {
      String v = server.arg(key); v.trim(); v.toLowerCase();
      if (v == "1" || v == "true" || v == "on" || v == "yes") return true;
      if (v == "0" || v == "false" || v == "off" || v == "no") return false;
      return true; // present but unknown -> ON
    }
    return false;
  }
  // Other sections: keep current unless explicitly posted
  return readBool(key, cur);
};

// Snapshot Gas/Heat advice settings so we can apply changes immediately (no reboot needed)
const bool    oldGasTotalEnabled      = gasTotalEnabled;
const uint8_t oldGasPriceSource       = gasPriceSource;
const float   oldGasFixedEurM3        = gasFixedEurM3;
const float   oldGasEnergyTaxEur      = gasEnergyTaxEur;
const float   oldGasInkoopEur         = gasInkoopEur;
const float   oldGasBtwPct            = gasBtwPct;

const bool    oldHeatAutoEnabled      = heatAutoEnabled;
const bool    oldHeatExecElecWhenCheaper = heatExecElecWhenCheaper;
const bool    oldHeatCheapBlocksEnabled = heatCheapBlocksEnabled;
const bool    oldHeatCheapUseBoilerTarget = heatCheapUseBoilerTarget;
const int     oldHeatCheapNBlocks = heatCheapNBlocks;
const float   oldGasKwhPerM3          = gasKwhPerM3;
const float   oldGasBoilerEff         = gasBoilerEff;
const float   oldElecBoilerEff        = elecBoilerEff;
const float   oldHeatElecMarginCt     = heatElecMarginCt;
const int     oldHeatMinSwitchMin     = heatMinSwitchMin;
// Snapshot Energy strategy settings so we can apply schedule changes immediately (no reboot needed)
const uint8_t oldSunSchedMode        = sunSchedMode;
const int     oldSunWinStartMin      = sunWinStartMin;
const int     oldSunWinEndMin        = sunWinEndMin;
const int     oldSunCheapNBlocks     = sunCheapNBlocks;
const int     oldSunDaylightMarginMin= sunDaylightMarginMin;
const float   oldSunLat              = sunLat;
const float   oldSunLon              = sunLon;
const bool    oldDashShowSunWindowLabel = dashShowSunWindowLabel;

// Snapshot Nordpool-related settings so we can apply changes immediately (no reboot needed)
const int    oldNpResMin          = npResMin;
const bool   oldNpEnabled         = npEnabled;
const String oldNpArea            = npArea;
const bool   oldNpInclTaxEnabled  = npInclTaxEnabled;
const float  oldNpEnergyTaxCt     = npEnergyTaxCt;
const float  oldNpInkoopCt        = npInkoopCt;
const float  oldNpBtwPct          = npBtwPct;

const bool   oldNpBoostEnabled    = npBoostEnabled;
const uint8_t oldNpBoostPriceSource = npBoostPriceSource;
const float  oldNpBoostThresholdCt  = npBoostThresholdCt;
const int    oldNpBoostNBlocks      = npBoostNBlocks;

const bool   oldRelayEnabled        = relayIsEnabled();
const uint8_t oldRelayType          = relayGetType();
const bool   oldRelayBootOn         = relayGetBootOn();
  // Save settings that can be applied immediately (no reboot).
  // IMPORTANT: This endpoint intentionally ignores WiFi/Meter/MQTT fields.
  // Those must be saved via /settings/save_reboot to avoid accidental disconnect.


// UI / Theme (dark mode)
// IMPORTANT: most settings forms do NOT post ui_dark. Only update when the field is present.
// Otherwise an "Opslaan" elsewhere would unintentionally reset the theme.
if (server.hasArg("ui_dark")) {
    uiDarkMode = (server.arg("ui_dark").toInt() == 1);
  } else if (server.hasArg("ui_theme")) {
  // Optional explicit theme value
  String t = server.arg("ui_theme");
  t.toLowerCase();
  uiDarkMode = (t == "dark");
}
// If neither is present: keep current uiDarkMode as-is.

// Relais (vaste GPIO18)
relaySetEnabled(readBoolSec("relay_en", relayIsEnabled(), "relay"));
if (server.hasArg("relay_type")) {
  relaySetType((uint8_t)server.arg("relay_type").toInt());
}
if (server.hasArg("relay_boot_on")) {
  String rv = server.arg("relay_boot_on");
  rv.trim(); rv.toLowerCase();
  relaySetBootOn(rv == "1" || rv == "true" || rv == "on" || rv == "yes");
}
if (!relayIsEnabled()) {
  relaySetManualOn(false);
}

// Core control parameters
  if (server.hasArg("target_export")) targetExportW = server.arg("target_export").toFloat();
  if (server.hasArg("max_boiler"))    maxBoilerPowerW = server.arg("max_boiler").toFloat();
  if (server.hasArg("min_step"))      minStepPercent = server.arg("min_step").toFloat();
  if (server.hasArg("min_interval")) {
    float sec = server.arg("min_interval").toFloat();
    if (sec < 0) sec = 0;
    minUpdateIntervalMs = (unsigned long)(sec * 1000.0f);
  }

  // NORMAL tuning (zelfde velden als /settings/save_reboot)
  if (server.hasArg("n_alg"))  normalAlg = (uint8_t)server.arg("n_alg").toInt();
  if (server.hasArg("n_sf"))   stepFactor = server.arg("n_sf").toFloat();
  if (server.hasArg("n_dbw"))  deadbandW  = server.arg("n_dbw").toFloat();
  if (server.hasArg("n_msp"))  maxStepPctPerUpdatePerUpdate = server.arg("n_msp").toFloat();

  // Elektriciteit prijsbron
  if (server.hasArg("elec_src"))    elecPriceSource = (uint8_t)server.arg("elec_src").toInt();
  if (server.hasArg("elec_fix_ct")) {
    String v = server.arg("elec_fix_ct");
    v.replace(',', '.');
    elecFixedCtKwhAllIn = v.toFloat();
  }

  // Nordpool
  npEnabled = readBoolSec("np_en", npEnabled, "nordpool");
  if (server.hasArg("np_area")) { npArea = server.arg("np_area"); npArea.trim(); }
  if (server.hasArg("np_res")) npResMin = server.arg("np_res").toInt();

  // Nordpool price-boost (PWM MAX)
  // Read explicit 0/1 value (UI uses hidden field) so toggle is always deterministic.
  npBoostEnabled = readBoolSec("np_boost_en", npBoostEnabled, "nordpool");
  if (server.hasArg("np_boost_src")) npBoostPriceSource = (uint8_t)server.arg("np_boost_src").toInt();
  if (server.hasArg("np_boost_thr")) { String s = server.arg("np_boost_thr"); s.replace(',', '.'); npBoostThresholdCt = s.toFloat(); }
  if (server.hasArg("np_boost_n"))   npBoostNBlocks     = server.arg("np_boost_n").toInt();

  // Nordpool belastingen/kosten
  npInclTaxEnabled = readBoolSec("np_tax_en", npInclTaxEnabled, "nordpool");
  if (server.hasArg("np_tax_eb"))  { String s = server.arg("np_tax_eb"); s.replace(',', '.'); npEnergyTaxCt = s.toFloat(); }
  if (server.hasArg("np_tax_in"))  { String s = server.arg("np_tax_in"); s.replace(',', '.'); npInkoopCt    = s.toFloat(); }
  if (server.hasArg("np_tax_btw")) { String s = server.arg("np_tax_btw"); s.replace(',', '.'); npBtwPct      = s.toFloat(); }

  // Gas (EUR/m³)
  gasTotalEnabled = readBoolSec("gas_tot_en", gasTotalEnabled, "gas");
  if (server.hasArg("gas_src")) gasPriceSource = (uint8_t)server.arg("gas_src").toInt();
  if (server.hasArg("gas_fix"))  { String s = server.arg("gas_fix");  s.replace(',', '.'); gasFixedEurM3    = s.toFloat(); }
  if (server.hasArg("gas_tax_eb"))  { String s = server.arg("gas_tax_eb");  s.replace(',', '.'); gasEnergyTaxEur = s.toFloat(); }
  if (server.hasArg("gas_tax_in"))  { String s = server.arg("gas_tax_in");  s.replace(',', '.'); gasInkoopEur    = s.toFloat(); }
  if (server.hasArg("gas_tax_btw")) { String s = server.arg("gas_tax_btw"); s.replace(',', '.'); gasBtwPct       = s.toFloat(); }

// Gas vs Elektra advies (instellingen)
heatAutoEnabled = readBoolSec("heat_auto", heatAutoEnabled, "gas");
heatExecElecWhenCheaper = readBoolSec("heat_exec_el", heatExecElecWhenCheaper, "gas");
heatCheapBlocksEnabled = readBoolSec("heat_ch_en", heatCheapBlocksEnabled, "gas");
heatCheapUseBoilerTarget = readBoolSec("heat_ch_bt", heatCheapUseBoilerTarget, "gas");
if (server.hasArg("heat_ch_n")) heatCheapNBlocks = server.arg("heat_ch_n").toInt();

if (server.hasArg("heat_kwhm3")) { String s = server.arg("heat_kwhm3"); s.replace(',', '.'); gasKwhPerM3 = s.toFloat(); }
if (server.hasArg("heat_geff"))  { String s = server.arg("heat_geff");  s.replace(',', '.'); gasBoilerEff = s.toFloat(); }
if (server.hasArg("heat_eeff"))  { String s = server.arg("heat_eeff");  s.replace(',', '.'); elecBoilerEff = s.toFloat(); }
if (server.hasArg("heat_hyst"))  { String s = server.arg("heat_hyst");  s.replace(',', '.'); heatElecMarginCt = s.toFloat(); }
if (server.hasArg("heat_minsw")) { heatMinSwitchMin = server.arg("heat_minsw").toInt(); }

  // Fase-beveiliging (stap 1/3) — geen reboot nodig
  // Let op: dit is alleen de configuratie; de PWM-ingreep komt in stap 4.
  phaseGuardEnabled = readBoolSec("pg_en", phaseGuardEnabled, "fase");
  if (server.hasArg("pg_ph"))   phaseGuardPhase = (uint8_t)server.arg("pg_ph").toInt();
  if (server.hasArg("pg_maxa")) {
    String s = server.arg("pg_maxa");
    s.replace(',', '.');
    float v = s.toFloat();
    if (phaseGuardPhase == 1) phaseGuardMaxA_L1 = v;
    else if (phaseGuardPhase == 2) phaseGuardMaxA_L2 = v;
    else if (phaseGuardPhase == 3) phaseGuardMaxA_L3 = v;
  }
  if (server.hasArg("pg_maxa_l1")) { String s = server.arg("pg_maxa_l1"); s.replace(',', '.'); phaseGuardMaxA_L1 = s.toFloat(); }
  if (server.hasArg("pg_maxa_l2")) { String s = server.arg("pg_maxa_l2"); s.replace(',', '.'); phaseGuardMaxA_L2 = s.toFloat(); }
  if (server.hasArg("pg_maxa_l3")) { String s = server.arg("pg_maxa_l3"); s.replace(',', '.'); phaseGuardMaxA_L3 = s.toFloat(); }
  refreshLegacyPhaseGuardMaxA();

  if (_sec == "fase") {
    for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
      String key = String("pg_mc_s") + String(i + 1) + String("_ph");
      if (server.hasArg(key)) {
        multiControllerSetSlotAssignedPhase(i, (uint8_t)server.arg(key).toInt());
      }
    }
    if (mcEnabled) {
      if ((mcControllerCount > 0) && (mcSlots[0].slotType == MC_SLOT_LOCAL_PWM)) {
        phaseGuardPhase = multiControllerGetSlotAssignedPhase(0);
      }
      refreshLegacyPhaseGuardMaxA();
    }
  }

  // Soft-start prioriteitsmodi (step91)
  softStartEnabled = readBoolSec("ss_en", softStartEnabled, "fase");
  if (server.hasArg("ss_dur"))   softStartDurationSec = (uint16_t)server.arg("ss_dur").toInt();
  if (server.hasArg("ss_start")) {
    String s2 = server.arg("ss_start");
    s2.replace(',', '.');
    softStartStartPct = s2.toFloat();
  }

  
// Energy Strategy (stand-alone) - zonregeling schema
// NOTE: Deze velden zitten in de UI onder "Energy strategy" en moeten óók via /settings/save
//       (zonder reboot) opgeslagen worden. In step92 stonden ze alleen in /settings/save_reboot.
if (server.hasArg("sun_s_mode")) sunSchedMode = (uint8_t)server.arg("sun_s_mode").toInt();

// Handmatig tijdvenster (HH:MM)
if (server.hasArg("sun_w_s")) {
  String t = server.arg("sun_w_s"); t.trim();
  int c = t.indexOf(':');
  if (c > 0) {
    int h = t.substring(0, c).toInt();
    int m2 = t.substring(c + 1).toInt();
    sunWinStartMin = constrain(h, 0, 23) * 60 + constrain(m2, 0, 59);
  }
}
if (server.hasArg("sun_w_e")) {
  String t = server.arg("sun_w_e"); t.trim();
  int c = t.indexOf(':');
  if (c > 0) {
    int h = t.substring(0, c).toInt();
    int m2 = t.substring(c + 1).toInt();
    sunWinEndMin = constrain(h, 0, 23) * 60 + constrain(m2, 0, 59);
  }
}

// Slim: goedkoopste opeenvolgende blokken overdag
if (server.hasArg("sun_c_n"))  sunCheapNBlocks      = server.arg("sun_c_n").toInt();
if (server.hasArg("sun_marg")) sunDaylightMarginMin = server.arg("sun_marg").toInt();

// Locatie (voor stand-alone sunrise/sunset berekening)
if (server.hasArg("sun_lat"))  sunLat = server.arg("sun_lat").toFloat();
if (server.hasArg("sun_lon"))  sunLon = server.arg("sun_lon").toFloat();

// Dashboard label toggle
dashShowSunWindowLabel = readBoolSec("dash_sun_lbl", dashShowSunWindowLabel, "energy");

  const bool oldMcEnabled = mcEnabled;
  const uint8_t oldMcRole = mcRole;
  const uint8_t oldMcLastActiveRole = mcLastActiveRole;
  const uint8_t oldMcMode = mcMode;
  const uint8_t oldMcControllerCount = mcControllerCount;
  const uint8_t oldMcLocalControllerId = mcLocalControllerId;
  const String oldMcGroupName = mcGroupName;
  const bool oldMcSlaveAllowLocalLegionellaOverride = mcSlaveAllowLocalLegionellaOverride;
  MultiControllerSlotConfig oldMcSlots[MC_MAX_SLOTS];
  for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) oldMcSlots[i] = mcSlots[i];

  multiControllerApplySettings(server);

  String mcConfigReason;
  if (!multiControllerValidateCurrentConfig(mcConfigReason)) {
    mcEnabled = oldMcEnabled;
    mcRole = oldMcRole;
    mcLastActiveRole = oldMcLastActiveRole;
    mcMode = oldMcMode;
    mcControllerCount = oldMcControllerCount;
    mcLocalControllerId = oldMcLocalControllerId;
    mcGroupName = oldMcGroupName;
    mcSlaveAllowLocalLegionellaOverride = oldMcSlaveAllowLocalLegionellaOverride;
    for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) mcSlots[i] = oldMcSlots[i];
    server.send(400, "text/plain; charset=utf-8", mcConfigReason);
    return;
  }

  clampAll();
  multiControllerReconfigureLocalPwmRuntime(PWM_FREQ_HZ, PWM_RES_BITS, (uint32_t)lround((lastDutyPercent / 100.0f) * 255.0f));

  const bool relayConfigChanged =
      (relayIsEnabled() != oldRelayEnabled) ||
      (relayGetType() != oldRelayType) ||
      (relayGetBootOn() != oldRelayBootOn);
  if (relayConfigChanged || !relayIsEnabled()) {
    relayApplyRuntimeState();
  }

// Apply Nordpool/price changes immediately:
// - resolution/area/taxes changes require a refetch (otherwise the graph & calculations keep old cached data)
// - boost settings changes require plan recompute (done by invalidate+refetch or direct recompute)
const bool npCoreChanged =
    (npResMin != oldNpResMin) ||
    (npEnabled != oldNpEnabled) ||
    (npArea != oldNpArea) ||
    (npInclTaxEnabled != oldNpInclTaxEnabled) ||
    (npEnergyTaxCt != oldNpEnergyTaxCt) ||
    (npInkoopCt != oldNpInkoopCt) ||
    (npBtwPct != oldNpBtwPct);

const bool npBoostChanged =
    (npBoostEnabled != oldNpBoostEnabled) ||
    (npBoostPriceSource != oldNpBoostPriceSource) ||
    (npBoostThresholdCt != oldNpBoostThresholdCt) ||
    (npBoostNBlocks != oldNpBoostNBlocks);

if (npCoreChanged) {
  // force refetch with the new settings
  npInvalidateCache();
} else if (npBoostChanged) {
  // no refetch needed, but recompute today's boost plan using the already cached prices
  npRecomputeBoostPlanToday();
}


// Apply Energy strategy schedule changes immediately:
// If any input changes, invalidate cached cheapest/daylight window so the UI + gating updates right away.
const bool sunStrategyChanged =
    (sunSchedMode != oldSunSchedMode) ||
    (sunWinStartMin != oldSunWinStartMin) ||
    (sunWinEndMin != oldSunWinEndMin) ||
    (sunCheapNBlocks != oldSunCheapNBlocks) ||
    (sunDaylightMarginMin != oldSunDaylightMarginMin) ||
    (sunLat != oldSunLat) ||
    (sunLon != oldSunLon) ||
    (dashShowSunWindowLabel != oldDashShowSunWindowLabel);

if (sunStrategyChanged) {
  // Force recompute for today (no reboot required)
  sunWinDate = "";
  sunCheapestStartMin = -1;
  sunCheapestEndMin   = -1;
  recomputeCheapestDaylightWindowIfNeeded();
}


// Apply Gas/Heat advice changes immediately (dashboard + logic).
// Without this, values are saved but the runtime advice can lag until a reboot or next periodic tick.
const bool gasChanged =
    (gasTotalEnabled != oldGasTotalEnabled) ||
    (gasPriceSource  != oldGasPriceSource)  ||
    (gasFixedEurM3   != oldGasFixedEurM3)   ||
    (gasEnergyTaxEur != oldGasEnergyTaxEur) ||
    (gasInkoopEur    != oldGasInkoopEur)    ||
    (gasBtwPct       != oldGasBtwPct);

const bool heatChanged =
    (heatAutoEnabled != oldHeatAutoEnabled) ||
    (heatExecElecWhenCheaper != oldHeatExecElecWhenCheaper) ||
    (heatCheapBlocksEnabled != oldHeatCheapBlocksEnabled) ||
    (heatCheapUseBoilerTarget != oldHeatCheapUseBoilerTarget) ||
    (heatCheapNBlocks != oldHeatCheapNBlocks) ||
    (gasKwhPerM3      != oldGasKwhPerM3)    ||
    (gasBoilerEff     != oldGasBoilerEff)   ||
    (elecBoilerEff      != oldElecBoilerEff)    ||
    (heatElecMarginCt != oldHeatElecMarginCt) ||
    (heatMinSwitchMin != oldHeatMinSwitchMin) ;

if (gasChanged || heatChanged) {
  // Nieuwe prijzen/instellingen betekenen een nieuw slim ELEC-plan.
  heatCheapPlanInvalidate();

  // Recompute costs/recommendation right away.
  heatCompareUpdateNow();

  // If the user changed the min switch time or wants an immediate visible flip, allow switching again.
  // (This keeps the configured min switch time, but doesn't force the old delay after a settings change.)
  heatLastSwitchMs = 0;

  // If gas spot data is not loaded yet and the source is spot, fetch once so advice has real data.
  if (gasPriceSource != 0 /*spot*/ || gasDayCount > 0) {
    // ok
  } else {
    gasFetch15Days();
  }
}

  saveConfig();

  // Publish all retained state topics touched by Settings so Home Assistant mirrors the UI immediately.
  if (mqttEnabled && mqttClient.connected()) {
    mqttPublishRelayState(true);
    mqttPublishMultiControllerStates(true);
    mqttPublishLegionellaPlanStates(true);
    mqttPublishLegionellaState(true);
    mqttPublishTuningStates(true);
    mqttPublishLimitsStates(true);
    mqttPublishNormalConfigStates(true);
    mqttPublishSunStrategyStates(true);
    mqttPublishElecPriceStates(true);
    mqttPublishNordpoolStates(true);
    mqttPublishHeatCheapConfigStates(true);
    mqttPublishCoreLiveStates(true);
    mqttPublishStatusJson(true);
  }

  redirectTo("/settings");
}

void handleSettingsHaMqttCleanup() {
  if (!mqttClient.connected()) {
    redirectTo("/settings");
    return;
  }

  const uint16_t cleared = mqttRebuildHADiscovery();
  mqttPublishSettingsStateBundle(true);
  (void)cleared;

  redirectTo("/settings");
}


// ==================================================
// GitHub OTA (pull) update helpers
// ==================================================

// moved to src/FirmwareUpdateManager.cpp: GitHub OTA helpers / startFirmwareUpdateTaskNow



void handleSettingsSaveSensors() {
  // Toggle: publish names to Home Assistant (Discovery)
  tempPublishNamesToHA = server.hasArg("ts_pub") ? (server.arg("ts_pub").toInt() == 1) : false;
// Alleen verwerken als we sensoren hebben (anders alleen toggle opslaan)
  if (ds18Available && tempSensorCount > 0) {
    DynamicJsonDocument doc(4096);
    JsonObject root = doc.to<JsonObject>();

    for (int i = 0; i < tempSensorCount; i++) {
      TempSensorEntry& e = tempSensors[i];

      // Enabled (toggles are styled checkboxes; unchecked is not posted, so prefer explicit 0/1)
      String enKey = String("ts_en_") + String(i);
      bool en = false;
      if (server.hasArg(enKey)) {
        en = (server.arg(enKey).toInt() == 1);
      } else if (server.hasArg(enKey + "_chk")) {
        en = true; // legacy presence-only checkbox
      }
      // Name
      String nm = server.arg(String("ts_name_") + String(i));
      nm.trim();
      if (nm.length() == 0) nm = String("Temp ") + String(i + 1);
      if (nm.length() > 32) nm = nm.substring(0, 32); // mobiel + HA netjes kort

      e.enabled = en;
      e.name    = nm;

      JsonObject o = root.createNestedObject(e.addrStr);
      o["name"] = nm;
      o["en"]   = en ? 1 : 0;
    }

    // CONTROL sensor address
    if (server.hasArg("ts_ctrl")) {
      String c = server.arg("ts_ctrl");
      c.trim();
      if (c.length() == 16) tempControlAddrStr = c;
    }

    tempSensorMapJson = "";
    serializeJson(root, tempSensorMapJson);
  }

  // Opslaan in Preferences (via bestaande saveConfig zodat alles consistent blijft)
  saveConfig();

  // Na opslaan: opnieuw scannen en mapping toepassen (voor directe UI/legionella)
  scanTempSensors();

  // HA Discovery opnieuw publiceren zodat (nieuwe) namen direct overkomen
  if (mqttEnabled && mqttClient.connected() && haDiscoveryEnabled) {
    mqttPublishHADiscovery(true);
  }

  redirectTo("/settings");
}

// ================= UI / Theme (Optie A) =================
// Slaat de keuze Light/Dark op in Preferences.
// Let op: dit is een aparte form in Settings (onder Temperatuursensoren) om nested forms te vermijden.
void handleSettingsSaveUi() {
  // Verwacht bij voorkeur: checkbox ui_dark (aan/uit)
  // (Fallback: ui_theme = "light" of "dark")
  if (server.hasArg("ui_dark")) {
    uiDarkMode = true;
  } else if (server.hasArg("ui_theme")) {
    String t = server.arg("ui_theme");
    t.trim();
    uiDarkMode = (t == "dark");
  } else {
    // Als niets meegestuurd is: behandel als uit
    uiDarkMode = false;
  }
  saveConfig();
  redirectTo("/settings");
}




void handleSettingsPage() {
  String page = htmlHeader("Settings");

  // (UI) Top tabs verwijderd – bottom nav is genoeg


  // page += topTabs("set");
page += "<details class='acc'><summary><div class='sum'><b>Netwerk & Meter</b><span class='tag'>reboot</span></div></summary><div class='accbd'>";
  page += "<form id='mainSettingsForm' method='POST' action='/settings/save' novalidate>";
  page += "<input type='hidden' name='_section' value='' data-role='section'>";

  page += "<label>Apparaatnaam (mDNS)</label>";
page += "<input name='device_name' value='" + deviceName + "' placeholder='Bijv. boiler, zolder, garage'>";
page += "<div class='help'>Optioneel. Als ingevuld is dit apparaat bereikbaar via: <b>http://&lt;apparaatnaam&gt;.local</b>. Laat leeg voor een automatisch gegenereerde .local naam. Homey discovery blijft in beide gevallen actief.</div>";

page += "<label>WiFi SSID</label>";
  page += "<div style='display:flex; gap:10px; flex-wrap:wrap; align-items:center;'>";
  // Mobiel-proof: echte dropdown + handmatig veld. Dropdown vult na scan; selectie kopieert naar handmatig veld.
  page += "<select id='ssid_select' style='flex:1; min-width:160px;' onchange='onSsidPick()'>";
  page += "<option value=''>— Kies uit scanlijst —</option>";
  page += "</select>";
  page += "<input id='ssid_manual' style='flex:1; min-width:160px;' name='wifi_ssid' value='" + wifiSsid + "' placeholder='Of typ handmatig (ook voor verborgen SSID)'>";
  page += "<button id='btn_wifi_scan' type='button' class='btn ghost' style='width:auto;'>Scan WiFi</button>";
  page += "</div>";
  page += "<div id='ssid_status' class='help'>Huidig: <b>" + wifiSsid + "</b> • Tip: klik op <b>Scan WiFi</b> om netwerken te tonen (verborgen SSID&apos;s moet je typen).</div>";

  page += "<label>WiFi wachtwoord (leeg = ongewijzigd)</label><input type='password' name='wifi_pass' value='' placeholder='••••••••'>";

  page += "<label>Meetbron</label><select name='meter_type'>";
  page += String("<option value='youless'") + ((meterType=="youless") ? " selected" : "") + ">Youless LS120</option>";
  page += String("<option value='homewizard'") + ((meterType=="homewizard") ? " selected" : "") + ">HomeWizard P1</option>";
  page += String("<option value='smartgateways'") + ((meterType=="smartgateways") ? " selected" : "") + ">Smart Gateways P1</option>";
  page += "</select>";

  page += "<label>Meter IP-adres</label><input name='meter_ip' value='" + meterIp + "'>";

  page += "<div class=\'row\' style=\'margin-top:14px\'><button class=\'btn\' type=\'submit\' name=\'_section\' value=\'mqtt\' data-section=\'mqtt\' formaction=\'/settings/save_reboot\'>Opslaan & reboot</button></div>";
page += "</div></details>";

  page += multiControllerBuildSettingsSection();

  page += "<details class='acc'><summary><div class='sum'><b>Relais</b><span class='tag'>GPIO18</span></div></summary><div class='accbd'>";
  page += "<div class='row'><div class='k'>Relais gebruiken</div><div class='v'>";
  page += String("<span class='switch ") + (relayIsEnabled() ? "on" : "") + "' onclick='sw(this,false)'>";
  page += String("<input type='checkbox' name='relay_en' value='1' ") + (relayIsEnabled() ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";
  page += "<label>GPIO-pin</label>";
  page += "<input type='number' value='" + String(relayGetPin()) + "' readonly disabled>";
  page += "<label>Relais type</label>";
  page += "<select name='relay_type'>";
  page += String("<option value='0'") + (relayIsActiveLow() ? " selected" : "") + ">active LOW</option>";
  page += String("<option value='1'") + (!relayIsActiveLow() ? " selected" : "") + ">active HIGH</option>";
  page += "</select>";
  page += "<label>Opstartstand</label>";
  page += "<select name='relay_boot_on'>";
  page += String("<option value='0'") + (!relayGetBootOn() ? " selected" : "") + ">uit</option>";
  page += String("<option value='1'") + (relayGetBootOn() ? " selected" : "") + ">aan</option>";
  page += "</select>";
  page += "<div class='help'>Dashboard-toggle wordt alleen getoond als <b>Relais gebruiken</b> aan staat. GPIO18 is vast voor deze functie.</div>";
  page += "<div class='row' style='margin-top:14px'><button class='btn' type='submit' name='_section' value='relay' data-section='relay' formaction='/settings/save'>Opslaan</button></div>";
  page += "</div></details>";

  page += "<details class='acc'><summary><div class='sum'><b>Regeling (NORMAL)</b><span class='tag'>tuning</span></div></summary><div class='accbd'>";

  page += "<label>Doel netvermogen (targetExportW) [W]</label><div class='inrow'><button type='button' class='pm' onclick=\"pm('target_export')\">±</button><input id='target_export' type='number' step='1' name='target_export' value='" + String(targetExportW,1) + "'></div>";
  page += "<label>Max boilervemogen (maxBoilerPowerW) [W]</label><input type='number' step='10' name='max_boiler' value='" + String(maxBoilerPowerW,1) + "'>";

  page += "<label>Min stap (minStepPercent) [%]</label><input type='number' step='0.1' name='min_step' value='" + String(minStepPercent,1) + "'>";
  page += "<label>Min interval [s]</label><input type='number' step='1' name='min_interval' value='" + String(minUpdateIntervalMs/1000.0f,1) + "'>";

  page += "<div class='sep'></div>";
  page += "<b>NORMAL algoritme</b>";

  page += "<label>Regel-algoritme</label><select name='n_alg'>";
  page += String("<option value='1'") + ((normalAlg==1) ? " selected" : "") + ">STEP (closed-loop)</option>";
  page += String("<option value='0'") + ((normalAlg==0) ? " selected" : "") + ">DIRECT (lineair)</option>";
  page += "</select>";

  page += "<label>Stapfactor (agressiviteit)</label>";
  page += "<input type='number' step='0.01' min='0.01' max='2.00' name='n_sf' value='" + String(stepFactor,2) + "'>";
  page += "<div class='help'>Hoger = sneller/grotere stappen. Lager = rustiger.</div>";

  page += "<label>Deadband [W]</label>";
  page += "<input type='number' step='1' min='0' max='500' name='n_dbw' value='" + String(deadbandW,1) + "'>";
  page += "<div class='help'>Binnen ±deadband corrigeert hij niet (rustzone).</div>";

  page += "<label>Max stap per update [%/s]</label>";
  page += "<input type='number' step='0.5' min='0.1' max='50' name='n_msp' value='" + String(maxStepPctPerUpdatePerUpdate,1) + "'>";
  page += "<div class='help'>Begrenst “hoe hard” hij per seconde mag ingrijpen.</div>";

  page += "<div class=\'row\' style=\'margin-top:14px\'><button class=\'btn\' type=\'submit\' formaction=\'/settings/save\'>Opslaan</button></div>";
page += "</div></details>";

  // ---- Nordpool (publieke spotprijzen, geen keys) ----
  // ================= Energy Strategy =================
  auto fmtHm = [](int minOfDay)->String{
    int h = (minOfDay/60)%24;
    int m = minOfDay%60;
    char b[6];
    snprintf(b, sizeof(b), "%02d:%02d", h, m);
    return String(b);
  };

  page += "<details class='acc'><summary><div class='sum'><b>Energy strategy</b><span class='tag'>schema</span></div></summary><div class='accbd'>";
  page += "<label>Zonregeling schema (NORMAL)</label>";
  page += "<select name='sun_s_mode'>";
  page += String("<option value='0'") + ((sunSchedMode==0) ? " selected" : "") + ">Altijd actief (huidig)</option>";
  page += String("<option value='1'") + ((sunSchedMode==1) ? " selected" : "") + ">Handmatig tijdvenster</option>";
  page += String("<option value='2'") + ((sunSchedMode==2) ? " selected" : "") + ">Slim: goedkoopste opeenvolgende blokken overdag</option>";
  page += String("<option value='3'") + ((sunSchedMode==3) ? " selected" : "") + ">Uit (zonneregeling uit)</option>";
  page += "</select>";

  // Dashboard label (Daylight/Cheapest) – alleen UI
  page += "<div class='row'><div class='k'>Label: Daylight / Cheapest (Dashboard)</div><div class='v'>";
  page += String("<span class='switch ") + ((dashShowSunWindowLabel) ? "on" : "") + "' onclick='sw(this,false)'>";
  page += String("<input type='checkbox' name='dash_sun_lbl' value='1' ") + (dashShowSunWindowLabel ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";
  page += "<div class='help'>Toont op het Dashboard: Daylight (zonsopgang/zonsondergang) en het gekozen venster.</div>";
  page += "<div class='help'>Let op: Uit schakelt alleen NORMAL (zonne-overschot) uit. Force heat, Legionella en Prijs/Boost blijven werken.</div>";

  // Handmatig tijdvenster
  page += "<label>Handmatig tijdvenster (alleen modus: Handmatig)</label>";
  page += "<div class='row2'>";
  page += "<div><label class='mini'>Start</label><input type='time' name='sun_w_s' value='" + fmtHm(sunWinStartMin) + "'></div>";
  page += "<div><label class='mini'>Einde</label><input type='time' name='sun_w_e' value='" + fmtHm(sunWinEndMin) + "'></div>";
  page += "</div>";

  // Slim: goedkoopste blokken binnen daylight
  page += "<label>Slim: goedkoopste opeenvolgende blokken overdag</label>";
  page += "<div class='row2'>";
  page += "<div><label class='mini'>Aantal blokken</label><input name='sun_c_n' type='number' min='1' max='96' value='" + String(sunCheapNBlocks) + "'></div>";
  page += "<div><label class='mini'>Marge (min)</label><input name='sun_marg' type='number' min='0' max='180' value='" + String(sunDaylightMarginMin) + "'></div>";
  page += "</div>";

  // Stand-alone locatie
  page += "<label>Locatie (voor zonsopgang/zonsondergang)</label>";
  page += "<div class='row2'>";
  page += "<div><label class='mini'>Latitude</label><input name='sun_lat' type='number' step='0.0001' value='" + String(sunLat,4) + "'></div>";
  page += "<div><label class='mini'>Longitude</label><input name='sun_lon' type='number' step='0.0001' value='" + String(sunLon,4) + "'></div>";
  page += "</div>";
  page += "<div class='help'>Zonopgang/zonsondergang wordt stand-alone berekend (timezone: Europe/Amsterdam, incl. zomer-/wintertijd).</div>";

  page += "<div class=\'row\' style=\'margin-top:14px\'><button class=\'btn\' type=\'submit\' name=\'_section\' value=\'energy\' data-section=\'energy\' formaction=\'/settings/save\'>Opslaan</button></div>";
page += "</div></details>";

  page += String("<details class='acc'><summary><div class='sum'><b>Nordpool prijzen</b><span class='tag'>") + (npInclTaxEnabled ? "incl" : "spot") + "</span></div></summary><div class='accbd'>";

  // Elektriciteit prijsbron (dynamisch via Nordpool of vaste all-in prijs)
  page += "<label>Elektriciteitsprijs bron</label>";
  page += "<select id='elec_src' name='elec_src'>";
  page += String("<option value='0'") + ((elecPriceSource==0) ? " selected" : "") + ">Dynamisch (Nordpool)</option>";
  page += String("<option value='1'") + ((elecPriceSource==1) ? " selected" : "") + ">Vast (all-in)</option>";
  page += "</select>";
  page += "<div class='help'>Kies <b>Vast (all-in)</b> als je geen dynamisch stroomcontract gebruikt. Dan gebruikt de Solar Controller jouw vaste stroomprijs als basis voor de vergelijking (gas vs elektra).</div>";

  // Vaste stroomprijs (toon alleen als Vast geselecteerd is)
  {
    String v = String(elecFixedCtKwhAllIn, 3);
    v.replace('.', ',');
    page += String("<div id='elec_fixed_box' style='display:") + ((elecPriceSource==1) ? "block" : "none") + ";'>";
    page += "<label>Vaste stroomprijs (all-in, ct/kWh)</label>";
    page += "<input id='elec_fix_ct' name='elec_fix_ct' value='" + v + "' placeholder='Bijv. 28,500'>";
    page += "<div class='help'>All-in = inclusief energiebelasting/inkoop/BTW (net als gas). Eenheid: <b>ct/kWh</b>. Decimaal: <b>komma</b> (NL).</div>";
    page += "</div>";
  }

  // Dynamische Nordpool opties (verberg als Vast geselecteerd is)
  page += String("<div id='np_dyn_opts' style='display:") + ((elecPriceSource==1) ? "none" : "block") + ";'>";

  page += "<div class='row'><div class='k'>Nordpool inschakelen</div><div class='v'>";
page += String("<span class='switch ") + ((npEnabled) ? "on" : "") + "' onclick='sw(this,false)'>";
page += String("<input type='checkbox' name='np_en' value='1' ") + ((npEnabled) ? "checked" : "") + "><span class='slider'></span></span>";
page += "</div></div>";

  page += "<label>Gebied (delivery area)</label>";
  page += "<input name='np_area' value='" + npArea + "' placeholder='NL of BE'>";

  page += "<label>Prijsresolutie</label><select name='np_res'>";
  page += String("<option value='60'") + ((npResMin==60) ? " selected" : "") + ">Uurprijzen (60 min)</option>";
  page += String("<option value='15'") + ((npResMin==15) ? " selected" : "") + ">Kwartierprijzen (15 min)</option>";
  page += "</select>";


  // --- Optioneel: belastingen/kosten bij spotprijs optellen (grafiek + “nu” prijs op /prices) ---
  page += "<div class='sep'></div>";
  page += "<b>Inclusief belastingen</b>";

  page += "<div class='row'><div class='k'>Inclusief belastingen</div><div class='v'>";
  page += String("<span class='switch ") + ((npInclTaxEnabled) ? "on" : "") + "' onclick='sw(this,false)'>";
  page += String("<input type='checkbox' name='np_tax_en' value='1' ") + ((npInclTaxEnabled) ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";

  page += "<label>Energiebelasting [ct/kWh]</label>";
  page += "<input type='number' step='0.0001' min='0' name='np_tax_eb' value='" + String(npEnergyTaxCt,4) + "'>";

  page += "<label>Inkoopkosten [ct/kWh]</label>";
  page += "<input type='number' step='0.0001' min='0' name='np_tax_in' value='" + String(npInkoopCt,4) + "'>";

  page += "<label>BTW [%]</label>";
  page += "<input type='number' step='0.1' min='0' max='100' name='np_tax_btw' value='" + String(npBtwPct,1) + "'>";

// --- Prijs-boost (PWM MAX op goedkope/negatieve blokken) ---
page += "<hr class='sep'>";
  page += "<div class='row'><div class='k'>Prijs-boost (PWM MAX)</div><div class='v'>";
  page += String("<span class='switch ") + ((npBoostEnabled) ? "on" : "") + "' onclick='sw(this,false)'>";
  page += String("<input type='checkbox' name='np_boost_en' value='1' ") + ((npBoostEnabled) ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";


page += "<label>Prijsbron voor boost</label>";
page += "<select name='np_boost_src'>";
page += String("<option value='0' ") + ((npBoostPriceSource==0) ? "selected" : "") + ">Spotprijs</option>";
page += String("<option value='1' ") + ((npBoostPriceSource==1) ? "selected" : "") + ">Totale prijs (incl. belastingen)</option>";
page += "</select>";

page += "<label>PWM MAX als prijs ≤ [ct/kWh]</label>";
page += "<div class='inrow'><button type='button' class='pm' onclick=\"pm('np_boost_thr')\">±</button>";
page += "<input id='np_boost_thr' type='number' step='0.0001' min='-500' max='500' name='np_boost_thr' value='" + String(npBoostThresholdCt,4) + "'></div>";

page += "<label>Aantal boost-blokken vandaag (N)</label>";
page += "<input type='number' step='1' min='0' max='96' name='np_boost_n' value='" + String(npBoostNBlocks) + "'>";
page += "<div class='help'>Selecteert de <b>N</b> meest goedkope (meest negatieve) blokken van <b>vandaag</b> waarvoor prijs ≤ drempel. Als er minder dan N blokken voldoen, worden alleen die blokken gebruikt. Legionella blijft altijd leidend.</div>";


  page += "<div class='help'>Spotprijs is standaard <b>zonder</b> belastingen. Zet <b>Inclusief belastingen</b> aan om energiebelasting + inkoop + BTW mee te nemen. Morgen komt meestal na 14:00. Pagina: <b>/prices</b>.</div>";
  page += "</div>"; // np_dyn_opts
  page += "<div class=\'row\' style=\'margin-top:14px\'><button class=\'btn\' type=\'submit\' name=\'_section\' value=\'nordpool\' data-section=\'nordpool\' formaction=\'/settings/save\'>Opslaan</button></div>";
page += "</div></details>";

  
  // Gas menu (Settings)
  page += "<details class='acc'><summary><div class='sum'><b>Gas</b><span class='tag'>prijzen</span></div></summary><div class='accbd'>";

  page += "<div class='help'>Gas-grafiek gebruikt de kale marktprijs (spot, excl. BTW) en kan in de Prijzen-tab worden omgerekend naar totaal: (spot + energiebelasting + inkoopkosten) × (1 + btw).</div>";

  page += "<div class='row'><div class='k'>Standaard weergave in Prijzen-tab</div><div class='v'>";
  page += String("<span class='switch ") + ((gasTotalEnabled) ? "on" : "") + "' onclick='sw(this,false)'>";
  page += String("<input type='checkbox' name='gas_tot_en' value='1' ") + ((gasTotalEnabled) ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";

  page += "<label>Gasprijs bron</label><select name='gas_src'>";
  page += String("<option value='0'") + ((gasPriceSource==0) ? " selected" : "") + ">Spot (API)</option>";
  page += String("<option value='1'") + ((gasPriceSource==1) ? " selected" : "") + ">Vast (handmatig)</option>";
  page += "</select>";

  page += "<label>Vaste gasprijs (EUR/m³)</label>";
  page += "<input type='number' name='gas_fix' value='" + fmtDot(gasFixedEurM3,5) + "' step='0.00001' inputmode='decimal' lang='nl-NL' placeholder='0,00000'>";

  page += "<label>Energiebelasting (EUR/m³)</label>";
  // Gebruik native number-spinner (pijltjes rechts) – zoals bij Nordpool.
  page += "<input type='number' id='gas_tax_eb' name='gas_tax_eb' value='" + fmtDot(gasEnergyTaxEur,5) + "' step='0.00001' inputmode='decimal' lang='nl-NL' placeholder='0,00000'>";

  page += "<label>Inkoopkosten (EUR/m³)</label>";
  page += "<input type='number' id='gas_tax_in' name='gas_tax_in' value='" + fmtDot(gasInkoopEur,5) + "' step='0.00001' inputmode='decimal' lang='nl-NL' placeholder='0,00000'>";

  page += "<label>BTW (%)</label>";
  page += "<input type='number' id='gas_tax_btw' name='gas_tax_btw' value='" + fmtDot(gasBtwPct,5) + "' step='0.00001' inputmode='decimal' lang='nl-NL' placeholder='21,00000'>";


page += "<hr style='opacity:.15;margin:12px 0;'>";

page += "<div class='row'><div class='k'>Advies Gas vs Elektra</div><div class='v'>";
// Gebruik EXACT dezelfde toggle-methode als de rest (onclick -> sw(this,false))
page += String("<span class='switch ") + ((heatAutoEnabled) ? "on" : "") + "' onclick='sw(this,false)'><input type='checkbox' name='heat_auto' value='1' " + ((heatAutoEnabled) ? "checked" : "") + "><span class='slider'></span></span>";
page += "</div></div>";

page += "<div class='row'><div class='k'>Voer ELEC-advies uit (automatisch elektrisch verwarmen)</div><div class='v'>";
page += String("<span class='switch ") + ((heatExecElecWhenCheaper) ? "on" : "") + "' onclick='sw(this,false)'><input type='checkbox' name='heat_exec_el' value='1' " + ((heatExecElecWhenCheaper) ? "checked" : "") + "><span class='slider'></span></span>";
page += "</div></div>";
page += "<div class='help'>Automatisch verwarmen start alleen wanneer actuele gas- én elektraprijzen beschikbaar zijn en ELEC werkelijk goedkoper is. Bij ontbrekende prijsdata blijft de normale zonneregeling actief.</div>";

page += "<div class='row'><div class='k'>Alleen verwarmen tijdens goedkoopste aaneengesloten ELEC-blokken</div><div class='v'>";
page += String("<span class='switch ") + ((heatCheapBlocksEnabled) ? "on" : "") + "' onclick='sw(this,false)'><input type='checkbox' name='heat_ch_en' value='1' " + ((heatCheapBlocksEnabled) ? "checked" : "") + "><span class='slider'></span></span>";
page += "</div></div>";

page += "<div class='row'><div class='k'>Gebruik boiler-doeltemperatuur bij ELEC-blokken</div><div class='v'>";
page += String("<span class='switch ") + ((heatCheapUseBoilerTarget) ? "on" : "") + "' onclick='sw(this,false)'><input type='checkbox' name='heat_ch_bt' value='1' " + ((heatCheapUseBoilerTarget) ? "checked" : "") + "><span class='slider'></span></span>";
page += "</div></div>";
page += "<div class='help'>Gebruikt uitsluitend de expliciet gekozen boilersensor bij Legionella en de daar ingestelde doeltemperatuur. AAN: AUTO_ELEC stopt zodra de doeltemperatuur is bereikt. UIT: de doeltemperatuur begrenst de N-run niet. Is geen boilersensor gekozen of is de sensor niet geldig, dan blijft alleen de prijsplanning leidend.</div>";

page += "<label>Max. aantal goedkope aaneengesloten blokken</label>";
page += "<input type='number' id='heat_ch_n' name='heat_ch_n' value='" + String(heatCheapNBlocks) + "' min='1' max='96' step='1' inputmode='numeric' placeholder='4'>";
{
  int heatBlockMin = (npResMin == 15) ? 15 : 60;
  int totalMin = heatCheapNBlocks * heatBlockMin;
  String duur = (totalMin % 60 == 0) ? (String(totalMin / 60) + " uur") : (String(totalMin) + " minuten");
  page += "<div class='help'>N is een maximum. Bij de huidige prijsinstelling is 1 blok " + String(heatBlockMin) + " minuten; " + String(heatCheapNBlocks) + " blokken is maximaal " + duur + ". Bij een nieuw plan worden alle resterende blokken van vandaag bekeken en wordt de goedkoopste aaneengesloten ELEC-reeks gekozen. Een GAS-blok wordt nooit toegevoegd. Zijn nergens N opeenvolgende ELEC-blokken beschikbaar, dan wordt de langste geschikte reeks onder N gebruikt. Om 00:00 begint een nieuw plan.</div>";
  page += "<div class='help'>Een expliciet gekozen boilersensor kan nieuwe warmtevraag herkennen wanneer de temperatuur na opwarming weer minimaal 5 °C onder de ingestelde boiler-doeltemperatuur komt. Met 'Gebruik boiler-doeltemperatuur bij ELEC-blokken' AAN stopt AUTO_ELEC bovendien bij de doeltemperatuur; met deze toggle UIT wordt de gekozen N-run niet door de doeltemperatuur afgebroken.</div>";
}

page += "<label>kWh per m³ gas</label>";
page += "<input type='number' id='heat_kwhm3' name='heat_kwhm3' value='" + fmtDot(gasKwhPerM3,5) + "' step='0.00001' inputmode='decimal' lang='nl-NL' placeholder='9,77000'>";

page += "<label>Rendement gasboiler (0..1)</label>";
page += "<input type='number' id='heat_geff' name='heat_geff' value='" + fmtDot(gasBoilerEff,5) + "' min='0' max='1' step='0.00001' inputmode='decimal' lang='nl-NL' placeholder='0,90000'>";

page += "<label>COP elektrisch verwarmen (0..10)</label>";
page += "<input type='number' id='heat_eeff' name='heat_eeff' value='" + fmtDot(elecBoilerEff,5) + "' min='0' max='10' step='0.01' inputmode='decimal' lang='nl-NL' placeholder='1,00'>";

page += "<label>Extra marge op elektriciteit (ct/kWh)</label>";
page += "<input type='number' id='heat_hyst' name='heat_hyst' value='" + fmtDot(heatElecMarginCt,2) + "' min='0' max='99' step='0.01' inputmode='decimal' lang='nl-NL' placeholder='0,00'>";

page += "<label>Min. minuten tussen advies-switch</label>";
page += "<input type='number' id='heat_minsw' name='heat_minsw' value='" + String(heatMinSwitchMin) + "' step='1' inputmode='numeric' placeholder='30'>";

page += "<div class='hint'>Let op: de Solar Controller kan je elektrische boiler direct sturen. Voor gas kun je het advies via MQTT/HA gebruiken (automation/thermostaat). Legionella/Force blijven altijd voorrang houden.</div>";

  page += "<div class=\'row\' style=\'margin-top:14px\'><button class=\'btn\' type=\'submit\' name=\'_section\' value=\'gas\' data-section=\'gas\' formaction=\'/settings/save\'>Opslaan</button></div>";
page += "</div></details>";

  // ================= Fase-beveiliging (Force/Legionella/Price Boost) =================
  // Let op: in stap 3 doen we nog GEEN PWM-ingreep, we slaan alleen de settings op
  // en berekenen de statusflag (pg_limit) voor dashboard/MQTT.
  page += "<details class='acc'><summary><div class='sum'><b>Fase-beveiliging</b><span class='tag'>A</span></div></summary><div class='accbd'>";

  page += "<div class='row'><div class='k'>Inschakelen</div><div class='v'>";
  page += String("<span class='switch ") + (phaseGuardEnabled ? "on" : "") + "' onclick='sw(this,false)'>";
  page += String("<input type='checkbox' name='pg_en' value='1' ") + (phaseGuardEnabled ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";

  if (!mcEnabled) {
    page += "<label>Solar Controller zit op fase</label>";
    page += "<select name='pg_ph'>";
    page += String("<option value='1'") + (phaseGuardPhase==1 ? " selected" : "") + ">L1</option>";
    page += String("<option value='2'") + (phaseGuardPhase==2 ? " selected" : "") + ">L2</option>";
    page += String("<option value='3'") + (phaseGuardPhase==3 ? " selected" : "") + ">L3</option>";
    page += "</select>";
    page += "<label>Max ampère op deze fase</label>";
    page += "<input name='pg_maxa' type='number' step='0.1' min='1' max='63' value='" + String(getConfiguredPhaseGuardMaxA(phaseGuardPhase),1) + "'>";
    page += "<div class='help'>Actief tijdens <b>Forceer verwarmen</b>, <b>Legionella</b>, <b>Price Boost</b> en <b>ELEC-advies</b>. In NORMAL (zonne-overschot) regelt PWM al op overschot.</div>";
    page += "<div class='help'>Alleen beschikbaar als je P1-meter (Youless/HomeWizard/Smart Gateways) fase-ampères levert.</div>";
  } else {
    page += "<div class='help'><b>Multi Controller aan:</b> koppel op de <b>master</b> iedere actieve controller aan de juiste fase, ook netwerk-controllers. De master plant daarna de hele groep per fase; een slave houdt lokaal alleen een harde noodrem op zijn eigen fase.</div>";
    page += "<div class='help'>Actief tijdens <b>Forceer verwarmen</b>, <b>Legionella</b>, <b>Price Boost</b>, <b>ELEC-advies</b> en groepsregeling. In NORMAL blijft volgorde leidend, maar als een fase vol zit mag de master alleen die fase terugnemen.</div>";
    page += "<div class='help'>De ampèrelimiet stel je <b>per fase</b> in. Controllers die op dezelfde fase zitten delen dus dezelfde fasegrens.</div>";
    page += "<div class='help'>Alleen beschikbaar als je P1-meter (Youless/HomeWizard/Smart Gateways) fase-ampères levert.</div>";
    for (uint8_t i = 0; i < MC_MAX_SLOTS; i++) {
      const bool slotVisible = (i < mcControllerCount) && ((mcRole != MC_ROLE_SLAVE) || (mcSlots[i].slotType == MC_SLOT_LOCAL_PWM));
      if (!slotVisible) continue;
      String ctrlName = mcSlots[i].name.length() ? mcSlots[i].name : (String("Controller ") + String(i + 1));
      const uint8_t slotPhase = multiControllerGetSlotAssignedPhase(i);
      page += "<hr style='opacity:.15;margin:12px 0;'>";
      page += "<div class='help'><b>" + ctrlName + "</b> — " + String((mcSlots[i].slotType == MC_SLOT_NETWORK_PEER) ? "netwerk-controller" : "lokale PWM-uitgang") + "</div>";
      page += "<label>Controller zit op fase</label>";
      page += "<select name='pg_mc_s" + String(i + 1) + "_ph'>";
      page += String("<option value='1'") + (slotPhase==1 ? " selected" : "") + ">L1</option>";
      page += String("<option value='2'") + (slotPhase==2 ? " selected" : "") + ">L2</option>";
      page += String("<option value='3'") + (slotPhase==3 ? " selected" : "") + ">L3</option>";
      page += "</select>";
    }
    page += "<hr style='opacity:.15;margin:12px 0;'>";
    page += "<label>Max ampère L1</label>";
    page += "<input name='pg_maxa_l1' type='number' step='0.1' min='1' max='63' value='" + String(phaseGuardMaxA_L1,1) + "'>";
    page += "<label>Max ampère L2</label>";
    page += "<input name='pg_maxa_l2' type='number' step='0.1' min='1' max='63' value='" + String(phaseGuardMaxA_L2,1) + "'>";
    page += "<label>Max ampère L3</label>";
    page += "<input name='pg_maxa_l3' type='number' step='0.1' min='1' max='63' value='" + String(phaseGuardMaxA_L3,1) + "'>";
  }
  page += "<hr style='opacity:.15;margin:12px 0;'>";
  page += "<div class='row'><div class='k'>Zacht opstarten bij prioriteitsmodi</div><div class='v'>";
  page += String("<span class='switch ") + (softStartEnabled ? "on" : "") + "' onclick='sw(this,false)'>";
  page += String("<input type='checkbox' name='ss_en' value='1' ") + (softStartEnabled ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";
  page += "<label>Opstartduur (seconden)</label>";
  page += "<input name='ss_dur' type='number' step='1' min='0' max='30' value='" + String(softStartDurationSec) + "'>";
  page += "<label>Start PWM (%)</label>";
  page += "<input name='ss_start' type='number' step='1' min='0' max='30' value='" + String(softStartStartPct,0) + "'>";
  page += "<div class='help'>Zacht opstarten voorkomt een korte piek wanneer <b>Price Boost</b>, <b>Legionella</b> of <b>Force heat</b> inschakelt. 0s = direct (oud gedrag).</div>";

  page += "<div class='row' style='margin-top:14px'><button class='btn' type='submit' name='_section' value='fase' data-section='fase' formaction='/settings/save'>Opslaan</button></div>";
  page += "</div></details>";

  // ================= MQTT =================
  page += "<details class='acc'><summary><div class='sum'><b>MQTT</b><span class='tag'>HA</span></div></summary><div class='accbd'>";

  page += "<div class='row'><div class='k'>MQTT inschakelen</div><div class='v'>";
page += String("<span class='switch ") + ((mqttEnabled) ? "on" : "") + "' onclick='sw(this,false)'>";
page += String("<input type='checkbox' name='mqtt_enabled' value='1' ") + ((mqttEnabled) ? "checked" : "") + "><span class='slider'></span></span>";
page += "</div></div>";
page += "<label>MQTT host (broker)</label><input name='mqtt_host' value='" + mqttHost + "'>";
  page += "<label>MQTT poort</label><input type='number' name='mqtt_port' value='" + String(mqttPort) + "'>";
  page += "<label>MQTT gebruiker</label><input name='mqtt_user' value='" + mqttUser + "'>";
  page += "<label>MQTT wachtwoord (leeg = ongewijzigd)</label><input type='password' name='mqtt_pass' value='' placeholder='••••••••'>";
  page += "<label>MQTT basistopic</label><input name='mqtt_topic' value='" + mqttBaseTopic + "'>";

  page += "<div class='sep'></div>";
  page += "<b>Home Assistant</b>";
  page += "<div class='row'><div class='k'>MQTT Discovery</div><div class='v'>";
page += String("<span class='switch ") + ((haDiscoveryEnabled) ? "on" : "") + "' onclick='sw(this,false)'>";
page += String("<input type='checkbox' name='ha_disc' value='1' ") + ((haDiscoveryEnabled) ? "checked" : "") + "><span class='slider'></span></span>";
page += "</div></div>";
  page += "<div class='row'><div class='k'>HA Discovery opschonen</div><div class='v'>";
  page += "<button class='btn ghost' type='submit' name='_section' value='mqtt_cleanup' formaction='/settings/ha_mqtt_cleanup'>HA Discovery opschonen</button>";
  page += "<div class='help'>Meldt oude HA MQTT-entiteiten af en meldt daarna alleen de huidige geldige entiteiten opnieuw aan.</div>";
  page += "</div></div>";
page += "<div class='sep'></div>";
  page += "<b>Diagnostiek</b>";

  page += "<div class='row'><div class='k'>Publish system data</div><div class='v'>";
    page += String("<span class='switch ") + ((sysPublishEnabled) ? "on" : "") + "' onclick='sw(this,false)'>";
  page += String("<input type='checkbox' name='sys_pub' value='1' ") + ((sysPublishEnabled) ? "checked" : "") + "><span class='slider'></span></span>";
  page += "</div></div>";

  page += "<label>Publish interval [s]</label>";
  page += "<input type='number' min='10' max='3600' step='1' name='sys_int' value='" + String(sysPublishIntervalSec) + "'>";

  page += "<label>Status publish interval [s]</label>";
  page += "<input type='number' min='1' max='3600' step='1' name='st_int' value='" + String(statusPublishIntervalSec) + "'>";
  page += "<div class='help'>MQTT topic: <code>" + mqttBaseTopic + "/status</code> (default 5s). Lager = sneller, hoger = rustiger.</div>";

  // step26 (minimaal): live MQTT connectie status in de UI (pullt /api/status)
  page += "<div class='tiny'>MQTT status: <span id='mqtt_status' class='bad'>Niet verbonden</span> <span id='mqtt_status_extra' class='muted'></span></div>";

  page += "<div class=\'row\' style=\'margin-top:14px\'><button class=\'btn\' type=\'submit\' name=\'_section\' value=\'mqtt\' data-section=\'mqtt\' formaction=\'/settings/save_reboot\'>Opslaan & reboot</button></div>";
page += "</div></details>";

  // Altijd zichtbaar buiten inklapmenu's:
  // Let op: Temperatuursensoren heeft een eigen <form>. Om nested forms te voorkomen sluiten we hier de hoofdform.
  page += "</form>";

// ================= Temperatuursensoren (DS18B20) =================
page += "<details class='acc'><summary><div class='sum'><b>Temperatuursensoren</b><div class='tags'><span class='tag'>DS18B20</span><span class='tag'>HA</span></div></div></summary><div class='accbd'>";
page += "<form method='POST' action='/settings/save_sensors' novalidate>";

// Toggle: publish names to HA
page += "<div class='row'><div class='k'>Publiceer sensornamen naar Home Assistant</div><div class='v'>";
page += String("<span class='switch ") + (tempPublishNamesToHA ? "on" : "") + "' onclick='sw(this,false)'>";
page += String("<input type='checkbox' name='ts_pub' value='1' ") + (tempPublishNamesToHA ? "checked" : "") + "><span class='slider'></span></span>";
page += "</div></div>";
page += "<div class='help'>Als dit aan staat, neemt Home Assistant automatisch de namen over die je hier instelt (via MQTT Discovery).</div>";

if (!ds18Available || tempSensorCount == 0) {
  page += "<div class='help'><b>Geen DS18B20 sensoren gevonden.</b> Sluit ze aan op GPIO22 met een 4.7k pull-up naar 3.3V en herstart.</div>";
} else {
  page += "<div class='help'>Kies per sensor: naam, enable en één CONTROL sensor (die wordt gebruikt voor Legionella temp / lastTempC).</div>";

  for (int i = 0; i < tempSensorCount; i++) {
    TempSensorEntry& e = tempSensors[i];

    page += "<hr style='opacity:.12; margin:12px 0'>";
    page += "<div class='row'><div class='k'><b>Sensor " + String(i + 1) + "</b>";
    page += "<div class='help' style='margin-top:6px'>Address: <code>" + e.addrStr + "</code>";
    if (!isnan(e.tempC)) {
      if (tempIsValid(e.tempC)) {
        page += " • " + String(e.tempC, 1) + " °C";
      } else if (fabsf(e.tempC - 85.0f) < 0.1f) {
        page += " • <span style='color:#ffb74d'><b>85°C</b> (conversie nog niet klaar)</span>";
      } else if (e.tempC <= -100.0f) {
        page += " • <span style='color:#ff6b6b'><b>-127°C</b> (sensor niet bereikbaar)</span>";
      } else {
        page += " • <span style='color:#ffb74d'>" + String(e.tempC, 1) + " °C (ongeldig)</span>";
      }
    }
    page += "</div></div><div class='v'></div></div>";

    // Naam
    page += "<label>Naam</label>";
    page += "<input name='ts_name_" + String(i) + "' value='" + htmlAttrEscape(e.name) + "' placeholder='Bijv. Boiler boven'>";

    // Enable + Control
    page += "<div class='row2'>";

    // Enable switch
    page += "<div><label class='mini'>Actief</label>";
    page += String("<span class='switch ") + (e.enabled ? "on" : "") + "' onclick='sw(this,false)'>";
    page += String("<input type='checkbox' name='ts_en_" + String(i) + "' value='1' ") + (e.enabled ? "checked" : "") + "><span class='slider'></span></span>";
    page += "</div>";

    // Control radio
    bool isCtrl = (e.addrStr == tempControlAddrStr);
    page += "<div><label class='mini'>CONTROL</label>";
    page += "<label style='display:flex; gap:10px; align-items:center; padding:10px 0'>";
    page += "<input type='radio' name='ts_ctrl' value='" + e.addrStr + "' " + String(isCtrl ? "checked" : "") + ">";
    page += "<span class='help' style='margin:0'>Gebruik deze sensor voor Legionella temp</span>";
    page += "</label></div>";

    page += "</div>"; // row2
  }
}

page += "<div class='row' style='margin-top:14px'><button class='btn' type='submit'>Opslaan</button></div>";
page += "</form>";
page += "</div></details>";

// ================= Weergave (Theme) =================
// Optie A: onder Temperatuursensoren een eenvoudige keuze voor Dark mode.
page += "<details class='acc'><summary><div class='sum'><b>Weergave</b><span class='tag'>theme</span></div></summary><div class='accbd'>";
page += "<form method='POST' action='/settings/save'>";
page += "<div class='row'><div class='k'>Donkere modus</div><div class='v'>";
page += String("<span class='switch ") + ((uiDarkMode) ? "on" : "") + "' onclick=\"sw(this,false);document.getElementById('ui_dark').value=document.getElementById('ui_dark_chk').checked?'1':'0';\">";
page += String("<input type='hidden' name='ui_dark' id='ui_dark' value='") + (uiDarkMode ? "1" : "0") + "'>";
page += String("<input type='checkbox' name='ui_dark_chk' value='1' ") + (uiDarkMode ? "checked" : "") + " id='ui_dark_chk' onchange=\"document.getElementById('ui_dark').value = this.checked ? '1' : '0';\">";
page += "<span class='slider'></span></span>";
page += "</div></div>";
page += "<div class='help'>Zelfde groen/wit thema, maar met een donkere achtergrond en lichte tekst (fijner in de avond).</div>";
page += "<div class='row' style='margin-top:14px'><button class='btn' type='submit'>Opslaan</button></div>";
page += "</form>";
page += "</div></details>";


  page += "<div style='height:10px'></div>";
  page += "<a href='/update' style='text-decoration:none'><button class='btn dark' type='button'>OTA update</button></a>";


  // WiFi scan (op aanvraag) – vult dropdown met SSID's (voor verborgen SSID blijft handmatig typen mogelijk)
    page += "<script>";
  page += "function pm(id){var el=document.getElementById(id);if(!el)return;var s=(el.value||'0').toString().replace(',','.');var v=parseFloat(s);if(!isFinite(v))v=0;v=-v;var step=(el.getAttribute('step')||'');if(step.indexOf('.')>=0){var dec=step.split('.')[1].length;el.value=v.toFixed(dec);}else{el.value=String(v);}el.dispatchEvent(new Event('input',{bubbles:true}));el.dispatchEvent(new Event('change',{bubbles:true}));}";
  page += "function parseEU(v){v=(v||'').toString().trim();v=v.replace(',','.');var n=parseFloat(v);return isFinite(n)?n:0;}";
  page += "function fmtEU(n,dec){if(!isFinite(n))n=0;var s=n.toFixed(dec);return s.replace('.',',');}";
  page += "function fixEU(id,dec){var el=document.getElementById(id);if(!el)return;var n=parseEU(el.value);el.value=fmtEU(n,dec);}";
  page += "function spinEU(id,dir,step,dec){var el=document.getElementById(id);if(!el)return;var n=parseEU(el.value);n=n+(dir*step);el.value=fmtEU(n,dec);el.dispatchEvent(new Event('input',{bubbles:true}));el.dispatchEvent(new Event('change',{bubbles:true}));}";
  // Gas gebruikt nu <input type='number'> met de native spinner (zoals Nordpool).
  // Daarom géén custom EU-formatting (fixEU/spinEU) op deze velden.
  page += "";
  page += "function onSsidPick(){var sel=document.getElementById(\"ssid_select\");var man=document.getElementById(\"ssid_manual\");if(!sel||!man)return;var v=sel.value||\"\";if(v)man.value=v;}";
  page += "async function scanWifi(){var st=document.getElementById('ssid_status');var sel=document.getElementById('ssid_select');var man=document.getElementById('ssid_manual');var btn=document.getElementById('btn_wifi_scan');if(!st||!sel||!man){return false;}st.innerHTML='Scannen…';sel.innerHTML='<option value=\"\">— Kies uit scanlijst —</option>';if(btn){btn.disabled=true;btn.textContent='Scannen…';}try{var r=await fetch('/api/wifi_scan',{cache:'no-store'});if(!r.ok){throw new Error('HTTP '+r.status);}var j=await r.json();var arr=[];if(j){if(Array.isArray(j.aps)){arr=j.aps;}else if(Array.isArray(j.networks)){arr=j.networks;}}var cnt=0;for(var i=0;i<arr.length;i++){var ap=arr[i]||{};var ssid=(ap.ssid||ap.SSID||'');if(!ssid){continue;}var rssi=(ap.rssi!==undefined)?ap.rssi:ap.RSSI;var opt=document.createElement('option');opt.value=ssid;opt.textContent=(rssi!==undefined&&rssi!==null&&rssi!=='')?(ssid+' ('+rssi+' dBm)'):ssid;sel.appendChild(opt);cnt++;}if(cnt===0){st.innerHTML='Geen netwerken gevonden.';}else{st.innerHTML='Gevonden: <b>'+cnt+'</b> (kies uit de lijst of typ handmatig).';}}catch(e){st.innerHTML='Scan mislukt: '+(e&&e.message?e.message:e);}finally{if(btn){btn.disabled=false;btn.textContent='Scan WiFi';}}return false;}";
  // step26 (minimaal): MQTT connectie status in Settings (via /api/status)
  page += "async function refreshMqttUi(){var s=document.getElementById('mqtt_status');var x=document.getElementById('mqtt_status_extra');if(!s){return;}try{var r=await fetch('/api/status_light',{cache:'no-store'});if(!r.ok){throw new Error('HTTP '+r.status);}var j=await r.json();var ok=!!(j&&j.mqtt_connected);s.textContent=ok?'Verbonden':'Niet verbonden';s.className=ok?'ok':'bad';if(x){var b=(j&&j.mqtt_broker)?j.mqtt_broker:'';var t=(j&&j.mqtt_topic_base)?j.mqtt_topic_base:'';x.textContent=(b||t)?(' — '+b+(t?(' — '+t):'')):'';}}catch(e){s.textContent='Onbekend';s.className='bad';if(x){x.textContent='';}}}";
  page += "document.addEventListener(\"DOMContentLoaded\",function(){\n"
          "  // WiFi scan knop\n"
          "  var btn=document.getElementById(\"btn_wifi_scan\");\n"
          "  if(btn){btn.addEventListener(\"click\",function(ev){ev.preventDefault();scanWifi();});}\n"
          "  window.scanWifi=scanWifi;\n"
          "\n"
          "  // MQTT status\n"
          "  refreshMqttUi();\n"
          "  setInterval(refreshMqttUi,3000);\n"
          "\n"
          "  // NL decimaal (komma) voor vaste stroomprijs\n"
          "  try{ fixEU('elec_fix_ct',3); }catch(e){}\n"
          "\n"
          "  // UI: toon/verberg vaste prijs en Nordpool opties (zonder reload)\n"
          "  function applyElecSrcUi(){\n"
          "    var sel=document.getElementById('elec_src');\n"
          "    var fixed=document.getElementById('elec_fixed_box');\n"
          "    var dyn=document.getElementById('np_dyn_opts');\n"
          "    if(!sel) return;\n"
          "    var isFixed = (sel.value==='1');\n"
          "    if(fixed) fixed.style.display = isFixed ? 'block' : 'none';\n"
          "    if(dyn)   dyn.style.display   = isFixed ? 'none'  : 'block';\n"
          "  }\n"
          "  var sel=document.getElementById('elec_src');\n"
          "  if(sel){ sel.addEventListener('change', applyElecSrcUi); applyElecSrcUi(); }\n"
          "\n"
          "  function applyMcEnabledUi(){\n"
          "    var cb=document.getElementById('mc_en');\n"
          "    var panel=document.getElementById('mc_enabled_panel');\n"
          "    if(!cb || !panel) return;\n"
          "    panel.style.display = cb.checked ? '' : 'none';\n"
          "  }\n"
          "  var mcEn=document.getElementById('mc_en');\n"
          "  if(mcEn){ mcEn.addEventListener('change', applyMcEnabledUi); applyMcEnabledUi(); }\n"
          "\n"
          "  function applyMcSlotUi(slot){\n"
          "    var type=document.getElementById('mc_s'+slot+'_type');\n"
          "    if(!type) return;\n"
          "    var isNetwork=(type.value==='2');\n"
          "    var ip=document.getElementById('mc_s'+slot+'_ip');\n"
          "    var ipNa=document.getElementById('mc_s'+slot+'_ip_na');\n"
          "    var pin=document.getElementById('mc_s'+slot+'_pin');\n"
          "    var pinNa=document.getElementById('mc_s'+slot+'_pin_na');\n"
          "    if(ip){ ip.disabled=!isNetwork; ip.style.display=isNetwork?'':'none'; }\n"
          "    if(ipNa){ ipNa.style.display=isNetwork?'none':''; }\n"
          "    if(pin){ pin.disabled=isNetwork; pin.style.display=isNetwork?'none':''; }\n"
          "    if(pinNa){ pinNa.style.display=isNetwork?'':'none'; }\n"
          "  }\n"
          "  function initMcSlotUi(){\n"
          "    var types=document.querySelectorAll(\"select[data-mc-slot]\");\n"
          "    for(var i=0;i<types.length;i++){\n"
          "      (function(sel){\n"
          "        var slot=sel.getAttribute('data-mc-slot');\n"
          "        if(!slot) return;\n"
          "        sel.addEventListener('change', function(){ applyMcSlotUi(slot); });\n"
          "        applyMcSlotUi(slot);\n"
          "      })(types[i]);\n"
          "    }\n"
          "  }\n"
          "  initMcSlotUi();\n"
          "\n"
          "  function markSubmitBusy(ev){\n"
          "    var sb=(ev&&ev.submitter)?ev.submitter:null;\n"
          "    if(sb && !sb.dataset.busy){\n"
          "      sb.dataset.busy='1';\n"
          "      sb.disabled=true;\n"
          "      var t=(sb.textContent||'').trim();\n"
          "      if(t){ sb.textContent = (t.indexOf('reboot')>=0 || t.indexOf('Reboot')>=0) ? 'Opslaan & rebooten...' : 'Opslaan...'; }\n"
          "    }\n"
          "  }\n"
          "\n"
          "  // Veilig: bij submit comma -> punt (ESP verwacht dot, server-side doen we ook nog een replace)\n"
          "  var f=document.getElementById('mainSettingsForm');\n"
          "  if(f){\n"
          "    f.addEventListener('submit', function(ev){\n"
          "      var e=document.getElementById('elec_fix_ct');\n"
          "      if(e && e.value){ e.value = e.value.toString().replace(',', '.'); }\n"
          "      markSubmitBusy(ev);\n"
          "    });\n"
          "  }\n"
          "  var forms=document.querySelectorAll(\"form[action='/settings/save'],form[action='/settings/save_reboot'],form[action='/settings/save_sensors']\");\n"
          "  for(var fi=0; fi<forms.length; fi++){\n"
          "    if(forms[fi]===f) continue;\n"
          "    forms[fi].addEventListener('submit', markSubmitBusy);\n"
          "  }\n"
          "});";
  page += "</script>";

  page += htmlFooter("set");
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "text/html; charset=utf-8", page);
}

// ==================================================
// SETUP / LOOP
// ==================================================
