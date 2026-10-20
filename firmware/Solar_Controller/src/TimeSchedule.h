#pragma once

#include <Arduino.h>
#include <time.h>

inline constexpr uint8_t GAS_DAY_START_HOUR = 6;

bool timeIsValid();
uint32_t dateIntLocal(time_t t);
String fmtTimeLocal(time_t t);
time_t computeNextLegionellaEpoch();

String isoDateLocal(time_t t);
String dmyDateLocal(time_t t);
String gasDayIsoLocal(time_t t);
String isoDateAddDays(const String& iso, int deltaDays);
String fmtDateDMYFromIso(const String& iso);
int isoDeltaMinutes(const String& startIso, const String& endIso);

void recomputeCheapestDaylightWindowIfNeeded();
bool normalAllowedByScheduleNow();
bool appNormalAllowedByScheduleNow();
