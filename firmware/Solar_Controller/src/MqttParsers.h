#pragma once

#include <Arduino.h>

bool parseOnOff(const String &payload, bool &out);
bool parseAlgorithm(const String& payload, uint8_t &outAlg);
bool parseDowPayload(const String& payload, int &outDow);
