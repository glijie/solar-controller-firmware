/*
  Solar Controller
*/


#include <WiFi.h>
#include <esp_wifi.h>  // step34: force visible AP + channel 1 (setup mode)
#include <driver/gpio.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#define MQTT_MAX_PACKET_SIZE 4096  // nodig voor HA discovery + JSON status (PubSubClient)

// PubSubClient buffer: publish FAIL (len>~250) betekent: buffer te klein.
// - Nieuwere PubSubClient versies ondersteunen mqttClient.setBufferSize().
// - Als jouw PubSubClient dit NIET heeft, zet USE_PUBSUBCLIENT_SETBUFFERSIZE op 0
//   en pas MQTT_MAX_PACKET_SIZE in de PubSubClient library zelf aan.
#define MQTT_CLIENT_BUFFER_SIZE 4096
#define USE_PUBSUBCLIENT_SETBUFFERSIZE 1

#include <PubSubClient.h>
#include <math.h>
#include "src/app_globals.h"
#include "src/mqtt.h"
#include "src/legionella.h"
#include "src/nordpool.h"
#include "src/sensors.h"
#include "src/collector.h"
#include "src/MultiController.h"
#include "src/MultiControllerTelemetry.h"
#include "src/ControllerTemperatureGate.h"
#include "src/RelayControl.h"
#include "src/ConfigStore.h"
#include "src/ApiRoutes.h"
#include "src/PwmOutput.h"
#include "src/PhaseGuard.h"
#include "src/PhaseBudget.h"
#include "src/ScenarioLogic.h"
#include "src/WebRoutes.h"
#include "src/WebUtils.h"
#include "src/HealthMonitor.h"
#include "src/FirmwareUpdateManager.h"
#include "src/NetworkManager.h"
#include "src/MeterManager.h"
#include "src/TimeSchedule.h"
#include "src/EnergyApi.h"
#include "src/StatusLabels.h"
#include "src/MqttParsers.h"


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <time.h>
#include <esp_system.h>

// C++ helpers for optional PubSubClient features (setBufferSize detection)
// ===== DS18B20 (optioneel) =====
#include <OneWire.h>
#include <DallasTemperature.h>

// ================= VERSION =================
// Let op: dit nummer wordt ook getoond in de UI en in GitHub OTA check.
// Issue #13 fail-safe: AUTO_ELEC alleen bij een geldige actuele prijsvergelijking.
const char* FW_VERSION = "v2026.09.19.02";

float elecCtKwhNow();
float gasEurM3Now(bool total);



// ================= TEMP DEBUG =================
// Zet op 1 om DS18B20 diagnose in Serial Monitor te zien (aanrader tijdens inregelen)
#ifndef TEMP_SERIAL_DEBUG
#define TEMP_SERIAL_DEBUG 1
#endif

// ================= DEVICE NAME =================
// UI-naam (mag spaties) + veilige ID (voor MQTT/AP/IDs zonder spaties)
const char* DEVICE_NAME = "Solar Controller";
const char* DEVICE_ID   = "solar_controller";

// ================= PWM / HARDWARE =================
const int PWM_PIN      = 25;     // GPIO25 -> Kemo CTRL+
const int PWM_CHANNEL  = 0;
extern const int PWM_FREQ_HZ  = 1000;   // Kemo: 100..10.000 Hz (veilig)
extern const int PWM_RES_BITS = 8;      // 0..255 duty

// DS18B20 data pin (optioneel)
int DS18B20_PIN = 22;

// ================= POLL / TIMERS ==================
const unsigned long POLL_INTERVAL_MS      = 1000;   // 1s
const unsigned long TEMP_POLL_INTERVAL_MS = 5000;   // 15s (lager load, temp hoeft niet per 5s)
const unsigned long SCHED_CHECK_MS        = 3000;   // 3s (lager load, scheduling hoeft niet per seconde)

// ================= WIFI CACHE (load reduction) ==================
// We cachen SSID/IP/RSSI zodat /api/status niet telkens Strings hoeft te alloceren.
// Dit verlaagt heap-fragmentatie bij langdurig open dashboard / veel API-calls.
String cachedWifiIP   = "";
String cachedWifiSSID = "";
int    cachedWifiRSSI = 0;
bool   cachedWifiConnected = false;
unsigned long cachedWifiLastMs = 0;
const unsigned long WIFI_CACHE_MIN_UPDATE_MS = 2000;

void updateWifiCache(bool force = false) {
  const unsigned long nowMs = millis();
  if (!force && (nowMs - cachedWifiLastMs) < WIFI_CACHE_MIN_UPDATE_MS) return;

  const bool connected = (WiFi.status() == WL_CONNECTED);

  // Alleen bij (re)connect of force updaten we de wat "zwaardere" strings
  if (force || connected != cachedWifiConnected) {
    if (connected) {
      cachedWifiIP   = WiFi.localIP().toString();
      cachedWifiSSID = WiFi.SSID();
      cachedWifiRSSI = WiFi.RSSI();
    } else {
      cachedWifiIP   = "";
      cachedWifiSSID = "";
      cachedWifiRSSI = 0;
    }
    cachedWifiConnected = connected;
  } else if (connected) {
    // RSSI kan wel wisselen; die mogen we vaker verversen (is goedkoop)
    cachedWifiRSSI = WiFi.RSSI();
  }

  cachedWifiLastMs = nowMs;
}

// ================= DEFAULTS =======================
const float DEFAULT_MAX_BOILER_POWER_W    = 2000.0f;
const float DEFAULT_TARGET_EXPORT_W       = -50.0f;
const float DEFAULT_MIN_STEP_PERCENT      = 1.0f;
const unsigned long DEFAULT_MIN_INTERVAL  = 4000UL;
const float DEFAULT_MAX_OUTPUT_PERCENT    = 100.0f;

// ===== NORMAL regel-algoritme =====
// 0 = DIRECT (oude lineaire mapping)
// 1 = STEP   (nieuwe closed-loop stapregelaar)
const uint8_t DEFAULT_NORMAL_ALG          = 1;      // <-- default = STEP (jouw wens)

