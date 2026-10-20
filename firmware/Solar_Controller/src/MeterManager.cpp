#include "MeterManager.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

#include "app_globals.h"
#include "PhaseBudget.h"

static unsigned long sgReadCacheMs = 0;
static String sgReadCachePayload;

String meterLabel() {
  if (meterType == "homewizard") return "HomeWizard P1";
  if (meterType == "smartgateways") return "Smart Gateways P1";
  return "Youless LS120";
}

static bool readYoulessPower(long &powerW) {
  powerW = 0;
  if (meterIp.length() == 0) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.setTimeout(500);

  {
    String url = String("http://") + meterIp + "/e?f=j";
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      http.end();

      StaticJsonDocument<512> doc;
      if (deserializeJson(doc, payload) == DeserializationError::Ok) {
        if (doc.is<JsonArray>() && doc.size() > 0) {
          JsonObject obj = doc[0];
          if (obj.containsKey("pwr")) { powerW = obj["pwr"].as<long>(); return true; }
        }
        if (doc.is<JsonObject>() && doc.containsKey("pwr")) { powerW = doc["pwr"].as<long>(); return true; }
      }
    } else {
      http.end();
    }
  }

  {
    String url = String("http://") + meterIp + "/a?f=j";
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) { http.end(); return false; }
    String payload = http.getString();
    http.end();

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) return false;
    if (doc.containsKey("pwr")) { powerW = doc["pwr"].as<long>(); return true; }
  }
  return false;
}

static bool readYoulessPhases(float &i1, float &i2, float &i3) {
  if (meterIp.length() == 0) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.setTimeout(500);

  String url = String("http://") + meterIp + "/f";
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) { http.end(); return false; }
  String payload = http.getString();
  http.end();

  StaticJsonDocument<640> doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) return false;
  if (!doc.is<JsonObject>()) return false;
  JsonObject o = doc.as<JsonObject>();
  if (!(o.containsKey("i1") && o.containsKey("i2") && o.containsKey("i3"))) return false;

  i1 = o["i1"].as<float>();
  i2 = o["i2"].as<float>();
  i3 = o["i3"].as<float>();
  return true;
}

static bool readHomeWizardPhases(float &i1, float &i2, float &i3) {
  if (meterIp.length() == 0) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.setTimeout(500);

  auto tryParsePhases = [&](const String &payload) -> bool {
    StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) return false;
    if (!doc.is<JsonObject>()) return false;
    JsonObject o = doc.as<JsonObject>();

    if (o.containsKey("active_current_l1_a") && o.containsKey("active_current_l2_a") && o.containsKey("active_current_l3_a")) {
      i1 = o["active_current_l1_a"].as<float>();
      i2 = o["active_current_l2_a"].as<float>();
      i3 = o["active_current_l3_a"].as<float>();
      return true;
    }

    if (o.containsKey("current_l1_a") && o.containsKey("current_l2_a") && o.containsKey("current_l3_a")) {
      i1 = o["current_l1_a"].as<float>();
      i2 = o["current_l2_a"].as<float>();
      i3 = o["current_l3_a"].as<float>();
      return true;
    }

    if (o.containsKey("active_power_l1_w") && o.containsKey("active_power_l2_w") && o.containsKey("active_power_l3_w") &&
        o.containsKey("active_voltage_l1_v") && o.containsKey("active_voltage_l2_v") && o.containsKey("active_voltage_l3_v")) {
      float p1 = o["active_power_l1_w"].as<float>();
      float p2 = o["active_power_l2_w"].as<float>();
      float p3 = o["active_power_l3_w"].as<float>();

      float u1 = o["active_voltage_l1_v"].as<float>();
      float u2 = o["active_voltage_l2_v"].as<float>();
      float u3 = o["active_voltage_l3_v"].as<float>();

      if (u1 == 0 || u2 == 0 || u3 == 0) return false;

      i1 = p1 / u1;
      i2 = p2 / u2;
      i3 = p3 / u3;
      return true;
    }

    return false;
  };

  {
    String url = String("http://") + meterIp + "/api/v1/data";
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      http.end();
      if (tryParsePhases(payload)) return true;
    } else {
      http.end();
    }
  }

  {
    String url = String("http://") + meterIp + "/api/measurement";
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) { http.end(); return false; }
    String payload = http.getString();
    http.end();
    return tryParsePhases(payload);
  }
}

