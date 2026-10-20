#pragma once

#include <Arduino.h>

void mcTcpLinkInit();
void mcTcpLinkShutdown();
void mcTcpLinkLoop(uint32_t nowMs, bool slaveRole);
void mcTcpLinkConfigChanged();

bool multiControllerTcpRealtimeAvailable();
bool multiControllerTcpSlaveServerListening();
uint16_t multiControllerGetTcpControlPort();
bool multiControllerIsPeerTcpConnected(uint8_t slotIndex);
uint32_t multiControllerGetPeerTcpAgeMs(uint8_t slotIndex);
uint32_t multiControllerGetPeerTcpRoundTripMs(uint8_t slotIndex);
uint32_t multiControllerGetPeerTcpReconnectCount(uint8_t slotIndex);