// ===== NORMAL tuning defaults (voor STEP) =====
// stepFactor bepaalt agressiviteit. Ruwweg:
//   errorW = maxBoilerPowerW  -> deltaPct ≈ stepFactor * 100% (maar gecapped door maxStepPctPerUpdatePerUpdate)
// Praktisch: stepFactor 0.10..0.40 werkt vaak goed.
const float DEFAULT_STEP_FACTOR           = 0.20f;  // agressiviteit
const float DEFAULT_DEADBAND_W            = 30.0f;  // rustzone: binnen +- deadband -> geen bijsturing
const float DEFAULT_MAX_STEP_PCT          = 10.0f;  // max stap per update (per seconde) in %

// Legionella defaults
const bool  DEFAULT_LEG_ENABLED           = false;
const int   DEFAULT_LEG_DOW               = 0;      // 0=Zondag .. 6=Zaterdag
const int   DEFAULT_LEG_HOUR              = 13;
const int   DEFAULT_LEG_MINUTE            = 0;
const int   DEFAULT_LEG_DURATION_MIN      = 90;     // max runtime / tijd-run
const float DEFAULT_LEG_OUTPUT_LIMIT_PCT  = 100.0f;

const bool  DEFAULT_LEG_USE_TEMP_SENSOR   = false;
const float DEFAULT_LEG_TARGET_TEMP_C     = 60.0f;
const int   DEFAULT_LEG_HOLD_MIN          = 15;
const bool  DEFAULT_LEG_RELAY_AUTO            = false;
const int   DEFAULT_LEG_RELAY_START_DELAY_MIN = 0;
const int   DEFAULT_LEG_RELAY_DELAY_MIN       = 0;

// System publish defaults
const bool  DEFAULT_SYS_PUB_ENABLED       = true;
const int   DEFAULT_SYS_PUB_INTERVAL_SEC  = 60;
const int   DEFAULT_STATUS_PUB_INTERVAL_SEC = 5;   // MQTT <base>/status publish interval (default)

// ================= CONFIG (FLASH) =================
String wifiSsid;
String wifiPass;
String deviceName;  // mDNS hostname (zonder .local)

// Meter keuze: "youless", "homewizard" of "smartgateways"
String meterType = "youless";
String meterIp;

// Modus:
ControlMode controlMode = MODE_NORMAL;

// NORMAL regel-algoritme + tuning
uint8_t normalAlg = DEFAULT_NORMAL_ALG;   // 0 DIRECT / 1 STEP
float   stepFactor = DEFAULT_STEP_FACTOR;
float   deadbandW  = DEFAULT_DEADBAND_W;
float   maxStepPctPerUpdatePerUpdate = DEFAULT_MAX_STEP_PCT;

// Regelparameters
float maxBoilerPowerW             = DEFAULT_MAX_BOILER_POWER_W;
float targetExportW               = DEFAULT_TARGET_EXPORT_W;
float minStepPercent              = DEFAULT_MIN_STEP_PERCENT;
unsigned long minUpdateIntervalMs = DEFAULT_MIN_INTERVAL;
float maxOutputPercentLimit       = DEFAULT_MAX_OUTPUT_PERCENT;

// ============== LEGIONELLA CONFIG ==============
bool  legScheduleEnabled          = DEFAULT_LEG_ENABLED;
int   legDow                      = DEFAULT_LEG_DOW;           // 0..6
int   legStartHour                = DEFAULT_LEG_HOUR;          // 0..23
int   legStartMinute              = DEFAULT_LEG_MINUTE;        // 0..59
int   legDurationMin              = DEFAULT_LEG_DURATION_MIN;  // 1..360
float legOutputLimitPercent       = DEFAULT_LEG_OUTPUT_LIMIT_PCT;

bool  legUseTempSensor            = DEFAULT_LEG_USE_TEMP_SENSOR;
float legTargetTempC              = DEFAULT_LEG_TARGET_TEMP_C;
int   legHoldMin                  = DEFAULT_LEG_HOLD_MIN;
bool  legRelayAutoEnabled         = DEFAULT_LEG_RELAY_AUTO;
int   legRelayStartDelayMin       = DEFAULT_LEG_RELAY_START_DELAY_MIN;  // 0..60
int   legRelayOffDelayMin         = DEFAULT_LEG_RELAY_DELAY_MIN;        // 0..60

// Legionella: welke DS18B20 (addrStr) gebruiken we als temperatuurbron?
// - Leeg = fallback naar CONTROL-sensor (ts_ctrl) voor backward compat.
String legTempAddrStr            = "";

// laatste run (voorkomt dubbel starten)
uint32_t legLastRunDateInt        = 0; // YYYYMMDD (local)
uint32_t legLastRunEpoch          = 0; // seconds (epoch)

// laatste succesvolle legionella-cyclus (target + hold behaald)
uint32_t legLastSuccessEpoch      = 0; // seconds (epoch)

// ============== LEGIONELLA RUNTIME ==============
bool     legRunning               = false;
bool     legHoldActive            = false;
time_t   legEndEpoch              = 0;
time_t   legHoldEndEpoch          = 0;
String   legReason                = "IDLE";

// ================= NORDPOOL (spotprijzen) ===================
// Publieke DayAheadPrices endpoint (geen API key) zoals Home Assistant gebruikt.
// We halen kale spotprijzen op (geen BTW/opslaan kosten).
// Opslag: ct/kWh (EUR/kWh * 100). Nordpool levert doorgaans EUR/MWh -> /1000 *100.
// Keuzes voor consument:
//   - delivery area (bijv. NL, BE, DE_LU, etc.)
//   - resolutie: 60 min (uur) of 15 min (kwartier)  [geen auto-keuze]

// Default instellingen
const bool   DEFAULT_NP_ENABLED   = true;
const char*  DEFAULT_NP_AREA      = "NL";
const int    DEFAULT_NP_RES_MIN   = 60;   // 60 = uurprijzen, 15 = kwartierprijzen

// Elektriciteitsprijs bron:
//  0 = Dynamisch (Nordpool) -> gebruikt np_* settings en /prices grafieken
//  1 = Vast (handmatig)     -> all-in prijs in ct/kWh (geen timing-optimalisatie)
const uint8_t DEFAULT_ELEC_PRICE_SOURCE = 0;
const float   DEFAULT_ELEC_FIXED_CT_KWH_ALLIN = 0.0f; // ct/kWh (all-in)


