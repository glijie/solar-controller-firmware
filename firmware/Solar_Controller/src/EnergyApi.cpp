#include "EnergyApi.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <time.h>

#include "app_globals.h"
#include "nordpool.h"
#include "ScenarioLogic.h"
#include "TimeSchedule.h"

extern WebServer server;

extern uint8_t gasPriceSource;
extern float gasFixedEurM3;
extern float gasEnergyTaxEur;
extern float gasInkoopEur;
extern float gasBtwPct;
extern String gasDayIso[];
extern float gasSpotEurM3[];
extern int gasDayCount;
extern String gasUpdatedAt;
extern uint8_t elecPriceSource;
extern float elecFixedCtKwhAllIn;

static const int GAS_DAYS = 24;

// Gasprijs-herstel:
// - direct proberen zodra wifi + tijd beschikbaar zijn;
// - na een mislukte poging iedere 60 seconden opnieuw proberen;
// - pas na een geslaagde download terug naar het normale 3-uursinterval.
static unsigned long gasLastAttemptMs = 0;
static bool gasLastAttemptSucceeded = false;
static bool gasFetchPrerequisitesWereReady = false;

void gasTick(){
  if (configMode) return;
  if (gasPriceSource != 0) return; // vaste gasprijs heeft geen internetdownload nodig

  const bool prerequisitesReady = WiFi.isConnected() && timeIsValid();
  if (!prerequisitesReady) {
    gasFetchPrerequisitesWereReady = false;
    return;
  }

  // Wifi/tijd is opnieuw beschikbaar: meteen een nieuwe poging toestaan.
  if (!gasFetchPrerequisitesWereReady) {
    gasFetchPrerequisitesWereReady = true;
    gasLastAttemptMs = 0;
    gasLastAttemptSucceeded = false;
  }

  const unsigned long ms = millis();
  const unsigned long retryMs = gasLastAttemptSucceeded
      ? 3UL * 60UL * 60UL * 1000UL
      : 60UL * 1000UL;

  if (gasLastAttemptMs == 0 || (ms - gasLastAttemptMs) >= retryMs) {
    gasLastAttemptMs = ms;
    gasLastAttemptSucceeded = gasFetch15Days();
  }
}

// JSON API voor web UI
void handlePricesJson(){
  StaticJsonDocument<4096> doc;

  doc["enabled"] = npEnabled;
  doc["area"] = npArea;
  doc["want_res_min"] = npResMin;

  // Belasting/kosten mode (ct/kWh)
  doc["incl_tax_enabled"] = npInclTaxEnabled;
  doc["energy_tax_ct"]    = npEnergyTaxCt;
  doc["inkoop_ct"]        = npInkoopCt;
  doc["btw_pct"]          = npBtwPct;
  doc["price_mode"]       = npInclTaxEnabled ? "incl" : "spot";
  doc["today_date"] = npDateToday;
  doc["today_interval_min"] = npIntervalMinToday;
  doc["today_points"] = npTodayN;

  doc["tomorrow_date"] = npDateTomorrow;
  doc["tomorrow_interval_min"] = npIntervalMinTomorrow;
  doc["tomorrow_points"] = npTomorrowN;

  doc["updatedAt"] = npUpdatedAt;

  // Web-friendly velden
  doc["last_update"] = npUpdatedAt;  // DD-MM-YYYY HH:MM
  doc["today_date_dmy"] = fmtDateDMYFromIso(npDateToday);
  doc["tomorrow_date_dmy"] = fmtDateDMYFromIso(npDateTomorrow);

  // Status tekst (consument-proof)
  String st;
  if (!npEnabled) st = "Nordpool uit";
  else if (npLastError == "rate_limit") st = "Wacht op prijzen…";
  else if (npTodayN == 0) st = "Wacht op prijzen…";
  else if (npLastError.length() > 0) st = "Fout bij ophalen";
  else st = "OK";
  doc["status_text"] = st;

  doc["last_http"] = npLastHttp;
  doc["last_error"] = npLastError;

  JsonArray aT = doc.createNestedArray("today_ct_kwh");
  for (int i=0;i<npTodayN;i++) aT.add(npEffectiveCtKwh(npTodayCt[i]));

  JsonArray aM = doc.createNestedArray("tomorrow_ct_kwh");
  for (int i=0;i<npTomorrowN;i++) aM.add(npEffectiveCtKwh(npTomorrowCt[i]));

  String out;
  out.reserve(3600);
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "application/json", out);
}

// ================================================================================================
// GAS: fetch & JSON API for web UI
// ================================================================================================
// Public endpoint often used for EnergyZero / ANWB / Mijndomein etc.
// NOTE: We request usageType=3 (gas) and inclBtw=false (spot, excl. BTW).
static const char* GAS_API_URL = "https://api.energyzero.nl/v1/energyprices";


