#pragma once

#include <ArduinoJson.h>

void appendMultiControllerCompactPeerOutputs(JsonDocument& doc);
void appendMultiControllerLocalPwmArray(JsonDocument& doc);
void appendMultiControllerMqttStatus(JsonDocument& doc);
void appendMultiControllerApiStatus(JsonDocument& doc);
void appendMultiControllerApiStatusLight(JsonDocument& doc);
void appendMultiControllerPeerCompactStatus(JsonDocument& doc);