// --- Optioneel: belastingen/kosten toevoegen aan spotprijs (voor consumenten) ---
// Invoer is in ct/kWh (energiebelasting + inkoopkosten) en BTW in %.
// Totaal (ct/kWh) = (spot + energiebelasting + inkoopkosten) * (1 + btw/100)
const bool  DEFAULT_NP_INCL_TAX_ENABLED = false;
const float DEFAULT_NP_ENERGY_TAX_CT    = 0.0f;   // ct/kWh
const float DEFAULT_NP_INKOOP_CT        = 0.0f;   // ct/kWh
const float DEFAULT_NP_BTW_PCT          = 21.0f;  // %
// ================================================================================================
// GAS prijzen (EUR/m³) – laatste 15 dagen (incl. vandaag)
// - We gebruiken (standaard) de publieke EnergyZero endpoint zoals vaak gebruikt in HA/Node-RED.
// - "Spot" = kale marktprijs EXCL. BTW (VAT). "Totaal" = (spot + EB + inkoop) * (1 + btw/100).
// ================================================================================================
const bool  DEFAULT_GAS_TOTAL_ENABLED   = false; // default weergave in Prijzen-tab
// Bron voor gasprijs in de grafiek:
//  0 = Spot (API, excl. BTW)
//  1 = Vast (handmatig ingestelde prijs)
const uint8_t DEFAULT_GAS_PRICE_SOURCE = 0;
const float   DEFAULT_GAS_FIXED_EUR_M3 = 0.0f;  // EUR/m³
const float DEFAULT_GAS_ENERGY_TAX_EUR  = 0.0f;  // EUR/m³
const float DEFAULT_GAS_INKOOP_EUR      = 0.0f;  // EUR/m³
const float DEFAULT_GAS_BTW_PCT         = 21.0f; // %



// ================================================================================================
// Verwarmkeuze (Gas vs Elektra) – kostenvergelijking / advies
// ================================================================================================
const bool  DEFAULT_HEAT_AUTO_ENABLED              = false; // advies/vergelijking actief
const bool  DEFAULT_HEAT_EXEC_ELEC_WHEN_CHEAPER     = false; // als ELEC goedkoper: PWM actief aansturen
const bool  DEFAULT_HEAT_CHEAP_BLOCKS_ENABLED        = false; // alleen goedkoopste aaneengesloten ELEC-blokken uitvoeren
const bool  DEFAULT_HEAT_CHEAP_USE_BOILER_TARGET      = false; // boiler-doeltemperatuur begrenst AUTO_ELEC alleen wanneer expliciet ingeschakeld
const int   DEFAULT_HEAT_CHEAP_N_BLOCKS              = 4;     // maximum blokken per aaneengesloten ELEC-periode
const float DEFAULT_GAS_KWH_PER_M3                 = 9.77f;  // kWh per m³ (instelbaar)
const float DEFAULT_GAS_BOILER_EFF                 = 0.90f;  // rendement gasboiler (0..1)
const float DEFAULT_ELEC_BOILER_EFF                = 1.00f;  // (legacy) elektrisch rendement/COP (zie COP instelling)
const float DEFAULT_ELEC_MARGIN_CT                  = 0.00f;  // extra marge op elektriciteit in ct/kWh warmte (voor advies)
const int   DEFAULT_HEAT_MIN_SWITCH_MIN            = 30;     // minimaal minuten tussen advies-switch

static const int GAS_DAYS = 24;

// Settings (persisted)
bool  gasTotalEnabled = DEFAULT_GAS_TOTAL_ENABLED;
uint8_t gasPriceSource = DEFAULT_GAS_PRICE_SOURCE;
float gasFixedEurM3   = DEFAULT_GAS_FIXED_EUR_M3;
float gasEnergyTaxEur = DEFAULT_GAS_ENERGY_TAX_EUR;
float gasInkoopEur    = DEFAULT_GAS_INKOOP_EUR;
float gasBtwPct       = DEFAULT_GAS_BTW_PCT;



// Verwarmkeuze (Gas vs Elektra)
bool  heatAutoEnabled            = DEFAULT_HEAT_AUTO_ENABLED;
bool  heatExecElecWhenCheaper     = DEFAULT_HEAT_EXEC_ELEC_WHEN_CHEAPER;
bool  heatCheapBlocksEnabled       = DEFAULT_HEAT_CHEAP_BLOCKS_ENABLED;
bool  heatCheapUseBoilerTarget     = DEFAULT_HEAT_CHEAP_USE_BOILER_TARGET;
int   heatCheapNBlocks             = DEFAULT_HEAT_CHEAP_N_BLOCKS;
float gasKwhPerM3                = DEFAULT_GAS_KWH_PER_M3;
float gasBoilerEff               = DEFAULT_GAS_BOILER_EFF;   // 0..1
float elecBoilerEff              = DEFAULT_ELEC_BOILER_EFF;  // COP 0..10 (1.0 = element, >1.0 = warmtepomp)
float heatElecMarginCt           = DEFAULT_ELEC_MARGIN_CT;
int   heatMinSwitchMin           = DEFAULT_HEAT_MIN_SWITCH_MIN;

// Runtime (advies)
bool  heatRecommendGasNow         = false;
bool  heatPriceDecisionValidNow   = false;
float heatGasCtPerKwhHeatNow      = NAN;
float heatElecCtPerKwhHeatNow     = NAN;
unsigned long heatLastSwitchMs    = 0;

// Runtime: planning voor goedkoopste aaneengesloten ELEC-blokken.
bool   heatCheapPeriodLatched       = false;
bool   heatCheapPlanValid           = false;
String heatCheapPlanStartDate       = "";
String heatCheapPlanEndDate         = "";
int    heatCheapPlanStartIdx        = -1;
int    heatCheapPlanEndIdxExcl      = -1;
int    heatCheapPlanIntervalMin     = 0;
int    heatCheapPlanSelectedBlocks  = 0;

// Runtime cache
String gasDayIso[GAS_DAYS];      // YYYY-MM-DD (UTC date from API)
float  gasSpotEurM3[GAS_DAYS];   // EUR/m³ (spot, excl. BTW)
int    gasDayCount = 0;
String gasUpdatedAt = "";        // DD-MM-YYYY HH:MM (lokaal)



