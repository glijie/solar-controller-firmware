#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>

void registerHealthRoutes(WebServer& server);
void handleStatusJson();
void handleStatusLightJson();
void handleMultiControllerPeerStatusJson();
void handleLiveJson();
void handleSystemJson();
String fmtUptimeDHMM(uint32_t uptimeS);
void mqttPublishSystemJson(bool forceNow = false);
