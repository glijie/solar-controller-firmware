#include "MqttParsers.h"

bool parseOnOff(const String &payload, bool &out) {
  String p = payload;
  p.trim(); p.toUpperCase();
  if (p == "ON" || p == "1" || p == "TRUE" || p == "YES") { out = true;  return true; }
  if (p == "OFF" || p == "0" || p == "FALSE" || p == "NO") { out = false; return true; }
  return false;
}

bool parseAlgorithm(const String& payload, uint8_t &outAlg) {
  String p = payload; p.trim(); p.toUpperCase();
  if (p == "STEP" || p == "1")   { outAlg = 1; return true; }
  if (p == "DIRECT" || p == "0") { outAlg = 0; return true; }
  return false;
}

bool parseDowPayload(const String& payload, int &outDow) {
  // Accept 0..6, or Dutch/English day names (case-insensitive).
  String p = payload;
  p.trim();
  if (p.length() == 0) return false;

  bool allNum = true;
  for (size_t i = 0; i < p.length(); i++) {
    char c = p[i];
    if (c < '0' || c > '9') { allNum = false; break; }
  }
  if (allNum) {
    int v = p.toInt();
    if (v >= 0 && v <= 6) { outDow = v; return true; }
  }

  p.toUpperCase();

  if (p.startsWith("ZON")) { outDow = 0; return true; }
  if (p.startsWith("MAA")) { outDow = 1; return true; }
  if (p.startsWith("DIN")) { outDow = 2; return true; }
  if (p.startsWith("WOE")) { outDow = 3; return true; }
  if (p.startsWith("DON")) { outDow = 4; return true; }
  if (p.startsWith("VRI")) { outDow = 5; return true; }
  if (p.startsWith("ZAT")) { outDow = 6; return true; }

  if (p.startsWith("SUN")) { outDow = 0; return true; }
  if (p.startsWith("MON")) { outDow = 1; return true; }
  if (p.startsWith("TUE")) { outDow = 2; return true; }
  if (p.startsWith("WED")) { outDow = 3; return true; }
  if (p.startsWith("THU")) { outDow = 4; return true; }
  if (p.startsWith("FRI")) { outDow = 5; return true; }
  if (p.startsWith("SAT")) { outDow = 6; return true; }

  return false;
}
