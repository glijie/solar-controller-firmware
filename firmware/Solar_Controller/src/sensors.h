#pragma once
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// NOTE (Step 2):
// We moved the multi-DS18B20 runtime + mapping logic out of the .ino into sensors.cpp.
// To keep behaviour identical, we keep the original global names and expose them here as extern,
// so the rest of the firmware can remain unchanged.

extern int DS18B20_PIN;                 // was const in the .ino; now int for cross-TU linkage

extern OneWire oneWire;
extern DallasTemperature ds18b20;

extern bool  ds18Available;
extern float lastTempC;
extern unsigned long lastTempPollMs;
extern unsigned long lastTempSerialMs;

// Multi-sensor support
constexpr int MAX_TEMP_SENSORS = 6;

struct TempSensorEntry {
  DeviceAddress addr;
  String  addrStr;   // "28ff...." (lowercase, zonder separators)
  String  name;      // gebruiksvriendelijke naam (UI + HA)
  bool    enabled;   // toon/publish
  bool    present;   // momenteel gezien op de bus
  float   tempC;     // laatste meting
};

extern TempSensorEntry tempSensors[MAX_TEMP_SENSORS];
extern int   tempSensorCount;

// Preferences-backed config
extern String tempSensorMapJson;
extern String tempControlAddrStr;
extern bool   tempPublishNamesToHA;

// Functions used by the existing .ino (kept identical)
void applyTempSensorMap();
void scanTempSensors();
