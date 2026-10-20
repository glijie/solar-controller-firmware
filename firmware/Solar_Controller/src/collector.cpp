#include "collector.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <time.h>

// ---------------------------
// Configuratie van de collector
// ---------------------------
// Endpoint van de Cloudflare Worker:
static const char* COLLECTOR_URL = "https://flat-firefly-9d53.p-blijie.workers.dev/ping";

// Publieke apparaatcode voor /ping; /stats gebruikt een aparte private beheerderssleutel.
static const char* COLLECTOR_KEY = "SOLAR_CONTROLLER_SECRET_1";

// ---------------------------
// Intern
// ---------------------------
static bool g_collectorInit = false;
static bool g_sentThisBoot  = false;
static uint32_t g_bootMs    = 0;

// Dagelijkse melding met persistent last_sent.
// Bij opstarten wordt ook een melding verstuurd.
// Later kunnen we de boot-ping uitschakelen door COLLECTOR_SEND_ON_BOOT=false.
static const bool COLLECTOR_SEND_ON_BOOT = true;

static Preferences g_prefs;
static uint32_t g_lastSentEpoch = 0;   // UTC epoch seconds (alleen geldig als tijd gesynct is)
static uint32_t g_nextTryMs     = 0;   // retry moment bij mislukte send

static const uint32_t DAY_SEC   = 24u * 60u * 60u;
static const uint32_t RETRY_MS  = 10u * 60u * 1000u; // 10 min

static uint32_t nowEpoch() {
  // time(nullptr) == 0/klein wanneer NTP nog niet is gesynct.
  time_t t = time(nullptr);
  if (t < 1577836800) return 0; // < 2020-01-01 => onbetrouwbaar
  return (uint32_t)t;
}

static String buildSerialFromMac() {
  // Unieke serial afgeleid van ESP32 base MAC (efuse) – volledige 6 bytes
  // Voorbeeld: SC-8857218F5F48
  uint64_t mac = ESP.getEfuseMac(); // 48-bit MAC in de laagste bits
  uint8_t b0 = (mac >> 40) & 0xFF;
  uint8_t b1 = (mac >> 32) & 0xFF;
  uint8_t b2 = (mac >> 24) & 0xFF;
  uint8_t b3 = (mac >> 16) & 0xFF;
  uint8_t b4 = (mac >>  8) & 0xFF;
  uint8_t b5 = (mac >>  0) & 0xFF;

  char buf[32];
  snprintf(buf, sizeof(buf), "SC-%02X%02X%02X%02X%02X%02X", b0,b1,b2,b3,b4,b5);
  return String(buf);
}

String collectorGetSerial() {
  // Small helper so the main UI (.ino) can display the same serial we send
  // to the Cloudflare collector.
  return buildSerialFromMac();
}

static bool sendPingNow() {
  if (WiFi.status() != WL_CONNECTED) return false;

  // TLS: in deze stap gebruiken we "insecure" voor maximale compatibiliteit.
  // (Volgende stap kunnen we root CA pinnen als je dat wilt.)
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(3000); // 3s max blokkeren (zo minimaal mogelijk)

  if (!http.begin(client, COLLECTOR_URL)) {
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-SC-KEY", COLLECTOR_KEY);

  extern const char* FW_VERSION; // uit .ino
  String payload = String("{\"serial\":\"") + buildSerialFromMac() +
                   "\",\"firmware\":\"" + String(FW_VERSION) + "\"}";

  int code = http.POST(payload);
  http.end();

  return (code >= 200 && code < 300);
}

void collectorSetup() {
  g_collectorInit = true;
  g_sentThisBoot  = false;
  g_bootMs        = millis();

  // Persistente storage voor last_sent (Stap 2A)
  // Namespace: "collector" (klein en geïsoleerd)
  g_prefs.begin("collector", false);
  g_lastSentEpoch = g_prefs.getUInt("last_sent", 0);
  g_nextTryMs     = millis();
}

void collectorLoop() {
  if (!g_collectorInit) return;
  const uint32_t nowMs = millis();

  // Geef de WiFi-stack even tijd na connect (voorkomt rare first-request issues)
  if ((nowMs - g_bootMs) < 5000) return;

  // 1) Boot-ping (voor testen voorlopig AAN)
  if (COLLECTOR_SEND_ON_BOOT && !g_sentThisBoot) {
    if (sendPingNow()) {
      g_sentThisBoot = true;

      // Als tijd gesynct is, telt dit meteen als 'dagelijkse' send.
      uint32_t epoch = nowEpoch();
      if (epoch > 0) {
        g_lastSentEpoch = epoch;
        g_prefs.putUInt("last_sent", g_lastSentEpoch);
      }
    } else {
      // Boot-ping mislukte: probeer later opnieuw via dagelijkse retry
    }

    // Geen dubbele call in dezelfde loop-iteratie
    return;
  }

  // 2) Dagelijkse ping (1x per 24h, persistent)
  const uint32_t epoch = nowEpoch();
  if (epoch == 0) {
    // Tijd nog niet bekend: we kunnen niet betrouwbaar 24h meten.
    // (Boot-ping dekt voorlopig de testcase.)
    return;
  }

  if (g_lastSentEpoch > 0 && (epoch - g_lastSentEpoch) < DAY_SEC) {
    return; // nog niet aan de beurt
  }

  if (nowMs < g_nextTryMs) {
    return; // wacht op retry moment
  }

  if (sendPingNow()) {
    g_lastSentEpoch = epoch;
    g_prefs.putUInt("last_sent", g_lastSentEpoch);
    g_nextTryMs = nowMs + RETRY_MS; // niet nodig, maar voorkomt storm bij edge-cases
  } else {
    // Mislukt: over 10 minuten opnieuw proberen
    g_nextTryMs = nowMs + RETRY_MS;
  }
}
