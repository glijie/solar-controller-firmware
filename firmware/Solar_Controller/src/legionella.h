#pragma once
#include <Arduino.h>

// Legionella control logic (moved out of .ino in Step 21)
// NOTE: Implementations live in legionella.cpp
void legStart(const String& reason);
void legStop(const String& reason);
void legTick();
void legSchedulerTick();