// --- Prijs-boost: PWM MAX op de meest goedkope/negatieve Nordpool blokken van VANDAAG ---
// Werkt naast zonne-overschot en handmatig force, maar met duidelijke prioriteit:
//  1) Legionella (altijd leidend)
//  2) Handmatig FORCE_HEAT (altijd leidend boven boost)
//  3) Prijs-boost (alleen als enabled en in geselecteerd blok)
//  4) Normale zonne-overschot regeling
//
// Instellingen:
//  - enable
//  - prijsbron: spot (0) of totaal incl. belastingen (1)
//  - drempel (ct/kWh): boost als prijs <= drempel
//  - N: aantal blokken vandaag op MAX (meest goedkoop/negatief)
const bool    DEFAULT_NP_BOOST_ENABLED      = false;
const uint8_t DEFAULT_NP_BOOST_PRICE_SOURCE = 0;      // 0=spot, 1=totaal (incl. belastingen)
const float   DEFAULT_NP_BOOST_THRESHOLD_CT = 0.0f;   // ct/kWh (0.0 = alleen <= 0)
const int     DEFAULT_NP_BOOST_N_BLOCKS     = 2;      // aantal blokken (uren bij 60min, kwartieren bij 15min)


// ============== Energy Strategy: SUN scheduling (stand-alone) =================
// Sun schedule modes for NORMAL (sun-overshoot) regulation:
//  0 = ALWAYS (huidig gedrag)
//  1 = MANUAL_TIME_WINDOW (start/eind)
//  2 = CHEAPEST_DAYLIGHT_BLOCKS (goedkoopste opeenvolgende blokken tussen zonsopgang/zonsondergang)
//  3 = OFF (zonneregeling uit; Force/Legionella/Prijs-Boost blijven werken)
const uint8_t DEFAULT_SUN_SCHED_MODE           = 0;    // ALWAYS
const int     DEFAULT_SUN_WIN_START_MIN        = 9*60; // 09:00
const int     DEFAULT_SUN_WIN_END_MIN          = 16*60;// 16:00
const int     DEFAULT_SUN_CHEAP_N_BLOCKS       = 4;    // opeenvolgende uren/kwartieren
const int     DEFAULT_SUN_DAYLIGHT_MARGIN_MIN  = 0;    // marge (minuten) na zonsopgang / voor zonsondergang

// Stand-alone locatie (default: regio Rotterdam / Rozenburg). Mag in Settings aangepast worden.
const float   DEFAULT_SUN_LAT                  = 51.90f;
const float   DEFAULT_SUN_LON                  = 4.40f;



// Elektriciteit prijsbron (persisted)
uint8_t elecPriceSource = DEFAULT_ELEC_PRICE_SOURCE;           // 0=dynamisch (Nordpool), 1=vast
float   elecFixedCtKwhAllIn = DEFAULT_ELEC_FIXED_CT_KWH_ALLIN; // ct/kWh (all-in)

bool npEnabled   = DEFAULT_NP_ENABLED;
String npArea      = DEFAULT_NP_AREA;
int    npResMin    = DEFAULT_NP_RES_MIN;  // 15 of 60

// Belasting/kosten (ct/kWh) + BTW (%)
bool  npInclTaxEnabled = DEFAULT_NP_INCL_TAX_ENABLED;
float npEnergyTaxCt    = DEFAULT_NP_ENERGY_TAX_CT;
float npInkoopCt       = DEFAULT_NP_INKOOP_CT;
float npBtwPct         = DEFAULT_NP_BTW_PCT;





// ============== Energy Strategy (stand-alone) =================
uint8_t sunSchedMode         = DEFAULT_SUN_SCHED_MODE;            // 0/1/2/3
int     sunWinStartMin       = DEFAULT_SUN_WIN_START_MIN;         // minuten vanaf 00:00
int     sunWinEndMin         = DEFAULT_SUN_WIN_END_MIN;           // minuten vanaf 00:00
int     sunCheapNBlocks      = DEFAULT_SUN_CHEAP_N_BLOCKS;        // opeenvolgende blokken
int     sunDaylightMarginMin = DEFAULT_SUN_DAYLIGHT_MARGIN_MIN;   // marge na zonsopgang/voor zonsondergang
float   sunLat               = DEFAULT_SUN_LAT;                   // stand-alone locatie
float   sunLon               = DEFAULT_SUN_LON;

bool    dashShowSunWindowLabel = true;  // Toon Daylight/Cheapest label op Dashboard

// ================= UI / Theme =====================
// Optie A: Licht/Donker modus binnen hetzelfde Solar Controller thema.
// Wordt opgeslagen in NVS (Preferences) zodat het ook na reboot en op andere devices consistent is.
bool    uiDarkMode = false;

// Runtime: berekende daylight + gekozen goedkoopste window (vandaag)
String  sunWinDate           = "";
int     sunDaylightStartMin  = -1;
int     sunDaylightEndMin    = -1;
int     sunCheapestStartMin  = -1;
int     sunCheapestEndMin    = -1;

// Runtime prijsbuffers (max 96 kwartieren)
static const int NP_MAX_POINTS = 96;


// Prijs-boost settings
bool    npBoostEnabled      = DEFAULT_NP_BOOST_ENABLED;
uint8_t npBoostPriceSource  = DEFAULT_NP_BOOST_PRICE_SOURCE;   // 0=spot, 1=totaal
float   npBoostThresholdCt  = DEFAULT_NP_BOOST_THRESHOLD_CT;   // ct/kWh
int     npBoostNBlocks      = DEFAULT_NP_BOOST_N_BLOCKS;

// Runtime: geselecteerde boost blokken (indices in npTodayCt[])
int   npBoostIdx[NP_MAX_POINTS];
int   npBoostSelectedN = 0;   // hoeveel blokken geselecteerd (<= npBoostNBlocks)
int   npBoostEligibleN = 0;   // hoeveel blokken voldeden aan drempel
int   npBoostNegativeN = 0;   // hoeveel blokken vandaag < 0 (gekozen prijsbron)
bool  npBoostActiveNow = false;

