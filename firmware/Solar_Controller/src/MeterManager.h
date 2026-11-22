#pragma once

#include <Arduino.h>

inline bool isHomeyP1Meter(const String& type) {
  return type == "homey_energy_dongle";
}

inline bool isSupportedP1MeterType(const String& type) {
  return type == "youless" || type == "homewizard" || type == "smartgateways" || isHomeyP1Meter(type);
}

String meterLabel();
void updateHomeyMeterConnection();
bool readMeterPower(long &powerW);
bool readMeterPhases(float &i1, float &i2, float &i3, unsigned long& measuredAtMs);
void appRefreshSmartGatewaysPhasesForGuard();
