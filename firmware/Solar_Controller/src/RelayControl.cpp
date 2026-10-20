#include "RelayControl.h"

namespace {
  bool gRelayEnabled = false;
  uint8_t gRelayType = RELAY_TYPE_ACTIVE_LOW;
  bool gRelayBootOn = false;
  bool gRelayManualOn = false;
  uint32_t gRelayManualChangeCounter = 0;

  uint8_t clampRelayType(uint8_t v) {
    return (v == RELAY_TYPE_ACTIVE_HIGH) ? RELAY_TYPE_ACTIVE_HIGH : RELAY_TYPE_ACTIVE_LOW;
  }

  int logicalToPhysical(bool on) {
    const bool activeLow = (gRelayType == RELAY_TYPE_ACTIVE_LOW);
    return activeLow ? (on ? LOW : HIGH) : (on ? HIGH : LOW);
  }

  void writeRelayPin(bool on) {
    pinMode(RELAY_GPIO_PIN, OUTPUT);
    digitalWrite(RELAY_GPIO_PIN, logicalToPhysical(on));
  }
}

void relaySetDefaults() {
  gRelayEnabled = false;
  gRelayType = RELAY_TYPE_ACTIVE_LOW;
  gRelayBootOn = false;
  gRelayManualOn = false;
  gRelayManualChangeCounter = 0;
}

void relayEarlyBootInit() {
  Preferences prefs;
  relaySetDefaults();

  prefs.begin("boilerbox", true);
  gRelayEnabled = prefs.getBool("relay_en", false);
  gRelayType = clampRelayType((uint8_t)prefs.getUChar("relay_type", RELAY_TYPE_ACTIVE_LOW));
  gRelayBootOn = prefs.getBool("relay_boot_on", false);
  prefs.end();

  gRelayManualOn = gRelayBootOn;

  if (!gRelayEnabled) {
    pinMode(RELAY_GPIO_PIN, INPUT);
    return;
  }

  writeRelayPin(gRelayBootOn);
}

void relayLoadConfig(Preferences& prefs) {
  gRelayEnabled = prefs.getBool("relay_en", false);
  gRelayType = clampRelayType((uint8_t)prefs.getUChar("relay_type", RELAY_TYPE_ACTIVE_LOW));
  gRelayBootOn = prefs.getBool("relay_boot_on", false);
  gRelayManualOn = gRelayBootOn;
}

void relaySaveConfig(Preferences& prefs) {
  prefs.putBool("relay_en", gRelayEnabled);
  prefs.putUChar("relay_type", clampRelayType(gRelayType));
  prefs.putBool("relay_boot_on", gRelayBootOn);
}

bool relayIsEnabled() { return gRelayEnabled; }
int relayGetPin() { return RELAY_GPIO_PIN; }
uint8_t relayGetType() { return clampRelayType(gRelayType); }
bool relayIsActiveLow() { return clampRelayType(gRelayType) == RELAY_TYPE_ACTIVE_LOW; }
bool relayGetBootOn() { return gRelayBootOn; }
bool relayGetManualOn() { return gRelayEnabled ? gRelayManualOn : false; }
uint32_t relayGetManualChangeCounter() { return gRelayManualChangeCounter; }

void relaySetEnabled(bool enabled) {
  gRelayEnabled = enabled;
  if (!gRelayEnabled) gRelayManualOn = false;
}

void relaySetType(uint8_t type) {
  gRelayType = clampRelayType(type);
}

void relaySetBootOn(bool bootOn) {
  gRelayBootOn = bootOn;
}

void relaySetManualOn(bool on) {
  gRelayManualOn = on;
  gRelayManualChangeCounter++;
  relayApplyRuntimeState();
}

void relaySetAutomaticOn(bool on) {
  gRelayManualOn = on;
  relayApplyRuntimeState();
}

void relayToggleManual() {
  if (!gRelayEnabled) {
    relaySetManualOn(false);
    return;
  }
  relaySetManualOn(!gRelayManualOn);
}

void relayApplyRuntimeState() {
  if (!gRelayEnabled) {
    writeRelayPin(false);
    return;
  }
  writeRelayPin(gRelayManualOn);
}

