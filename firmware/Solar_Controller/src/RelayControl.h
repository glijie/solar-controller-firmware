#pragma once

#include <Arduino.h>
#include <Preferences.h>

static constexpr int RELAY_GPIO_PIN = 18;
static constexpr uint8_t RELAY_TYPE_ACTIVE_LOW  = 0;
static constexpr uint8_t RELAY_TYPE_ACTIVE_HIGH = 1;

void relaySetDefaults();
void relayEarlyBootInit();
void relayLoadConfig(Preferences& prefs);
void relaySaveConfig(Preferences& prefs);

bool relayIsEnabled();
int relayGetPin();
uint8_t relayGetType();
bool relayIsActiveLow();
bool relayGetBootOn();
bool relayGetManualOn();
uint32_t relayGetManualChangeCounter();

void relaySetEnabled(bool enabled);
void relaySetType(uint8_t type);
void relaySetBootOn(bool bootOn);
void relaySetManualOn(bool on);
void relaySetAutomaticOn(bool on);
void relayToggleManual();
void relayApplyRuntimeState();
