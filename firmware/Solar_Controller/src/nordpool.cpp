#include "nordpool.h"
#include "app_globals.h"

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

float nordpoolToCtKwh(double eurPerMwh){
  double eurPerKwh = eurPerMwh / 1000.0;
  return (float)(eurPerKwh * 100.0);
}

float npEffectiveCtKwh(float spotCt){
  if (!npInclTaxEnabled) return spotCt;
  float base = spotCt + npEnergyTaxCt + npInkoopCt;
  float total = base * (1.0f + (npBtwPct / 100.0f));
  return total;
}

float npTotalCtKwh(float spotCt){
  float base = spotCt + npEnergyTaxCt + npInkoopCt;
  float total = base * (1.0f + (npBtwPct / 100.0f));
  return total;
}

float npBoostPriceCtKwh(float spotCt){
  return (npBoostPriceSource == 1) ? npTotalCtKwh(spotCt) : spotCt;
}


void npInvalidateCache(){
  // Force a refetch on next nordpoolTick() and rebuild derived plans (boost/cheapest windows)
  npTodayN = 0;
  npTomorrowN = 0;
  npIntervalMinToday = 0;
  npIntervalMinTomorrow = 0;
  npUpdatedAt = "";
  npLastOkToday = false;
  npLastOkTomorrow = false;
  npLastHttp = 0;
  npLastError = "";
  // Reset boost runtime info (will be recomputed after fetch today)
  npBoostSelectedN = 0;
  npBoostEligibleN = 0;
  npBoostNegativeN = 0;
  npBoostActiveNow = false;
  // Make tick react immediately
  npLastPollMs = 0;
  npLastFetchMs = 0;
}

void npRecomputeBoostPlanToday(){
  npBoostSelectedN = 0;
  npBoostEligibleN = 0;
  npBoostNegativeN = 0;

  if (!npEnabled || npTodayN <= 0) return;

  // 1) tel negatieve blokken vandaag (op gekozen prijsbron)
  for (int i=0;i<npTodayN;i++){
    float p = npBoostPriceCtKwh(npTodayCt[i]);
    if (p < 0.0f) npBoostNegativeN++;
  }

  // 2) selecteer alle blokken die voldoen aan drempel
  int idxTmp[96];
  float priceTmp[96];
  int n=0;
  for (int i=0;i<npTodayN && n<96;i++){
    float p = npBoostPriceCtKwh(npTodayCt[i]);
    if (p <= npBoostThresholdCt){
      idxTmp[n] = i;
      priceTmp[n] = p;
      n++;
    }
  }
  npBoostEligibleN = n;

  if (!npBoostEnabled || n <= 0 || npBoostNBlocks <= 0) return;

  // 3) sorteer op meest goedkoop/negatief (ascending)
  for (int i=0;i<n-1;i++){
    int best=i;
    for (int j=i+1;j<n;j++){
      if (priceTmp[j] < priceTmp[best]) best=j;
    }
    if (best != i){
      float tp = priceTmp[i]; priceTmp[i]=priceTmp[best]; priceTmp[best]=tp;
      int ti = idxTmp[i]; idxTmp[i]=idxTmp[best]; idxTmp[best]=ti;
    }
  }

  // 4) kies top-N (of minder als niet genoeg)
  int pick = npBoostNBlocks;
  if (pick > n) pick = n;
  if (pick > 96) pick = 96;

  for (int i=0;i<pick;i++){
    npBoostIdx[i] = idxTmp[i];
  }
  npBoostSelectedN = pick;
}

