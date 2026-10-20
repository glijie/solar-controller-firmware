#pragma once

#include <Arduino.h>
#include <WebServer.h>

String sanitizeHostname(const String& raw);
bool isValidHostname(const String& hn);
bool connectWiFiSTA(const String& wifiSsid, const String& wifiPass);

// WiFi herstel/diagnose (step308)
// - Alleen actief in normale RUN/STA-modus.
// - Config-AP gedrag blijft ongewijzigd.
void wifiRecoverySetup();
void wifiRecoveryTick();
uint32_t wifiDisconnectCount();
uint32_t wifiReconnectCount();
uint32_t wifiReconnectSuccessCount();
uint32_t wifiReconnectAttemptsCurrentOutage();
uint8_t wifiLastDisconnectReasonCode();
String wifiLastDisconnectReason();
String wifiStatusText();
String wifiPowerSaveModeText();
String wifiLastEventText();
String wifiPreviousSessionEvent();
String wifiPreviousSessionDisconnectReason();
uint32_t wifiPreviousSessionReconnectAttempts();
uint32_t wifiPreviousSessionEventUptimeS();
void startConfigAP(bool& configMode);
void setupTimeNtp();
// step309: publiceert HTTP mDNS + _solarcontroller._tcp voor Homey discovery.
bool startMdnsHttp(const String& deviceName);
void handleWifiScanJson();
void registerNetworkRoutes(WebServer& server);