// Minimal URL encoding (space, :, +, ?, &, =, %, /)
String urlEncode(const String& s){
  const char* hex = "0123456789ABCDEF";
  String out; out.reserve(s.length()*3);
  for (size_t i=0;i<s.length();i++){
    char c = s[i];
    if (('a'<=c && c<='z') || ('A'<=c && c<='Z') || ('0'<=c && c<='9') || c=='-' || c=='_' || c=='.' || c=='~'){
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

// Build ISO timestamp like 2025-12-29T00:00:00.000Z (UTC)
String isoUtcMidnight(time_t tUtc){
  struct tm tmUtc;
  gmtime_r(&tUtc, &tmUtc);
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT00:00:00.000Z",
           tmUtc.tm_year + 1900, tmUtc.tm_mon + 1, tmUtc.tm_mday);
  return String(buf);
}

// Fetch last 15 days (incl. today) gas spot prices (EUR/m³, excl. BTW) into runtime cache
bool gasFetch15Days(){
  if (!WiFi.isConnected()) return false;

  time_t nowUtc = time(nullptr);
  if (nowUtc < 100000) return false; // time not set yet

  // from = today-14 00:00Z, till = tomorrow 00:00Z
  time_t fromUtc = nowUtc - (time_t)14 * 86400;
  time_t tillUtc = nowUtc + (time_t)1 * 86400;

  String fromIso = isoUtcMidnight(fromUtc);
  String tillIso = isoUtcMidnight(tillUtc);

  String maxDate = isoUtcMidnight(nowUtc + (time_t)1 * 86400).substring(0,10); // YYYY-MM-DD (tomorrow)

  String url = String(GAS_API_URL) +
    "?fromDate=" + urlEncode(fromIso) +
    "&tillDate=" + urlEncode(tillIso) +
    "&interval=4&usageType=3&inclBtw=false";

  WiFiClientSecure client;
  client.setInsecure(); // public endpoint; we skip cert pinning to keep UI snappy and maintenance-free

  HTTPClient http;
  http.begin(client, url);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) { http.end(); return false; }

  String payload = http.getString();
  http.end();

  // Note: response shape is typically { "Prices": [ { "price": 1.16, "readingDate": "2025-12-29T00:00:00Z" }, ... ] }
  DynamicJsonDocument doc(32768);
  auto err = deserializeJson(doc, payload);
  if (err) return false;

  JsonArray arr = doc["Prices"].as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) return false;

  // ---------------------------------------------------------------------------
  // Build cache from API
  //
  // Belangrijk:
  //  - We willen 1 bar per "gas-dag" (06:00 -> 06:00) gelabeld met de startdatum.
  //  - Sommige leveranciers/APIs rapporteren de prijs van gas-dag D op datum D+1 (UTC).
  //
  // Daarom:
  //  1) We verzamelen eerst de unieke API-datums (UTC) zonder shift.
  //  2) Daarna bepalen we automatisch of er een -1 dag shift nodig is,
  //     gebaseerd op of de cache de huidige gas-dag bevat.
  // ---------------------------------------------------------------------------

  String tmpIso[GAS_DAYS];
  float  tmpSpot[GAS_DAYS];
  int    tmpCount = 0;

  String lastApiDate = "";
  for (JsonObject o : arr) {
    if (!o.containsKey("readingDate") || !o.containsKey("price")) continue;
    String rd = o["readingDate"].as<String>();
    if (rd.length() < 10) continue;
    String dApi = rd.substring(0,10); // YYYY-MM-DD (UTC date from API)
    if (dApi > maxDate) continue;     // allow up to tomorrow (see maxDate)

    float p = o["price"].as<float>();

    // 1 prijs per dag (neem de eerste entry per dag)
    if (dApi != lastApiDate) {
      if (tmpCount < GAS_DAYS) {
        tmpIso[tmpCount]  = dApi;
        tmpSpot[tmpCount] = p;
        tmpCount++;
      } else {
        // shift left to keep last GAS_DAYS unique days
        for (int i=1;i<GAS_DAYS;i++){
          tmpIso[i-1]  = tmpIso[i];
          tmpSpot[i-1] = tmpSpot[i];
        }
        tmpIso[GAS_DAYS-1]  = dApi;
        tmpSpot[GAS_DAYS-1] = p;
      }
      lastApiDate = dApi;
    }
  }

  if (tmpCount <= 0) return false;

  // Determine if we need to shift API dates by -1 day.
  // We compare using UTC dates to avoid timezone/DST edge cases.
  // Idea: if the unshifted series doesn't contain today's gas-day label, but it *does*
  // contain tomorrow, then the provider is reporting gas-dag D on date D+1.
  time_t nowUtc2 = time(nullptr);
  String todayLabel = isoUtcMidnight(nowUtc2 - (time_t)GAS_DAY_START_HOUR * 3600).substring(0,10); // YYYY-MM-DD
  bool hasToday = false;
  bool hasTomorrow = false;
  String tomorrowLabel = isoDateAddDays(todayLabel, +1);

  for (int i=0;i<tmpCount;i++){
    // tmpIso is UTC date; compare directly against labels (YYYY-MM-DD)
    if (tmpIso[i] == todayLabel) hasToday = true;
    if (tmpIso[i] == tomorrowLabel) hasTomorrow = true;
  }

  int shiftDays = 0;
  if (!hasToday && hasTomorrow) {
    shiftDays = -1;
  }

  // Copy to runtime cache with optional shift
  gasDayCount = 0;
  for (int i=0;i<tmpCount;i++){
    String d = (shiftDays != 0) ? isoDateAddDays(tmpIso[i], shiftDays) : tmpIso[i];
    gasDayIso[gasDayCount]   = d;
    gasSpotEurM3[gasDayCount]= tmpSpot[i];
    gasDayCount++;
  }

  gasUpdatedAt = fmtTimeLocal(time(nullptr));
  return (gasDayCount > 0);
}