bool npIsBoostActiveNow(){
  if (!npBoostEnabled) return false;
  if (npBoostSelectedN <= 0) return false;
  if (!timeIsValid()) return false;

  time_t now = time(nullptr);
  // Alleen "vandaag" toepassen (zodra date wisselt, wordt plan opnieuw gezet in nordpoolTick)
  if (isoDateLocal(now) != npDateToday) return false;

  int interval = (npIntervalMinToday > 0) ? npIntervalMinToday : npResMin;
  if (interval <= 0) interval = 60;

  struct tm tmNow;
  localtime_r(&now, &tmNow);
  int minutes = tmNow.tm_hour * 60 + tmNow.tm_min;
  int idx = minutes / interval;
  if (idx < 0 || idx >= npTodayN) return false;

  for (int i=0;i<npBoostSelectedN;i++){
    if (npBoostIdx[i] == idx) return true;
  }
  return false;
}

bool nordpoolFetchDate(const String& dateStr, float* outCt, int maxPoints, int &outN, int &outIntervalMin, String &outUpdatedAt, bool allowImmediateSecond=false){
  outN = 0;
  outIntervalMin = 0;
  outUpdatedAt = "";

  if (!npEnabled) { npLastError = "disabled"; return false; }
  if (WiFi.status() != WL_CONNECTED) { npLastError = "no_wifi"; return false; }
  if (!timeIsValid()) { npLastError = "no_time"; return false; } // we gebruiken local date planning

  // Rate limit (anti-spam)
unsigned long nowMs = millis();
if (nowMs - npLastFetchMs < 15000UL) {
  // Consument-proof: in één "batch" (vandaag + morgen) mogen we 2 calls direct achter elkaar doen
  // zodat je na 14:00 niet eerst 'vandaag OK' en daarna 'morgen rate_limit' krijgt.
  if (!(allowImmediateSecond && (nowMs - npLastFetchMs) < 2000UL)) {
    npLastError = "rate_limit";
    return false;
  }
}
npLastFetchMs = nowMs;

String area = npArea; area.trim();
  if (area.length() == 0) area = "NL";

  // API (zoals HA integration gebruikt): https://dataportal-api.nordpoolgroup.com/api/DayAheadPrices
  String url = "https://dataportal-api.nordpoolgroup.com/api/DayAheadPrices?date=" + dateStr +
               "&market=DayAhead&deliveryArea=" + area + "&currency=EUR";

  WiFiClientSecure client;
  client.setInsecure(); // gratis/zonder cert bundle (ok voor publieke data)
  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  https.setTimeout(6000);

  if (!https.begin(client, url)) {
    npLastError = "https_begin_failed";
    return false;
  }

  int httpCode = https.GET();
  npLastHttp = httpCode;

  if (httpCode != HTTP_CODE_OK) {
    // 204 = "No Content" (prijzen nog niet gepubliceerd)
    if (httpCode == HTTP_CODE_NO_CONTENT) {
      npLastError = "no_content";
    } else {
      npLastError = String("http_") + String(httpCode);
    }
    https.end();
    return false;
  }

  String payload = https.getString();
  https.end();

  if (payload.length() < 10) {
    npLastError = "empty_payload";
    return false;
  }

  // Dynamic omdat morgen (96 kwartieren) wat groter kan zijn
  DynamicJsonDocument doc(20000);
  DeserializationError err = deserializeJson(doc, payload);
  if (err != DeserializationError::Ok) {
    npLastError = String("json_err_") + err.c_str();
    return false;
  }

  if (!doc.containsKey("multiAreaEntries")) {
    npLastError = "no_multiAreaEntries";
    return false;
  }

  if (doc.containsKey("updatedAt")) outUpdatedAt = doc["updatedAt"].as<String>();

  JsonArray arr = doc["multiAreaEntries"].as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) {
    npLastError = "empty_entries";
    return false;
  }

  // eerste interval bepalen
  JsonObject e0 = arr[0].as<JsonObject>();
  String ds0 = e0["deliveryStart"] | "";
  String de0 = e0["deliveryEnd"]   | "";
  int intervalMin = isoDeltaMinutes(ds0, de0);
  outIntervalMin = intervalMin;

  // Filter naar gewenste resolutie:
  // - Als API kwartierprijzen teruggeeft maar user wil uur: aggregeren per 4 (gemiddelde).
  // - Als API uurprijzen teruggeeft maar user wil kwartier: we kunnen niet "invullen" -> dan houden we uur (en tonen dat als interval=60).
  bool apiIs15 = (intervalMin == 15);
  bool want15 = (npResMin == 15);

  // tijdelijke buffer met alle punten (ct/kWh)
  float tmp[96];
  int tmpN = 0;

  for (JsonObject e : arr) {
    if (tmpN >= 96) break;

    JsonObject per = e["entryPerArea"].as<JsonObject>();
    if (per.isNull()) continue;

    // area key kan exact zijn (NL/BE/etc)
    if (!per.containsKey(area)) continue;

    double eurPerMwh = per[area].as<double>();
    tmp[tmpN++] = nordpoolToCtKwh(eurPerMwh);
  }

  if (tmpN == 0) {
    npLastError = "no_area_data";
    return false;
  }

  if (apiIs15 && !want15) {
    // 15 -> 60: 4 kwartieren middelen
    int groups = tmpN / 4;
    if (groups < 1) { npLastError = "aggregate_failed"; return false; }
    outN = 0;
    for (int g=0; g<groups && outN < maxPoints; g++) {
      float sum = tmp[g*4] + tmp[g*4+1] + tmp[g*4+2] + tmp[g*4+3];
      outCt[outN++] = sum / 4.0f;
    }
    outIntervalMin = 60;
  } else {
    // keep as-is
    outN = (tmpN > maxPoints) ? maxPoints : tmpN;
    for (int i=0;i<outN;i++) outCt[i]=tmp[i];
    // interval blijft zoals API teruggeeft (meestal 60 of 15)
    // Als user 15 wil maar API 60 geeft: dan blijft 60.
  }

  npLastError = "";
  return true;
}