float npTodayCt[NP_MAX_POINTS];
float npTomorrowCt[NP_MAX_POINTS];
int   npTodayN = 0;
int   npTomorrowN = 0;
int   npIntervalMinToday = 0;
int   npIntervalMinTomorrow = 0;
String npDateToday = "";
String npDateTomorrow = "";
String npUpdatedAt = "";
unsigned long npLastPollMs = 0;          // scheduling tick
unsigned long npLastFetchMs = 0;         // rate limit
const unsigned long NP_MIN_FETCH_INTERVAL_MS = 15UL * 1000UL; // 15s (UI-clicks zonder irritatie, nog steeds anti-spam)

// debug/status
bool   npLastOkToday = false;
bool   npLastOkTomorrow = false;
int    npLastHttp = 0;
String npLastError = "";

// ================= MQTT CONFIG ====================
bool   mqttEnabled   = false;
String mqttHost;
uint16_t mqttPort    = 1883;
String mqttUser;
String mqttPassStr;
String mqttBaseTopic = DEVICE_ID;

// HA discovery toggle
bool   haDiscoveryEnabled = false;

// System publish config
bool   sysPublishEnabled  = DEFAULT_SYS_PUB_ENABLED;
int    sysPublishIntervalSec = DEFAULT_SYS_PUB_INTERVAL_SEC;
unsigned long lastSysPublishMs = 0;
int    statusPublishIntervalSec = DEFAULT_STATUS_PUB_INTERVAL_SEC;
unsigned long lastStatusPublishMs = 0;
unsigned long lastStatusActualPublishMs = 0;  // heartbeat: last time we actually published <base>/status


// MQTT intern
WiFiClient   mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);
unsigned long lastMqttReconnectAttempt = 0;
extern const unsigned long MQTT_RECONNECT_INTERVAL_MS = 10000;

// ================= RUNTIME ========================
unsigned long lastPollMs      = 0;
unsigned long lastUpdateMs    = 0;
float         lastDutyPercent = 0.0f;

long  lastNetPowerW  = 0;
float lastOvershootW = 0.0f;   // compat: max(0, errorW)
float lastErrorW     = 0.0f;   // NIEUW: signed error (target - net)
bool  lastMeterOk    = false;

// (step80) Fase-stroom (A) via P1 meter (Youless/HomeWizard/Smart Gateways) — alleen voor dashboard-weergave.
// Wordt ALLEEN gevuld als de geselecteerde meterbron een P1-meter is en de API de waarden levert.
float lastPhaseI1A = NAN;
float lastPhaseI2A = NAN;
float lastPhaseI3A = NAN;
bool  lastPhasesOk = false;
unsigned long lastPhasesUpdateMs = 0;   // millis() van laatste succesvolle fase-uitlezing
extern const unsigned long PHASES_STALE_MS = 15000; // na 15s zonder geldige update: phases_ok=false

// =====================================================================================
// Fase-beveiliging (stap 3: alleen beslislogica, GEEN PWM-ingreep)
//
// Doel:
//  - Tijdens Forceer verwarmen, Legionella of Price Boost voorkomen dat de
//    geselecteerde fase boven een ingestelde max-ampère uitkomt.
//  - In stap 3 berekenen we alleen de status/flag ("zou moeten limiteren").
//    De daadwerkelijke PWM-terugregeling komt pas in stap 4.
//
// Instellingen (stap 1):
//  - phaseGuardEnabled: aan/uit
//  - phaseGuardPhase:  1/2/3 (L1/L2/L3)
//  - phaseGuardMaxA:   max ampère (default 23A)
// =====================================================================================
static const bool  DEFAULT_PHASE_GUARD_ENABLED = false;
static const uint8_t DEFAULT_PHASE_GUARD_PHASE = 1;     // 1=L1,2=L2,3=L3
static const float DEFAULT_PHASE_GUARD_MAX_A   = 23.0f;  // standaard 23A

bool   phaseGuardEnabled = DEFAULT_PHASE_GUARD_ENABLED;
uint8_t phaseGuardPhase  = DEFAULT_PHASE_GUARD_PHASE;
float  phaseGuardMaxA    = DEFAULT_PHASE_GUARD_MAX_A;    // legacy / geselecteerde fase
float  phaseGuardMaxA_L1 = DEFAULT_PHASE_GUARD_MAX_A;
float  phaseGuardMaxA_L2 = DEFAULT_PHASE_GUARD_MAX_A;
float  phaseGuardMaxA_L3 = DEFAULT_PHASE_GUARD_MAX_A;

// Runtime (stap 3): alleen beslisresultaat voor UI/MQTT/debug.
bool   phaseGuardModeActive = false;  // Force/Leg/Boost active (prioriteitsmodi)
bool   phaseGuardWouldLimit = false;  // true => in stap 4 gaan we PWM terugregelen
float  phaseGuardSelectedA  = NAN;    // actuele A op gekozen fase (L1/L2/L3)
float  phaseGuardOverA      = 0.0f;

// =====================================================================================
// Soft-start (zacht opstarten) voor prioriteitsmodi (Price Boost / Legionella / Force Heat)
//
// Doel:
//  - Voorkom een plotselinge PWM-sprong naar MAX bij het inschakelen van prioriteitsmodi,
//    zodat er geen korte stroompiek ontstaat op de gekozen fase.
//  - Phase Guard blijft de harde veiligheidslaag (indien ingeschakeld); soft-start is extra “comfort/veiligheid”.
//
// Stap 90:
//  - Vaste defaults (geen UI-instellingen in deze stap).
// =====================================================================================
static const bool     DEFAULT_SOFTSTART_ENABLED       = true;
static const float    DEFAULT_SOFTSTART_START_PCT     = 10.0f;  // start op 10% PWM
static const uint16_t DEFAULT_SOFTSTART_DURATION_SEC  = 10;     // ramp naar target in 10s

bool     softStartEnabled      = DEFAULT_SOFTSTART_ENABLED;
float    softStartStartPct     = DEFAULT_SOFTSTART_START_PCT;
uint16_t softStartDurationSec  = DEFAULT_SOFTSTART_DURATION_SEC;

// Runtime (intern)
static bool     softStartInProgress = false;
static uint32_t softStartBeginMs    = 0;
static float    softStartBeginPct   = 0.0f;

