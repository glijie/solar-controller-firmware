#pragma once

#include <Arduino.h>

String meterLabel();
bool readMeterPower(long &powerW);
bool readMeterPhases(float &i1, float &i2, float &i3);
void appRefreshSmartGatewaysPhasesForGuard();
