#include "NetworkManager.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "app_globals.h"
#include "collector.h"

extern WebServer server;
namespace {

volatile uint32_t gWifiDisconnectCount = 0;
volatile uint32_t gWifiReconnectCount = 0;
volatile uint32_t gWifiReconnectSuccessCount = 0;
volatile uint8_t gWifiLastDisconnectReasonCode = 0;
volatile bool gWifiDisconnectEventPending = false;
volatile bool gWifiGotIpEventPending = false;
volatile uint32_t gWifiIgnoreDisconnectUntilMs = 0;
portMUX_TYPE gWifiEventMux = portMUX_INITIALIZER_UNLOCKED;

bool gWifiRecoveryInitialized = false;
bool gWifiOutageActive = false;
uint32_t gWifiNextReconnectMs = 0;
uint32_t gWifiReconnectAttemptsCurrentOutage = 0;
String gWifiLastEvent = "BOOT";

// step309: mDNS blijft de bestaande .local-hostname verzorgen en publiceert
// daarnaast een vaste Solar Controller service voor Homey ManagerDiscovery.
bool gMdnsConfigured = false;
bool gMdnsActive = false;
String gMdnsRequestedDeviceName;

String gWifiPreviousSessionEvent = "NONE";
uint8_t gWifiPreviousSessionReasonCode = 0;
uint32_t gWifiPreviousSessionReconnectAttempts = 0;
uint32_t gWifiPreviousSessionEventUptimeS = 0;

constexpr uint32_t WIFI_INITIAL_RECONNECT_DELAY_MS = 5000UL;
constexpr uint32_t WIFI_RECONNECT_BACKOFF_MS[] = {
  15000UL, 30000UL, 60000UL, 120000UL
};
constexpr size_t WIFI_RECONNECT_BACKOFF_COUNT = sizeof(WIFI_RECONNECT_BACKOFF_MS) / sizeof(WIFI_RECONNECT_BACKOFF_MS[0]);

String wifiReasonTextFromCode(uint8_t reason) {
  switch (reason) {
    case 0:   return "NONE";
    case 1:   return "UNSPECIFIED";
    case 2:   return "AUTH_EXPIRE";
    case 3:   return "AUTH_LEAVE";
    case 4:   return "ASSOC_EXPIRE";
    case 5:   return "ASSOC_TOOMANY";
    case 6:   return "NOT_AUTHED";
    case 7:   return "NOT_ASSOCED";
    case 8:   return "ASSOC_LEAVE";
    case 9:   return "ASSOC_NOT_AUTHED";
    case 13:  return "IE_INVALID";
    case 14:  return "MIC_FAILURE";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT";
    case 16:  return "GROUP_KEY_UPDATE_TIMEOUT";
    case 17:  return "IE_IN_4WAY_DIFFERS";
    case 18:  return "GROUP_CIPHER_INVALID";
    case 19:  return "PAIRWISE_CIPHER_INVALID";
    case 20:  return "AKMP_INVALID";
    case 21:  return "UNSUPP_RSN_IE_VERSION";
    case 22:  return "INVALID_RSN_IE_CAP";
    case 23:  return "802_1X_AUTH_FAILED";
    case 24:  return "CIPHER_SUITE_REJECTED";
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    default: {
      char buf[24];
      snprintf(buf, sizeof(buf), "REASON_%u", (unsigned int)reason);
      return String(buf);
    }
  }
}

void persistWifiSessionEvent(const char* eventName, uint8_t reasonCode, uint32_t reconnectAttempts) {
  Preferences diagPrefs;
  if (!diagPrefs.begin("wifi_diag", false)) return;
  diagPrefs.putString("event", eventName ? eventName : "UNKNOWN");
  diagPrefs.putUChar("reason", reasonCode);
  diagPrefs.putUInt("attempts", reconnectAttempts);
  diagPrefs.putUInt("uptime", (uint32_t)(millis() / 1000UL));
  diagPrefs.end();
}

void loadPreviousWifiSessionEvent() {
  Preferences diagPrefs;
  if (!diagPrefs.begin("wifi_diag", true)) return;
  gWifiPreviousSessionEvent = diagPrefs.getString("event", "NONE");
  gWifiPreviousSessionReasonCode = diagPrefs.getUChar("reason", 0);
  gWifiPreviousSessionReconnectAttempts = diagPrefs.getUInt("attempts", 0);
  gWifiPreviousSessionEventUptimeS = diagPrefs.getUInt("uptime", 0);
  diagPrefs.end();
}

String mdnsFallbackHostname() {
  // Hergebruik ook voor de fallback-hostname hetzelfde bestaande serienummer.
  // SC-XXXXXXXXXXXX is 15 tekens en past dus ruim binnen de 20-teken limiet.
  String serial = collectorGetSerial();
  serial.toLowerCase();
  return serial;
}

String mdnsEffectiveHostname(const String& requestedName) {
  String hn = sanitizeHostname(requestedName);
  if (isValidHostname(hn)) return hn;

  hn = sanitizeHostname(mdnsFallbackHostname());
  return hn;
}

bool publishMdnsServices() {
  if (WiFi.status() != WL_CONNECTED) return false;

  const String hostname = mdnsEffectiveHostname(gMdnsRequestedDeviceName);
  if (!isValidHostname(hostname)) return false;

  if (gMdnsActive) {
    MDNS.end();
    gMdnsActive = false;
  }

  if (!MDNS.begin(hostname.c_str())) return false;

  // Bestaande browser-toegang via http://<hostname>.local blijft behouden.
  MDNS.addService("http", "tcp", 80);

  // step309: officiële Solar Controller service voor Homey ManagerDiscovery.
  // Het bestaande SC-serienummer is de enige/stabiele apparaat-ID.
  MDNS.addService("solarcontroller", "tcp", 80);

  const String serial = collectorGetSerial();
  String displayName = gMdnsRequestedDeviceName;
  displayName.trim();
  if (displayName.length() == 0) displayName = "Solar Controller";

  MDNS.addServiceTxt((char*)"solarcontroller", (char*)"tcp", (char*)"id", (char*)serial.c_str());
  MDNS.addServiceTxt((char*)"solarcontroller", (char*)"tcp", (char*)"model", (char*)"solar-controller");
  MDNS.addServiceTxt((char*)"solarcontroller", (char*)"tcp", (char*)"fw", (char*)FW_VERSION);
  MDNS.addServiceTxt((char*)"solarcontroller", (char*)"tcp", (char*)"name", (char*)displayName.c_str());
  MDNS.addServiceTxt((char*)"solarcontroller", (char*)"tcp", (char*)"api", (char*)"1");

  gMdnsActive = true;
  return true;
}

void applyWifiNoPowerSave() {
  if (WiFi.status() != WL_CONNECTED) return;
  esp_wifi_set_ps(WIFI_PS_NONE);
}

void wifiEventHandler(WiFiEvent_t event, WiFiEventInfo_t info) {
  // Arduino-ESP32 roept WiFi-events vanuit een aparte FreeRTOS-task aan.
  // Daarom raakt de callback alleen kleine POD-velden aan onder een critical section;
  // Strings, Preferences en herstelacties blijven volledig in de hoofd-loop.
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    const uint32_t eventMs = millis();
    portENTER_CRITICAL(&gWifiEventMux);
    const uint32_t ignoreUntilMs = gWifiIgnoreDisconnectUntilMs;
    const bool intentional = (ignoreUntilMs != 0) && ((int32_t)(eventMs - ignoreUntilMs) < 0);
    if (!intentional) {
      gWifiIgnoreDisconnectUntilMs = 0;
      gWifiLastDisconnectReasonCode = info.wifi_sta_disconnected.reason;
      gWifiDisconnectCount++;
      gWifiDisconnectEventPending = true;
    }
    portEXIT_CRITICAL(&gWifiEventMux);
    return;
  }

  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    portENTER_CRITICAL(&gWifiEventMux);
    gWifiGotIpEventPending = true;
    portEXIT_CRITICAL(&gWifiEventMux);
  }
}