static float applyPrioritySoftStartWithStart(float requestedPct, bool prioActive, bool gateActive, float startPct, float currentAppliedPct, bool syncToCurrentApplied) {
  if (!softStartEnabled) return requestedPct;

  // Alleen tijdens prioriteitsmodi (Force/Leg/Boost)
  if (!prioActive) {
    softStartInProgress = false;
    return requestedPct;
  }

  // Als PWM door boot-lock/config/data-gate geblokkeerd is, starten we de ramp nog niet.
  if (gateActive) {
    softStartInProgress = false;
    return requestedPct;
  }

  // Niets te doen bij 0% (of als target lager is dan start)
  if (requestedPct <= 0.0f) {
    softStartInProgress = false;
    return requestedPct;
  }

  if (!isfinite(currentAppliedPct) || currentAppliedPct < 0.0f) currentAppliedPct = 0.0f;
  if (currentAppliedPct > requestedPct) currentAppliedPct = requestedPct;

  const uint32_t now = millis();

  float anchorPct = startPct;
  if (syncToCurrentApplied && currentAppliedPct > anchorPct) {
    anchorPct = currentAppliedPct;
  }
  if (anchorPct > requestedPct) anchorPct = requestedPct;
  if (anchorPct < 0.0f) anchorPct = 0.0f;

  // Start ramp bij eerste geldige poging binnen prioriteitsmodus
  if (!softStartInProgress) {
    softStartInProgress = true;
    softStartBeginMs  = now;
    softStartBeginPct = anchorPct;
    return softStartBeginPct;
  }

  // Ramp progress
  uint32_t durMs = (uint32_t)softStartDurationSec * 1000UL;
  if (durMs == 0) return requestedPct; // 0s = direct (compat)

  float p = (float)(now - softStartBeginMs) / (float)durMs;
  if (p >= 1.0f) return requestedPct;
  if (p < 0.0f) p = 0.0f;

  float cap = softStartBeginPct + (requestedPct - softStartBeginPct) * p;

  // Step 265:
  // In standalone prio/force/boost moet soft-start opnieuw verankeren op de
  // laatst veilige toegepaste PWM zodra phase/loadbalancing het target eerder
  // heeft begrensd. Zo loopt de interne ramp niet door vanaf een oude lagere
  // startwaarde terwijl de echte uitgang al op een andere veilige waarde staat.
  if (syncToCurrentApplied && currentAppliedPct > (cap + 0.25f)) {
    softStartBeginMs  = now;
    softStartBeginPct = currentAppliedPct;
    cap = softStartBeginPct;
  }

  // Cap nooit boven target
  if (cap > requestedPct) cap = requestedPct;
  if (cap < 0.0f) cap = 0.0f;
  return cap;
}



float appGetPhaseCurrentA(uint8_t phase123) { return getPhaseCurrentA(phase123); }
float appApplyPrioritySoftStartWithStart(float requestedPct, bool prioActive, bool gateActive, float startPct, float currentAppliedPct, bool syncToCurrentApplied) {
  return applyPrioritySoftStartWithStart(requestedPct, prioActive, gateActive, startPct, currentAppliedPct, syncToCurrentApplied);
}
bool appPriorityHeatingActiveNow() { return priorityHeatingActiveNow(); }
uint8_t appMultiControllerCurrentAppScenario() { return multiControllerCurrentAppScenario(); }



// =====================================================================================
// (step92) Hard cap op PWM tijdens prioriteitsmodi op basis van fase-max-A (indien fasebewaking is ingeschakeld)
//
// Probleem:
//   - In prioriteitsmodi (Force/Leg/Boost) ging PWM eerst (soft-start) oplopen en werd pas daarna teruggeregeld
//     wanneer de gekozen fase boven de ingestelde max-A uitkwam.
//
// Oplossing:
//   - Maak de fase-limiet een harde bovengrens voor de gewenste PWM (requestedPct), nog vóór soft-start.
//   - Daardoor kan PWM nooit tijdelijk boven de fase-limiet uitkomen (ook niet kortstondig).
//
// Werking:
//   - We schatten de 'huishoudstroom' op de gekozen fase door de huidige boilerstroom (afgeleid uit lastDutyPercent)
//     af te trekken van de gemeten fase-stroom.
//   - Vervolgens berekenen we hoeveel boiler-PWM nog "past" binnen phaseGuardMaxA.
//
// Let op:
//   - Deze cap geldt alleen wanneer fasebewaking aan staat én we in een prioriteitsmodus zitten.
//   - Phase Guard stap 4 blijft bestaan als extra vangnet voor meetvertragingen/afwijkingen.
// =====================================================================================


// =====================================================================================
// PWM boot-safety lockout (v2026.02.09.07)
// Tijdens opstarten is (meter/prijs/config) vaak nog niet 'ready'.
// Om spikes (100% -> 0% -> 100%) te voorkomen forceren we PWM=0 totdat:
//  - minimaal PWM_BOOT_LOCK_MS verstreken is sinds boot, én
//  - we minstens één geldige meter-sample hebben gezien (lastMeterOk==true).
// Dit is bewust laag in de aansturing geplaatst (pwmWriteDuty), zodat GEEN enkele
// modus (zon/force/legionella/elec-uitvoeren) per ongeluk toch output kan geven.
// =====================================================================================
extern const uint32_t PWM_BOOT_LOCK_MS = 30000; // 30s
uint32_t pwmBootStartMs = 0;
bool pwmSeenValidMeterSample = false;


bool  configMode     = false;

WebServer   server(80);
Preferences prefs;
// ==================================================
// Firmware update / OTA runtime
// ==================================================
// Auto-update preference stays shared here because config/API persistence touches it.
bool fwAutoUpdateEnabled = false;

// Most implementation now lives in src modules; this file keeps global state plus setup/loop integration.
// MQTT callback/connect lives in src/mqtt.cpp; this wrapper preserves existing callers.
bool mqttEnsureConnected() {
  return MQTT::ensureConnected();
}

// Page handlers implemented in src route/page modules.
void handleDashboard();
void handleControlPage();
void handleLegionellaPage();
void handleControlSaveTemperatureGate();
void handleSettingsPage();
void handleSettingsSaveSensors();
void handleSettingsSaveUi();

