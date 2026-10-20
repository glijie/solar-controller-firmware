#pragma once
// NOTE: We include Arduino.h for the String type used by collectorGetSerial().
#include <Arduino.h>
/*
  Solar Controller — Collector Telemetry (step 127 COLLECTOR_PING_DAILY_V1)
  -----------------------------------------------------------------------
  Doel:
    - 1x per boot een 'ping' sturen naar de Cloudflare Worker collector (voor testen voorlopig)
    - 1x per 24 uur een 'ping' sturen (persistent last_sent via Preferences)
    - Payload: serial + firmware
    - ZO MINIMAAL MOGELIJK: geen invloed op bestaande werking

  Let op:
    - Dit is stap 2A: dagelijkse ping + boot-ping blijft voorlopig AAN voor tests.
*/

void collectorSetup();   // call once after WiFi STA is connected
void collectorLoop();    // call from loop()

// Expose the device serial (SC- + full base MAC) so the UI can show it.
String collectorGetSerial();