void startWifiOutageIfNeeded(uint32_t nowMs) {
  if (gWifiOutageActive) return;
  gWifiOutageActive = true;
  gWifiNextReconnectMs = nowMs + WIFI_INITIAL_RECONNECT_DELAY_MS;
  gWifiReconnectAttemptsCurrentOutage = 0;
  gWifiLastEvent = "WIFI_DISCONNECTED";
}

void markWifiRecovered() {
  if (!gWifiOutageActive) return;

  gWifiReconnectSuccessCount++;
  gWifiLastEvent = "WIFI_RECOVERED";

  // Maximaal één extra NVS-write bij een herstelde storing. Daardoor kan na een
  // latere reboot worden gezien dat de vorige geregistreerde WiFi-storing wel
  // degelijk is hersteld, zonder iedere reconnect-poging naar flash te schrijven.
  if (gWifiReconnectAttemptsCurrentOutage > 0) {
    persistWifiSessionEvent("WIFI_RECOVERED", wifiLastDisconnectReasonCode(),
                            gWifiReconnectAttemptsCurrentOutage);
  }

  gWifiOutageActive = false;
  gWifiNextReconnectMs = 0;
  gWifiReconnectAttemptsCurrentOutage = 0;
}

}  // namespace

String sanitizeHostname(const String& raw) {
  String out;
  out.reserve(20);
  bool prevDash = false;

  for (size_t i = 0; i < raw.length() && out.length() < 20; i++) {
    char c = raw[i];
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');

    const bool isAlpha = (c >= 'a' && c <= 'z');
    const bool isDigit = (c >= '0' && c <= '9');
    const bool wantsDash = (c == '-' || c == ' ' || c == '_' || c == '.');

    if (isAlpha || isDigit) {
      out += c;
      prevDash = false;
    } else if (wantsDash) {
      if (out.length() == 0 || prevDash) continue;
      out += '-';
      prevDash = true;
    }
  }

  while (out.length() > 0 && out[out.length() - 1] == '-') {
    out.remove(out.length() - 1);
  }

  return out;
}