void setup() {
  // PWM HARD-SAFE start (v2026.02.09.07)
  // Zorg dat de PWM pin nooit 'floating' hoog kan zijn tijdens de eerste millis.
  // Dit voorkomt een 100% spike op Kemo M240 bij reboot/OTA voordat PWM init klaar is.
  pinMode(PWM_PIN, OUTPUT);
  digitalWrite(PWM_PIN, LOW);
#ifdef SOC_GPIO_SUPPORT_RTC_INDEPENDENT
  // niets
#endif
  // Interne pulldown als extra veiligheid (werkt op meeste ESP32 varianten)
  gpio_pulldown_en((gpio_num_t)PWM_PIN);

  // Multi Controller stap 3: zet ook de extra toegestane PWM-pinnen meteen hard-safe laag.
  multiControllerPrepareSafePins();

  // Relais: lees de minimale config vroeg in en zet GPIO18 meteen in een veilige/gevraagde opstartstand.
  relayEarlyBootInit();

  // PWM boot-safety: start lock timer and clear 'data ready' flags
  pwmBootStartMs = millis();
  pwmSeenValidMeterSample = false;

  Serial.begin(115200);
  delay(800);
  Serial.println();
  Serial.println(F("=== Solar Controller boot ==="));
  Serial.print(F("FW: ")); Serial.println(FW_VERSION);
  Serial.print(F("DS18B20 pin: ")); Serial.println(DS18B20_PIN);
  Serial.print(F("Temp poll interval ms: ")); Serial.println(TEMP_POLL_INTERVAL_MS);
  Serial.println(F("============================"));


  loadConfig();
  relayApplyRuntimeState();

  pwmInit();
  pwmWriteDuty(0);

  // MQTT buffer vergroten (nodig voor HA Discovery + JSON status/system)
  #if USE_PUBSUBCLIENT_SETBUFFERSIZE
    mqttClient.setBufferSize(MQTT_CLIENT_BUFFER_SIZE);
  #endif

  // step308: registreer WiFi-events vóór de eerste STA-verbinding.
  // Alleen diagnose + runtime reconnect; config-AP gedrag blijft ongewijzigd.
  wifiRecoverySetup();

  bool staOk = connectWiFiSTA(wifiSsid, wifiPass);
  if (!staOk) {
    startConfigAP(configMode);
  } else {
    configMode = false;
    setupTimeNtp();
    collectorSetup();

    // step309: bestaande .local-hostname + Solar Controller service voor Homey discovery.
    startMdnsHttp(deviceName);
  }

  ds18b20.begin();
  // DS18B20: use slightly lower resolution for better reliability on longer cables (waterproof probes).
  // 10-bit => ~187ms conversion time, more margin on noisy 1-wire buses.
  ds18b20.setResolution(10);
  // DS18B20: force blocking conversion to avoid reading the 85°C power-up value.
  // (We keep this explicit for robustness across library defaults and future changes.)
  ds18b20.setWaitForConversion(true);
  ds18b20.setCheckForConversion(true);
  // Multi DS18B20: scan + apply saved names/enabled/control
  scanTempSensors();
  controllerTemperatureGateRefresh();

  // Routes
  registerWebRoutes();

  registerHealthRoutes(server);
  registerNetworkRoutes(server);
  server.on("/api/prices", HTTP_GET, handlePricesJson);
  server.on("/api/gas_history", HTTP_GET, handleGasHistoryJson);
  server.on("/api/heat_compare", HTTP_GET, handleHeatCompareJson);

  // ================= REST API (write) =================
  // API-first endpoints so external controllers (Homey/HA/scripts) can fully control and configure the device
  // without relying on UI form posts or toggle-only actions.
  server.on("/api/config", HTTP_GET, handleApiConfigGet);
  server.on("/api/config", HTTP_POST, handleApiConfigPost);

  server.on("/api/force_heat", HTTP_POST, handleApiForceHeatPost);
  server.on("/api/relay", HTTP_POST, handleApiRelayPost);
  server.on("/api/relay/on", HTTP_POST, handleApiRelayOnPost);
  server.on("/api/relay/off", HTTP_POST, handleApiRelayOffPost);

  server.on("/api/legionella/run_now", HTTP_POST, handleApiLegionellaRunNowPost);
  server.on("/api/legionella/cancel",  HTTP_POST, handleApiLegionellaCancelPost);

  server.on("/api/reboot", HTTP_POST, handleApiRebootPost);


  
  server.on("/api/mc/targets", HTTP_POST, handleApiMultiControllerTargetsPost);
  registerFirmwareUpdateRoutes(server);
  server.begin();

  // MQTT init
  if (!configMode && mqttEnabled && mqttHost.length() > 0) {
    mqttEnsureConnected();
  }
}