void nordpoolTick(){
  if (!npEnabled) return;
  if (configMode) return;
  if (!timeIsValid()) return;

  unsigned long nowMs = millis();
  if (nowMs - npLastPollMs < 5000UL) return; // 5s tick
  npLastPollMs = nowMs;

  time_t now = time(nullptr);
  String today = isoDateLocal(now);

  // dagwissel
  if (npDateToday != today) {
    npDateToday = today;
    npTodayN = 0;
    npTomorrowN = 0;
    npUpdatedAt = "";
    npLastOkToday = false;
    npLastOkTomorrow = false;
    // reset prijs-boost runtime info (wordt opnieuw opgebouwd na fetch today)
    npBoostSelectedN = 0;
    npBoostEligibleN = 0;
    npBoostNegativeN = 0;
    npBoostActiveNow = false;
  }

  // Zorg dat "vandaag" er is
  if (npTodayN == 0) {
    String upd;
    int n=0, interval=0;
    bool ok = nordpoolFetchDate(today, npTodayCt, 96, n, interval, upd, true);
    npLastOkToday = ok;
    if (ok) {
      npTodayN = n;
      npIntervalMinToday = interval;
      npUpdatedAt = fmtTimeLocal(now);
      // Prijs-boost plan opnieuw berekenen zodra vandaag-data binnen is
      npRecomputeBoostPlanToday();
    }
  }

  // Morgen (na 14:00 lokale tijd)
  struct tm tmNow; localtime_r(&now, &tmNow);
  bool after14 = (tmNow.tm_hour >= 14);

  // date voor morgen
  time_t tom = now + 24*3600;
  String tomorrow = isoDateLocal(tom);

  if (after14 && npTomorrowN == 0) {
    String upd;
    int n=0, interval=0;
    bool ok = nordpoolFetchDate(tomorrow, npTomorrowCt, 96, n, interval, upd, true);
    npLastOkTomorrow = ok;
    if (ok) {
      npTomorrowN = n;
      npIntervalMinTomorrow = interval;
      if (upd.length() > 0) npUpdatedAt = fmtTimeLocal(now);
      npDateTomorrow = tomorrow;
    }
  }
}