static bool readHomeWizardPower(long &powerW) {
  powerW = 0;
  if (meterIp.length() == 0) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.setTimeout(500);

  String url = String("http://") + meterIp + "/api/v1/data";
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) { http.end(); return false; }
  String payload = http.getString();
  http.end();

  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) return false;
  if (!doc.containsKey("active_power_w")) return false;

  double v = doc["active_power_w"].as<double>();
  powerW = (long)lround(v);
  return true;
}

static bool fetchSmartGatewaysReadPayload(String &payload, bool forceRefresh = false) {
  payload = "";
  if (meterIp.length() == 0) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  const unsigned long now = millis();
  if (!forceRefresh && sgReadCachePayload.length() > 0 && (now - sgReadCacheMs) <= 750UL) {
    payload = sgReadCachePayload;
    return true;
  }

  HTTPClient http;
  http.setTimeout(500);

  String url = String("http://") + meterIp + ":82/smartmeter/api/read";
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) { http.end(); return false; }

  payload = http.getString();
  http.end();

  if (payload.length() == 0) return false;
  sgReadCachePayload = payload;
  sgReadCacheMs = now;
  return true;
}

static bool parseSmartGatewaysReadPayload(const String &payload, long *powerW, float *i1, float *i2, float *i3) {
  StaticJsonDocument<4096> doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) return false;

  if (powerW) {
    JsonVariant vp = doc["PowerDeliveredNetto"];
    if (vp.isNull()) return false;

    double netW = vp.as<double>();
    if (!isfinite(netW)) return false;

    // Smart Gateways modelafhankelijk:
    // - connectix_smartmeter_gateway_v1.0 geeft PowerDeliveredNetto al in watt.
    // - DSMR-BELGIUM geeft PowerDeliveredNetto in kW, dus omrekenen naar watt.
    const char* gatewayModel = doc["gateway_model"] | "";
    if (strcmp(gatewayModel, "DSMR-BELGIUM") == 0) {
      netW *= 1000.0;
    }

    *powerW = (long)lround(netW);
  }

  if (i1 || i2 || i3) {
    JsonVariant v1 = doc["Current_l1"];
    JsonVariant v2 = doc["Current_l2"];
    JsonVariant v3 = doc["Current_l3"];
    if (v1.isNull() || v2.isNull() || v3.isNull()) return false;

    float ti1 = v1.as<float>();
    float ti2 = v2.as<float>();
    float ti3 = v3.as<float>();
    if (!isfinite(ti1) || !isfinite(ti2) || !isfinite(ti3)) return false;

    if (i1) *i1 = ti1;
    if (i2) *i2 = ti2;
    if (i3) *i3 = ti3;
  }

  return true;
}

static bool readSmartGatewaysPhases(float &i1, float &i2, float &i3) {
  i1 = i2 = i3 = NAN;

  String payload;
  if (!fetchSmartGatewaysReadPayload(payload, false)) return false;
  return parseSmartGatewaysReadPayload(payload, nullptr, &i1, &i2, &i3);
}

static bool readSmartGatewaysPower(long &powerW) {
  powerW = 0;

  String payload;
  if (!fetchSmartGatewaysReadPayload(payload, false)) return false;
  return parseSmartGatewaysReadPayload(payload, &powerW, nullptr, nullptr, nullptr);
}

static void refreshSmartGatewaysPhasesForGuard() {
  if (meterType != "smartgateways") return;
  if (!phaseGuardEnabled) return;
  if (phaseBudgetSnapshotFresh(750UL)) return;

  phaseBudgetRefreshSnapshot();
}

void appRefreshSmartGatewaysPhasesForGuard() {
  refreshSmartGatewaysPhasesForGuard();
}

bool readMeterPhases(float &i1, float &i2, float &i3) {
  if (meterType == "homewizard") return readHomeWizardPhases(i1, i2, i3);
  if (meterType == "smartgateways") return readSmartGatewaysPhases(i1, i2, i3);
  return readYoulessPhases(i1, i2, i3);
}

bool readMeterPower(long &powerW) {
  if (meterType == "homewizard") return readHomeWizardPower(powerW);
  if (meterType == "smartgateways") return readSmartGatewaysPower(powerW);
  return readYoulessPower(powerW);
}
