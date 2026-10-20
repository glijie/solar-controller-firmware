#include "sensors.h"
#include "app_globals.h"
#include <ArduinoJson.h>
#include <math.h>

// ===== MOVED FROM .INO (Step 2) =====
// DS18B20 runtime
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
bool   ds18Available              = false;
float  lastTempC                  = NAN;
unsigned long lastTempPollMs      = 0;
unsigned long lastTempSerialMs    = 0;


// ================= TEMPERATURE SENSORS (MULTI) =================
// Vanaf v2025.12.27.01 ondersteunen we meerdere DS18B20 sensoren op één bus.
// - Detectie: automatisch (scan op boot en periodiek indien sensor 'verdwijnt')
// - UI: per sensor Enable + Naam + 1 sensor kiezen als 'Control' (voor legionella/temperatuur logica)
// - Home Assistant: MQTT Discovery per sensor (unique_id op basis van DS18B20 address)
//
// Belangrijk:
// - We houden lastTempC bestaan voor backward compatibility (legionella/status JSON).
// - lastTempC wordt gevuld met de CONTROL temperatuur (de sensor die je kiest).
//
// Praktische limiet: 6 sensoren is ruim genoeg voor de Solar Controller en blijft mobiel overzichtelijk.
// MAX_TEMP_SENSORS + TempSensorEntry staan in sensors.h om dubbele definities te voorkomen.

TempSensorEntry tempSensors[MAX_TEMP_SENSORS];
int   tempSensorCount            = 0;

// Config (Preferences)
// - ts_map: JSON mapping per address -> { "name": "...", "en": 0/1 }
// - ts_ctrl: address string die als CONTROL sensor geldt
// - ts_pub:  publish namen naar HA (Discovery name field)
String tempSensorMapJson         = "";
String tempControlAddrStr        = "";   // leeg => eerste gevonden sensor
bool   tempPublishNamesToHA      = true;



// ===== MOVED FUNCTIONS FROM .INO (Step 2) =====

void applyTempSensorMap() {
  // Default: alles enabled + standaard naam "Temp N"
  for (int i = 0; i < tempSensorCount; i++) {
    if (tempSensors[i].name.length() == 0) tempSensors[i].name = String("Temp ") + String(i + 1);
    tempSensors[i].enabled = true;
  }

  if (tempSensorMapJson.length() == 0) return;

  // Let op: DynamicJsonDocument i.v.m. variable mapping size
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, tempSensorMapJson);
  if (err) return;

  JsonObject root = doc.as<JsonObject>();
  for (int i = 0; i < tempSensorCount; i++) {
    String k = tempSensors[i].addrStr;
    if (!root.containsKey(k)) continue;

    JsonObject o = root[k];
    const char* n = o["name"] | "";
    bool en = (o["en"] | 1) ? true : false;

    if (String(n).length() > 0) tempSensors[i].name = String(n);
    tempSensors[i].enabled = en;
  }
}

// Zet een DeviceAddress (8 bytes) om naar een lowercase hex string zonder separators.
// Resultaat is 16 chars, bijv: "28ff4c1d2a1603b1"
static String dsAddrToString(const DeviceAddress addr) {
  static const char hex[] = "0123456789abcdef";
  char out[17];
  for (int i = 0; i < 8; i++) {
    out[i * 2 + 0] = hex[(addr[i] >> 4) & 0x0F];
    out[i * 2 + 1] = hex[addr[i] & 0x0F];
  }
  out[16] = '\0';
  return String(out);
}



