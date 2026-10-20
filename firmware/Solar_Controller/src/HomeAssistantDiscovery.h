#pragma once

#include <Arduino.h>

String haNodeId();
void mqttPublishHADiscovery(bool forceNow);
uint16_t mqttCleanupHADiscovery();
uint16_t mqttRebuildHADiscovery();