bool isValidHostname(const String& hn) {
  if (hn.length() == 0 || hn.length() > 20) return false;
  if (hn[0] == '-' || hn[hn.length() - 1] == '-') return false;

  for (size_t i = 0; i < hn.length(); i++) {
    char c = hn[i];
    const bool isAlpha = (c >= 'a' && c <= 'z');
    const bool isDigit = (c >= '0' && c <= '9');
    if (!(isAlpha || isDigit || c == '-')) return false;
  }
  return true;
}

void wifiRecoverySetup() {
  if (gWifiRecoveryInitialized) return;
  gWifiRecoveryInitialized = true;

  // Haal eerst de laatste diagnostische gebeurtenis van de vorige sessie op.
  // Daarna zetten we voor deze nieuwe sessie een neutrale marker. Eén NVS-write
  // per boot is bewust beperkt en voorkomt dat oude WiFi-fouten als "vorige sessie"
  // blijven staan wanneer de huidige sessie zonder WiFi-storing eindigt.
  loadPreviousWifiSessionEvent();
  persistWifiSessionEvent("SESSION_STARTED", 0, 0);

  WiFi.onEvent(wifiEventHandler);
}

void wifiRecoveryTick() {
  if (!gWifiRecoveryInitialized) return;

  const uint32_t nowMs = millis();

  bool gotIpPending = false;
  bool disconnectPending = false;
  portENTER_CRITICAL(&gWifiEventMux);
  gotIpPending = gWifiGotIpEventPending;
  disconnectPending = gWifiDisconnectEventPending;
  gWifiGotIpEventPending = false;
  gWifiDisconnectEventPending = false;
  portEXIT_CRITICAL(&gWifiEventMux);

  if (gotIpPending) {
    applyWifiNoPowerSave();
    if (!configMode) {
      markWifiRecovered();
      // WiFi kan na een storing hetzelfde of een nieuw IP krijgen. Publiceer
      // mDNS opnieuw vanuit de hoofd-loop zodat discovery altijd terugkomt.
      if (gMdnsConfigured) publishMdnsServices();
    }
  }

  if (disconnectPending && !configMode) {
    startWifiOutageIfNeeded(nowMs);
  }

  // Stap 4 geldt uitsluitend voor normale RUN/STA-modus.
  // Config-AP en de bestaande installatieflow worden in step308 niet aangepast.
  if (configMode) return;

  if (WiFi.status() == WL_CONNECTED) {
    markWifiRecovered();
    return;
  }

  // Vangt ook een statuswijziging op wanneer er onverhoopt geen event is gezien.
  startWifiOutageIfNeeded(nowMs);

  if ((int32_t)(nowMs - gWifiNextReconnectMs) < 0) return;

  gWifiReconnectCount++;
  gWifiReconnectAttemptsCurrentOutage++;
  gWifiLastEvent = "WIFI_RECONNECT_ATTEMPT";

  // Slechts één persistente write wanneer een storing lang genoeg duurt om de
  // eerste actieve reconnect-poging te bereiken. Geen flash-write per retry.
  if (gWifiReconnectAttemptsCurrentOutage == 1) {
    persistWifiSessionEvent("WIFI_RECOVERY_ACTIVE", wifiLastDisconnectReasonCode(),
                            gWifiReconnectAttemptsCurrentOutage);
  }

  WiFi.reconnect();

  size_t backoffIndex = 0;
  if (gWifiReconnectAttemptsCurrentOutage > 1) {
    backoffIndex = (size_t)(gWifiReconnectAttemptsCurrentOutage - 1);
  }
  if (backoffIndex >= WIFI_RECONNECT_BACKOFF_COUNT) {
    backoffIndex = WIFI_RECONNECT_BACKOFF_COUNT - 1;
  }
  gWifiNextReconnectMs = nowMs + WIFI_RECONNECT_BACKOFF_MS[backoffIndex];
}