void loop() {
  // step308: niet-blokkerend runtime WiFi-herstel. Moet vóór configMode-return lopen.
  wifiRecoveryTick();

  server.handleClient();

  
  // DS18B20: keep temperature updated for UI (not only during Legionella)
  updateTempIfNeeded();
  controllerTemperatureGateRefresh();
// GitHub OTA (pull) update: start pending update immediately (UI/auto)
  processFirmwareUpdateTask();
  // Stap 7 versnellen: laat de webserver ook rondom de Multi Controller-pass
  // vaker lucht krijgen, zodat de UI/externe bereikbaarheid minder stroperig
  // aanvoelt terwijl peers of planners actief zijn.
  server.handleClient();
  multiControllerLoop();
  server.handleClient();

  if (configMode) return;

  // Collector telemetry (step 126): send 1 boot-ping (non-blocking, max 3s)
  collectorLoop();

  if (mqttEnabled) {
    if (mqttEnsureConnected()) mqttClient.loop();
  }

  nordpoolTick();
  gasTick();

  // Houd de webserver ook na overige taken vlot bedienbaar.
  server.handleClient();

  static unsigned long lastSchedMs = 0;
  unsigned long nowMs = millis();
  if (nowMs - lastSchedMs >= SCHED_CHECK_MS) {
    lastSchedMs = nowMs;
    legSchedulerTick();
  

  firmwareUpdateAutoCheckTick();

}

  if (mqttEnabled && mqttClient.connected()) {
    mqttPublishSystemJson(false);
  }

  if (nowMs - lastPollMs >= POLL_INTERVAL_MS) {
    lastPollMs = nowMs;

    // Stap 1 fase/loadbalancing-basis:
    // ververs de actuele fasewaarden vóór de regelbeslissingen,
    // zodat dezelfde poll al met verse fase-info werkt.
    phaseBudgetRefreshSnapshot();

    static uint8_t lastPriorityMode = PMODE_NONE;
    const uint8_t activePriorityMode = currentPriorityModeNow();
    if (lastPriorityMode != PMODE_NONE && activePriorityMode == PMODE_NONE) {
      // Bij het verlaten van een prioriteitsmodus (Force / Legionella / Price Boost / AUTO_ELEC)
      // moet PWM direct naar 0%, net als de gebruiker verwacht. Daarna mag NORMAL pas in de
      // volgende poll weer oppakken. Zo voorkomen we traag uitlopen via de normale regeling.
      multiControllerSetAppScenario(MC_SCENARIO_IDLE);
      npBoostActiveNow = false;
      setBoilerDutyPercent(0.0f);
      lastPriorityMode = activePriorityMode;
    } else {
      lastPriorityMode = activePriorityMode;

    // 1) Legionella heeft voorrang
    if (legRunning) {
      npBoostActiveNow = false;
      multiControllerSetAppScenario(MC_SCENARIO_PRIORITY_HEAT);
      legTick();
      // netPower blijft wel handig om te tonen
      long np=0; bool ok = readMeterPower(np);
      lastMeterOk = ok;
      if (ok) {
        lastNetPowerW = np;
        pwmSeenValidMeterSample = true; // data-ready gate for PWM boot lockout
      }
    }
    // 2) Force heat heeft voorrang boven alles behalve legionella
    else if (controlMode == MODE_FORCE_HEAT) {
      npBoostActiveNow = false;
      multiControllerSetAppScenario(MC_SCENARIO_MANUAL_FORCE);
      long np=0;
      bool ok = readMeterPower(np);
      lastMeterOk = ok;
      if (ok) {
        lastNetPowerW = np;
        pwmSeenValidMeterSample = true; // data-ready gate for PWM boot lockout
      }
      applyForceHeatMode();
    }
    // 3) Prijs-boost: PWM MAX op geselecteerde goedkope/negatieve Nordpool blokken
    else if (npIsBoostActiveNow()) {
      npBoostActiveNow = true;
      multiControllerSetAppScenario((!isnan(elecCtKwhNow()) && elecCtKwhNow() < 0.0f) ? MC_SCENARIO_NEGATIVE : MC_SCENARIO_CHEAP);
      long np=0;
      bool ok = readMeterPower(np);
      lastMeterOk = ok;
      if (ok) {
        lastNetPowerW = np;
        pwmSeenValidMeterSample = true; // data-ready gate for PWM boot lockout
      }
      applyPriceBoostMode();
    }
    // 4) NORMAL regeling (DIRECT of STEP)
    else {
      npBoostActiveNow = false;
// Energy Strategy: beperk NORMAL (zonne-overschot) regeling via schema
// Force/Legionella/Prijs-boost zitten hoger in de prioriteit en omzeilen dit.

// PRIORITEIT: ELEC-advies uitvoeren (zoals Force/Legionella)
// Als ELEC aanbevolen is en "uitvoeren" aan staat, dan sturen we PWM actief,
// en omzeilen we NORMAL-schema blokkades (anders krijg je 100% -> 0% -> 100%...).
if (heatExecElecActiveNow()) {
  multiControllerSetAppScenario(MC_SCENARIO_PRIORITY_HEAT);
  // Voor status: meter blijven lezen zodat netvermogen/OK up-to-date blijft
  long netPowerW = 0;
  bool ok = readMeterPower(netPowerW);
  lastMeterOk = ok;
  if (ok) {
        lastNetPowerW = netPowerW;
        pwmSeenValidMeterSample = true; // data-ready gate for PWM boot lockout
      }

  // We voeren ELEC uit: met Multi Controller aan gelden de per-controller maxima;
  // met Multi Controller uit blijft het oude single-controller gedrag gelden.
  lastErrorW = 0.0f;
  lastOvershootW = 0.0f;
  if (multiControllerShouldUseGroupControlForScenario(MC_SCENARIO_PRIORITY_HEAT)) {
    setBoilerDutyPercentForScenario(multiControllerGroupTargetMaxForScenario(MC_SCENARIO_PRIORITY_HEAT), MC_SCENARIO_PRIORITY_HEAT);
  } else {
    setBoilerDutyPercent(maxOutputPercentLimit);
  }
} else if (!appNormalAllowedByScheduleNow()) {
  multiControllerSetAppScenario(MC_SCENARIO_IDLE);
  long netPowerW = 0;
  bool ok = readMeterPower(netPowerW);
  lastMeterOk = ok;
  if (ok) {
        lastNetPowerW = netPowerW;
        pwmSeenValidMeterSample = true; // data-ready gate for PWM boot lockout
      }
      lastErrorW = 0.0f;
  lastOvershootW = 0.0f;
  setBoilerDutyPercent(0.0f);
} else {

      long netPowerW = 0;
      bool ok = readMeterPower(netPowerW);
      lastMeterOk   = ok;

      if (!ok) {
        lastNetPowerW = 0;
        lastErrorW = 0;
        lastOvershootW = 0.0f;
        setBoilerDutyPercent(0.0f);
      } else {
        lastNetPowerW = netPowerW;
        pwmSeenValidMeterSample = true; // data-ready gate for PWM boot lockout
        multiControllerSetAppScenario(MC_SCENARIO_SURPLUS);
        applyNormalMode(netPowerW);
      }
      }
    }
    }

    if (legUseTempSensor && (!ds18Available || tempSensorCount == 0)) {
      // Sensor kan later ingeplugd worden -> opnieuw scannen
      scanTempSensors();
    }

    if (mqttEnabled && mqttClient.connected()) {
      // Retained state topics: publish only on change (functions have internal change-detect)
      mqttPublishForceState();
      mqttPublishRelayState();
      mqttPublishMultiControllerStates();
      mqttPublishLegionellaConfig();
      mqttPublishLegionellaState();
      mqttPublishTuningStates();
      mqttPublishLimitsStates();
      mqttPublishNormalConfigStates();
      mqttPublishLegionellaPlanStates();
      mqttPublishCoreLiveStates(false);

      // Status JSON: interval-based (default 5s, configurable in Settings)
      mqttPublishStatusJson(false);
    }
  }
}
