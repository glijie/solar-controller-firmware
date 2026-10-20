#pragma once

#include <Arduino.h>

String fmtHmMin(int minOfDay);
void redirectTo(const String& url);
String htmlAttrEscape(const String& in);
String fmtDot(float v, int decimals);
