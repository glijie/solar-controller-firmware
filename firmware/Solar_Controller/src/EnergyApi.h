#pragma once

#include <Arduino.h>

void gasTick();
void handlePricesJson();
bool gasFetch15Days();
void handleGasHistoryJson();
float gasEurM3Now(bool total);
float gasEurM3ForControlNow(bool total);
float gasEurM3ForControlAtLocal(const String& localDate, int minuteOfDay, bool total);
float elecCtKwhNow();
void handleHeatCompareJson();