void handleGasHistoryJson(){
  StaticJsonDocument<4096> doc;

  doc["days"] = gasDayCount;
  doc["updatedAt"] = gasUpdatedAt;

  // Settings for client-side total calculation
  doc["total_default"] = gasTotalEnabled;
  doc["price_source"] = (int)gasPriceSource;  // 0=spot(API) 1=vast
  doc["fixed_eur_m3"] = gasFixedEurM3;
  doc["energy_tax_eur"] = gasEnergyTaxEur;
  doc["inkoop_eur"] = gasInkoopEur;
  doc["btw_pct"] = gasBtwPct;

  JsonArray dates = doc.createNestedArray("date");
  JsonArray spot  = doc.createNestedArray("spot_eur_m3");

  for(int i=0;i<gasDayCount;i++){
    dates.add(gasDayIso[i]);          // YYYY-MM-DD
    spot.add(gasSpotEurM3[i]);        // EUR/m³
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json; charset=utf-8", out);
}




// ================================================================================================
// Verwarmkeuze (Gas vs Elektra) – helpers + API
// ================================================================================================

// Converteer gas spot (EUR/m³) naar "totaal" (incl. EB + inkoop + BTW)
static float gasTotalEurM3FromSpot(float spotEurM3){
  float base = spotEurM3 + gasEnergyTaxEur + gasInkoopEur;
  return base * (1.0f + (gasBtwPct/100.0f));
}

// Geef gasprijs (EUR/m³) voor vandaag. Als niet beschikbaar, fallback naar laatste bekende.
float gasEurM3Now(bool total){
  // 1) Vast (handmatig)
  if (gasPriceSource == 1) {
    return gasFixedEurM3;
  }

  // 2) Spot uit cache (API)
  if (gasDayCount <= 0) return NAN;

  String today = gasDayIsoLocal(time(nullptr)); // YYYY-MM-DD lokaal
  float spot = NAN;
  for (int i=0;i<gasDayCount;i++){
    if (gasDayIso[i] == today) { spot = gasSpotEurM3[i]; break; }
  }
  if (isnan(spot)) {
    // Voor grafiek/weergave blijft de bestaande fallback naar de laatste entry behouden.
    spot = gasSpotEurM3[gasDayCount-1];
  }

  return total ? gasTotalEurM3FromSpot(spot) : spot;
}

// Strikte prijs voor automatische regeling. Geen actuele gasdag = geen geldig advies.
float gasEurM3ForControlNow(bool total){
  if (gasPriceSource == 1) {
    return (!isnan(gasFixedEurM3) && gasFixedEurM3 > 0.0f) ? gasFixedEurM3 : NAN;
  }

  if (!timeIsValid() || gasDayCount <= 0) return NAN;

  const String today = gasDayIsoLocal(time(nullptr));
  for (int i=0; i<gasDayCount; i++) {
    if (gasDayIso[i] == today && !isnan(gasSpotEurM3[i])) {
      return total ? gasTotalEurM3FromSpot(gasSpotEurM3[i]) : gasSpotEurM3[i];
    }
  }
  return NAN;
}

// Strikte gasprijs voor een toekomstig lokaal prijsblok.
// De gasdag loopt 06:00 -> 06:00, gelijk aan de bestaande actuele regeling.
float gasEurM3ForControlAtLocal(const String& localDate, int minuteOfDay, bool total){
  if (gasPriceSource == 1) {
    return (!isnan(gasFixedEurM3) && gasFixedEurM3 > 0.0f) ? gasFixedEurM3 : NAN;
  }

  if (gasDayCount <= 0 || localDate.length() < 10) return NAN;

  int m = minuteOfDay;
  if (m < 0) m = 0;
  if (m > 24 * 60) m = 24 * 60;
  const String gasDay = (m < 6 * 60) ? isoDateAddDays(localDate, -1) : localDate;

  for (int i=0; i<gasDayCount; i++) {
    if (gasDayIso[i] == gasDay && !isnan(gasSpotEurM3[i])) {
      return total ? gasTotalEurM3FromSpot(gasSpotEurM3[i]) : gasSpotEurM3[i];
    }
  }
  return NAN;
}

// Geef huidige elektra prijs (ct/kWh) op basis van Nordpool "nu" (incl tax toggle zoals UI)
float elecCtKwhNow(){
  // FIXED all-in price mode (no Nordpool required)
  if (elecPriceSource == 1) {
    if (elecFixedCtKwhAllIn <= 0.0f) return NAN;
    return elecFixedCtKwhAllIn; // ct/kWh (all-in)
  }

  if (!timeIsValid()) return NAN;
  if (npTodayN <= 0) return NAN;

  time_t now = time(nullptr);
  // Alleen als we "vandaag" geladen hebben
  if (isoDateLocal(now) != npDateToday) return NAN;

  int interval = (npIntervalMinToday > 0) ? npIntervalMinToday : npResMin;
  if (interval <= 0) interval = 60;

  struct tm tmNow;
  localtime_r(&now, &tmNow);
  int minutes = tmNow.tm_hour * 60 + tmNow.tm_min;
  int idx = minutes / interval;
  if (idx < 0 || idx >= npTodayN) return NAN;

  float spot = npTodayCt[idx];
  return (npInclTaxEnabled) ? npTotalCtKwh(spot) : spot;
}

// API: current comparison
void handleHeatCompareJson(){
  heatCompareUpdateNow();

  DynamicJsonDocument doc(2048);
  doc["enabled"] = heatAutoEnabled;
  doc["decision_valid"] = heatPriceDecisionValidNow;
  doc["recommend"] = heatRecommendationLabelNow();
  doc["gas_ct_per_kwh_heat"]  = heatGasCtPerKwhHeatNow;
  doc["elec_ct_per_kwh_heat"] = heatElecCtPerKwhHeatNow;
  doc["elec_ct_kwh_now"] = elecCtKwhNow();
  doc["gas_eur_m3_now"]  = gasEurM3Now(gasTotalEnabled);
  doc["gas_source"] = gasPriceSource; // 0 spot, 1 fixed
  doc["gas_total_enabled"] = gasTotalEnabled;
  doc["cheap_blocks_enabled"] = heatCheapBlocksEnabled;
  doc["cheap_use_boiler_target"] = heatCheapUseBoilerTarget;
  doc["cheap_blocks_max"] = heatCheapNBlocks;
  doc["cheap_plan_valid"] = heatCheapPlanValid;
  doc["cheap_plan_selected"] = heatCheapPlanSelectedBlocks;
  doc["cheap_plan_interval_min"] = heatCheapPlanIntervalMin;
  doc["cheap_temp_sensor_assigned"] = heatCheapTemperatureSensorAssignedNow();
  doc["cheap_temp_mode_enabled"] = heatCheapTemperatureModeEnabledNow();
  doc["cheap_temp_sensor_valid"] = heatCheapTemperatureSensorValidNow();
  doc["cheap_temp_demand"] = heatCheapTemperatureDemandActiveNow();
  doc["cheap_temp_sensor_name"] = heatCheapTemperatureSensorNameNow();
  if (heatCheapTemperatureSensorValidNow()) {
    doc["cheap_temp_c"] = heatCheapTemperatureCurrentCNow();
    doc["cheap_temp_start_c"] = heatCheapTemperatureStartCNow();
    doc["cheap_temp_stop_c"] = heatCheapTemperatureStopCNow();
  }
  if (heatCheapPlanValid) {
    doc["cheap_plan_start_date"] = heatCheapPlanStartDate;
    doc["cheap_plan_start_idx"] = heatCheapPlanStartIdx;
    doc["cheap_plan_end_date"] = heatCheapPlanEndDate;
    doc["cheap_plan_end_idx_excl"] = heatCheapPlanEndIdxExcl;
  }
  doc["updatedAt"] = fmtTimeLocal(time(nullptr));

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json; charset=utf-8", out);
}