uint32_t wifiDisconnectCount() {
  uint32_t value;
  portENTER_CRITICAL(&gWifiEventMux);
  value = gWifiDisconnectCount;
  portEXIT_CRITICAL(&gWifiEventMux);
  return value;
}

uint32_t wifiReconnectCount() {
  return gWifiReconnectCount;
}

uint32_t wifiReconnectSuccessCount() {
  return gWifiReconnectSuccessCount;
}

uint32_t wifiReconnectAttemptsCurrentOutage() {
  return gWifiReconnectAttemptsCurrentOutage;
}

uint8_t wifiLastDisconnectReasonCode() {
  uint8_t value;
  portENTER_CRITICAL(&gWifiEventMux);
  value = gWifiLastDisconnectReasonCode;
  portEXIT_CRITICAL(&gWifiEventMux);
  return value;
}

String wifiLastDisconnectReason() {
  return wifiReasonTextFromCode(wifiLastDisconnectReasonCode());
}

String wifiStatusText() {
  switch (WiFi.status()) {
    case WL_IDLE_STATUS:     return "IDLE";
    case WL_NO_SSID_AVAIL:   return "NO_SSID";
    case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
    case WL_CONNECTED:       return "CONNECTED";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED:    return "DISCONNECTED";
    default:                 return "UNKNOWN";
  }
}

String wifiPowerSaveModeText() {
  wifi_ps_type_t mode;
  if (esp_wifi_get_ps(&mode) != ESP_OK) return "UNKNOWN";
  switch (mode) {
    case WIFI_PS_NONE:      return "NONE";
    case WIFI_PS_MIN_MODEM: return "MIN_MODEM";
    case WIFI_PS_MAX_MODEM: return "MAX_MODEM";
    default:                return "UNKNOWN";
  }
}

String wifiLastEventText() {
  return gWifiLastEvent;
}

String wifiPreviousSessionEvent() {
  return gWifiPreviousSessionEvent;
}

String wifiPreviousSessionDisconnectReason() {
  return wifiReasonTextFromCode(gWifiPreviousSessionReasonCode);
}

uint32_t wifiPreviousSessionReconnectAttempts() {
  return gWifiPreviousSessionReconnectAttempts;
}

uint32_t wifiPreviousSessionEventUptimeS() {
  return gWifiPreviousSessionEventUptimeS;
}

bool connectWiFiSTA(const String& wifiSsid, const String& wifiPass) {
  if (wifiSsid.length() == 0) return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(500);
    retries++;
  }

  const bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected) applyWifiNoPowerSave();
  return connected;
}