void scanTempSensors() {
  tempSensorCount = 0;
  for (int i = 0; i < MAX_TEMP_SENSORS; i++) {
    tempSensors[i].addrStr = "";
    tempSensors[i].name    = "";
    tempSensors[i].enabled = true;
    tempSensors[i].present = false;
    tempSensors[i].tempC   = NAN;
  }

  int cnt = ds18b20.getDeviceCount();
  if (cnt <= 0) {
    ds18Available = false;
    return;
  }

  for (int i = 0; i < cnt && tempSensorCount < MAX_TEMP_SENSORS; i++) {
    DeviceAddress a;
    if (!ds18b20.getAddress(a, i)) continue;

    TempSensorEntry& e = tempSensors[tempSensorCount++];
    memcpy(e.addr, a, sizeof(DeviceAddress));
    e.addrStr = dsAddrToString(e.addr);
    e.present = true;
    e.tempC   = NAN;
  }

  ds18Available = (tempSensorCount > 0);

  // Toepassen van opgeslagen namen/enabled flags
  applyTempSensorMap();

  // CONTROL sensor fallback:
  // - als ts_ctrl leeg is: pak de eerste
  // - als ts_ctrl bestaat maar niet aanwezig is: pak de eerste aanwezige
  if (tempSensorCount > 0) {
    bool ctrlOk = false;
    if (tempControlAddrStr.length() == 16) {
      for (int i = 0; i < tempSensorCount; i++) {
        if (tempSensors[i].addrStr == tempControlAddrStr) { ctrlOk = true; break; }
      }
    }
    if (!ctrlOk) tempControlAddrStr = tempSensors[0].addrStr;
  }
}




static const unsigned long SENSOR_TEMP_POLL_INTERVAL_MS = 5000UL;
#ifndef TEMP_SERIAL_DEBUG
#define TEMP_SERIAL_DEBUG 1
#endif

bool tempIsValid(float t) {
  if (isnan(t)) return false;
  if (t > 84.9f && t < 85.1f) return false;
  if (t < -20.0f || t > 125.0f) return false;
  return true;
}

void updateTempIfNeeded() {
  unsigned long nowMs = millis();
  if (nowMs - lastTempPollMs < SENSOR_TEMP_POLL_INTERVAL_MS) return;
  lastTempPollMs = nowMs;

  if (!ds18Available || tempSensorCount == 0) {
    scanTempSensors();
    if (!ds18Available) {
      lastTempC = NAN;
      return;
    }
  }

  ds18b20.requestTemperatures();

  for (int i = 0; i < tempSensorCount; i++) {
    TempSensorEntry& e = tempSensors[i];
    float t = ds18b20.getTempC(e.addr);

    if (fabsf(t - 85.0f) < 0.01f) {
      delay(800);
      t = ds18b20.getTempC(e.addr);
    }

    if (t <= -100.0f) {
      e.present = false;
      e.tempC = t;
      continue;
    }

    e.present = true;
    e.tempC = t;

    if (mqttEnabled && mqttClient.connected()) {
      String tpc = mqttBaseTopic + "/temp/" + e.addrStr + "/state";
      if (e.present) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", e.tempC);
        mqttClient.publish(tpc.c_str(), buf, true);
      }
    }
  }

  String useAddr = (legTempAddrStr.length() == 16) ? legTempAddrStr : tempControlAddrStr;

  lastTempC = NAN;
  if (useAddr.length() == 16) {
    for (int i = 0; i < tempSensorCount; i++) {
      if (tempSensors[i].addrStr == useAddr) {
        if (tempSensors[i].present && tempIsValid(tempSensors[i].tempC)) {
          lastTempC = tempSensors[i].tempC;
        }
        break;
      }
    }
  }

  if (!tempIsValid(lastTempC)) {
    for (int i = 0; i < tempSensorCount; i++) {
      if (tempSensors[i].present && tempIsValid(tempSensors[i].tempC)) {
        lastTempC = tempSensors[i].tempC;
        break;
      }
    }
  }

  if (TEMP_SERIAL_DEBUG) {
    const unsigned long sm = millis();
    if (sm - lastTempSerialMs >= 2000UL) {
      lastTempSerialMs = sm;
      Serial.print("[DS18B20] cnt=");
      Serial.print(tempSensorCount);
      Serial.print(" pref=");
      Serial.print(useAddr);
      Serial.print(" last=");
      if (isnan(lastTempC)) Serial.print("NAN");
      else Serial.print(lastTempC, 2);

      for (int i = 0; i < tempSensorCount; i++) {
        Serial.print(" | ");
        Serial.print(tempSensors[i].addrStr);
        Serial.print(" ");
        if (!tempSensors[i].present) Serial.print("OFF");
        else Serial.print("ON");
        Serial.print(" t=");
        if (isnan(tempSensors[i].tempC)) Serial.print("NAN");
        else Serial.print(tempSensors[i].tempC, 2);
      }
      Serial.println();
    }
  }
}
