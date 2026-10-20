#pragma once
#include <Arduino.h>

// Nordpool/price management moved out of the main .ino to keep the project maintainable.
// Only declarations live here; implementation is in nordpool.cpp.

float nordpoolToCtKwh(float eur_mwh);
float npEffectiveCtKwh(float spotCtKwh);
float npTotalCtKwh(float spotCtKwh);
float npBoostPriceCtKwh(float spotCtKwh);
void  npRecomputeBoostPlanToday();
bool  npIsBoostActiveNow();
bool  nordpoolFetchDate(int year, int month, int day, bool isTomorrow);
void  npInvalidateCache();
void  nordpoolTick();