void startConfigAP(bool& configMode) {
  // step34 WIFI_AP_VISIBLE
  // Doel: setup-AP moet ALTIJD zichtbaar zijn in WiFi-lijsten (geen hidden SSID)
  // en op een breed compatibel kanaal (1) staan.
  // Dit voorkomt situaties waarin de firmware wel draait, maar het SSID niet zichtbaar is.

  configMode = true;

  // Hard reset van eerdere WiFi-state (zeker na OTA / mislukte config).
  // Dit is een bewuste disconnect en mag niet als runtime-storing worden geteld.
  portENTER_CRITICAL(&gWifiEventMux);
  gWifiIgnoreDisconnectUntilMs = millis() + 1000UL;
  portEXIT_CRITICAL(&gWifiEventMux);
  WiFi.disconnect(true, true);
  WiFi.softAPdisconnect(true);
  delay(50);

  // Zet landcode expliciet (EU/NL) om kanaal/scan verschillen te vermijden
  esp_wifi_set_country_code("NL", true);

  // AP-only setup mode
  WiFi.mode(WIFI_AP);
  delay(50);

  uint32_t chipId = (uint32_t)ESP.getEfuseMac();
  char apName[32];
  snprintf(apName, sizeof(apName), "SolarController-%04X", (uint16_t)(chipId & 0xFFFF));

  // Force: kanaal 1 + zichtbaar SSID
  WiFi.softAP(apName, nullptr, 1, 0);
}

void setupTimeNtp() {
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org", "time.nist.gov", "time.google.com");
}

bool startMdnsHttp(const String& deviceName) {
  // Ook met een lege apparaatnaam blijft mDNS vanaf step309 actief. In dat
  // geval wordt een stabiele fallback-hostname gebruikt; handmatige .local
  // namen blijven ongewijzigd werken.
  gMdnsRequestedDeviceName = deviceName;
  gMdnsConfigured = true;
  return publishMdnsServices();
}

void handleWifiScanJson() {
  // Scan op aanvraag (geen continue scans). Werkt zowel in STA als AP-mode.
  // Let op: hidden SSID's worden niet getoond -> handmatige invoer blijft mogelijk.
  const uint8_t MAX_NETS = 20;

  wifi_mode_t prevMode = WiFi.getMode();
  if (prevMode == WIFI_AP) {
    WiFi.mode(WIFI_AP_STA);
    delay(20);
  }

  int n = WiFi.scanNetworks(false, true);
  StaticJsonDocument<2500> doc;
  JsonArray arr = doc.createNestedArray("networks");

  struct NetItem { String ssid; int32_t rssi; wifi_auth_mode_t enc; };
  NetItem nets[MAX_NETS];
  uint8_t used = 0;

  if (n > 0) {
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      s.trim();
      if (s.length() == 0) continue;
      int32_t r = WiFi.RSSI(i);
      wifi_auth_mode_t e = WiFi.encryptionType(i);

      int found = -1;
      for (uint8_t k = 0; k < used; k++) {
        if (nets[k].ssid == s) { found = k; break; }
      }
      if (found >= 0) {
        if (r > nets[found].rssi) {
          nets[found].rssi = r;
          nets[found].enc = e;
        }
      } else if (used < MAX_NETS) {
        nets[used++] = { s, r, e };
      }
    }

    for (uint8_t a = 0; a < used; a++) {
      for (uint8_t b = a + 1; b < used; b++) {
        if (nets[b].rssi > nets[a].rssi) {
          NetItem tmp = nets[a];
          nets[a] = nets[b];
          nets[b] = tmp;
        }
      }
    }

    for (uint8_t k = 0; k < used; k++) {
      JsonObject o = arr.createNestedObject();
      o["ssid"] = nets[k].ssid;
      o["rssi"] = nets[k].rssi;

      const char* encStr = "OPEN";
      switch (nets[k].enc) {
        case WIFI_AUTH_OPEN: encStr = "OPEN"; break;
        case WIFI_AUTH_WEP: encStr = "WEP"; break;
        case WIFI_AUTH_WPA_PSK: encStr = "WPA"; break;
        case WIFI_AUTH_WPA2_PSK: encStr = "WPA2"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: encStr = "WPA/WPA2"; break;
        case WIFI_AUTH_WPA2_ENTERPRISE: encStr = "WPA2-ENT"; break;
        case WIFI_AUTH_WPA3_PSK: encStr = "WPA3"; break;
        case WIFI_AUTH_WPA2_WPA3_PSK: encStr = "WPA2/WPA3"; break;
        default: encStr = "SEC"; break;
      }
      o["enc"] = encStr;
    }
  }

  WiFi.scanDelete();
  if (prevMode == WIFI_AP) {
    WiFi.mode(WIFI_AP);
  }

  String out;
  out.reserve(900);
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "application/json", out);
}

void registerNetworkRoutes(WebServer& server) {
  server.on("/api/wifi_scan", HTTP_GET, handleWifiScanJson);
}
