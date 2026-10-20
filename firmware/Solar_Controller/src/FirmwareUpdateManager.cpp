#include "FirmwareUpdateManager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "ApiRoutes.h"
#include "WebRoutes.h"
#include "app_globals.h"

// ----- Globals defined in the main .ino -----
extern WebServer server;
extern Preferences prefs;
extern bool uiDarkMode;
extern bool fwAutoUpdateEnabled;

// Shared OTA endpoints/settings now owned by this module.
static const char* FW_VERSION_URL = "https://github.com/glijie/solar-controller-firmware/releases/latest/download/version.json";
static const char* FW_BIN_URL     = "https://github.com/glijie/solar-controller-firmware/releases/latest/download/SolarController.bin";
static const char* PREF_FW_AUTO   = "fw_auto";
static const unsigned long FW_AUTO_CHECK_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL; // 6 uur

// Runtime state kept local to the firmware-update module.
static bool fwUpdateAvailable   = false;
static bool fwUpdateTaskPending = false; // set via UI "Update nu" of auto-update
static bool fwUpdateTaskRunning = false; // FreeRTOS task actief / coop running
static TaskHandle_t fwUpdateTaskHandle = nullptr;
// Cooperative (non-RTOS-task) GitHub OTA downloader/installer.
// This avoids xTaskCreate() failures on low-heap situations and keeps behaviour deterministic.
static bool       fwOtaCoopActive   = false;
static HTTPClient fwOtaHttp;
static WiFiClient *fwOtaStream      = nullptr;
static int        fwOtaHttpCode     = 0;
static unsigned long fwOtaLastByteMs = 0;

// Debug/telemetry for OTA start (helps diagnose UI vs API issues)
static uint32_t fwUpdateStartCount = 0;
static String   fwUpdateLastStartIso = "";
static String   fwUpdateLastStartVia = ""; // "ui" or "api"

// Progress state (voor UI voortgang)
static bool   fwUpdateInProgress = false;
static uint8_t  fwUpdatePercent = 0;
static uint32_t fwUpdateBytes = 0;
static uint32_t fwUpdateTotal = 0; // 0 = unknown
static String   fwUpdateStatusMsg = "";
static bool   fwUpdateDone = false;
static bool   fwUpdateSuccess = false;
static bool   fwUpdateRebootPending = false; // UI: reboot imminent after successful FW update
static String fwLatestVersion = "";
static String fwLatestNotes = "";
static String fwLastCheckIso = "";
static String fwLastError = "";
static unsigned long fwLastCheckMs = 0;

static bool fwUrlsConfigured();
void handleFwUpdateNowUi();

static String normalizeVersion(String s) {
  // Maak versie-strings vergelijkbaar:
  // - accepteert "v2026.02.09.07", "v.2025.12.22.01", "2025.12.22.01"
  // - trimt spaties en verwijdert niet-numerieke prefix
  s.trim();
  // verwijder eventuele leading 'v' en punt/underscore/etc. tot eerste cijfer
  while (s.length() && !isDigit(s[0])) {
    s.remove(0, 1);
  }
  return s;
}

static int parseVersionPart(const String& s, int idx) {
  // idx 0..N: split by '.'
  int partIndex = 0;
  int start = 0;
  for (int i = 0; i <= s.length(); i++) {
    if (i == s.length() || s[i] == '.') {
      if (partIndex == idx) {
        String sub = s.substring(start, i);
        sub.trim();
        return sub.toInt();
      }
      partIndex++;
      start = i + 1;
    }
  }
  return 0;
}

static bool isVersionNewer(const String& latest, const String& current) {
  // Vergelijk numeriek per deel: 2025.12.21.10 > 2025.12.21.7
  String L = normalizeVersion(latest);
  String C = normalizeVersion(current);
  for (int i = 0; i < 6; i++) { // max 6 delen is ruim zat
    int a = parseVersionPart(L, i);
    int b = parseVersionPart(C, i);
    if (a > b) return true;
    if (a < b) return false;
  }
  return false;
}

static String isoNowLocal() {
  // Simpele timestamp voor UI (geen echte timezone conversie nodig voor puur status)
  // Format: YYYY-MM-DD HH:MM:SS
  time_t now = time(nullptr);
  struct tm tmNow;
  localtime_r(&now, &tmNow);
  char buf[24];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday,
           tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
  return String(buf);
}

static bool fetchLatestFirmwareInfo() {
  fwLastError = "";
  fwLatestVersion = "";
  fwLatestNotes = "";
  fwUpdateAvailable = false;

  // Basic URL guard (zodat iemand niet vergeet de placeholders te vervangen)
  if (String(FW_VERSION_URL).indexOf("<USER>") >= 0 || String(FW_VERSION_URL).indexOf("<REPO>") >= 0) {
    fwLastError = "FW_VERSION_URL nog niet ingevuld (placeholders <USER>/<REPO>).";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure(); // eenvoud: GitHub cert validatie overslaan (werkt stabiel in practice)
  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  // cache-bust zodat GitHub/CDN nooit een oude redirect/body cached
  String vurl = String(FW_VERSION_URL) + "?t=" + String(millis());

  if (!https.begin(client, vurl.c_str())) {
    fwLastError = "HTTPS begin() failed (version.json)";
    return false;
  }

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    fwLastError = "version.json HTTP " + String(httpCode);
    https.end();
    fwUpdateInProgress = false; fwUpdateDone = true; fwUpdateSuccess = false; fwUpdateStatusMsg = "Update mislukt";
    return false;
  }

  String payload = https.getString();
  https.end();

  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    fwLastError = "version.json JSON parse error";
    return false;
  }

  fwLatestVersion = String(doc["version"] | "");
  fwLatestNotes   = String(doc["notes"]   | "");

  if (fwLatestVersion.length() == 0) {
    fwLastError = "version.json mist veld 'version'";
    return false;
  }

  fwUpdateAvailable = isVersionNewer(fwLatestVersion, String(FW_VERSION)); // FW_VERSION bestaat al in je project
  return true;
}

static bool performFirmwareUpdateFromGitHub() {
  // Download firmware.bin van GitHub en schrijf naar flash met zichtbare voortgang.
  fwLastError = "";

  if (String(FW_BIN_URL).indexOf("<USER>") >= 0 || String(FW_BIN_URL).indexOf("<REPO>") >= 0) {
    fwLastError = "FW_BIN_URL nog niet ingevuld (placeholders <USER>/<REPO>).";
    fwUpdateInProgress = false; fwUpdateDone = true; fwUpdateSuccess = false; fwUpdateStatusMsg = "Update mislukt";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!https.begin(client, FW_BIN_URL)) {
    fwLastError = "HTTPS begin() failed (firmware.bin)";
    fwUpdateInProgress = false; fwUpdateDone = true; fwUpdateSuccess = false; fwUpdateStatusMsg = "Update mislukt";
    return false;
  }

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    fwLastError = "firmware.bin HTTP " + String(httpCode);
    https.end();
    fwUpdateInProgress = false; fwUpdateDone = true; fwUpdateSuccess = false; fwUpdateStatusMsg = "Update mislukt";
    return false;
  }

  int contentLength = https.getSize();         // kan -1 zijn (unknown / chunked)
  WiFiClient* stream = https.getStreamPtr();

  // Reset progress state
  fwUpdateInProgress = true;
  fwUpdateDone = false;
  fwUpdateSuccess = false;
  fwUpdatePercent = 0;
  fwUpdateBytes = 0;
  fwUpdateTotal = (contentLength > 0) ? (uint32_t)contentLength : 0;
  fwUpdateStatusMsg = "Update gestart (downloaden)...";

  // Begin update (unknown size werkt ook)
  if (!Update.begin((contentLength > 0) ? (size_t)contentLength : UPDATE_SIZE_UNKNOWN)) {
    fwLastError = "Update.begin failed";
    https.end();
    fwUpdateInProgress = false; fwUpdateDone = true; fwUpdateSuccess = false; fwUpdateStatusMsg = "Update mislukt";
    return false;
  }

  // Handmatige stream loop (i.p.v. Update.writeStream) zodat we ook bij unknown Content-Length voortgang kunnen tonen.
  const size_t BUFSZ = 4096;
  uint8_t buf[BUFSZ];

  unsigned long lastUiMs = 0;
  uint32_t lastPct = 0;

  while (https.connected()) {
    size_t avail = stream->available();
    if (avail) {
      size_t toRead = (avail > BUFSZ) ? BUFSZ : avail;
      int r = stream->readBytes((char*)buf, toRead);
      if (r <= 0) break;

      size_t w = Update.write(buf, (size_t)r);
      if (w != (size_t)r) {
        fwLastError = "Update.write failed";
        Update.end();
        https.end();
        fwUpdateInProgress = false; fwUpdateDone = true; fwUpdateSuccess = false; fwUpdateStatusMsg = "Update mislukt";
        return false;
      }

      fwUpdateBytes += (uint32_t)r;

      // UI update (max ~5x/sec)
      unsigned long nowMs = millis();
      if (nowMs - lastUiMs >= 200) {
        lastUiMs = nowMs;

        if (fwUpdateTotal > 0) {
          uint32_t pct = (uint32_t)((fwUpdateBytes * 100ULL) / fwUpdateTotal);
          if (pct > 100) pct = 100;
          if (pct != lastPct) {
            lastPct = pct;
            fwUpdatePercent = (uint8_t)pct;
          }
          fwUpdateStatusMsg = "Installeren... " + String(fwUpdatePercent) + "%";
        } else {
          // Unknown total: toon KB geschreven, en laat de UI niet op 0% "hangen"
          fwUpdatePercent = 0;
          fwUpdateStatusMsg = "Installeren... " + String((uint32_t)(fwUpdateBytes / 1024UL)) + " KB";
        }
        delay(1); // yield / WDT
      }
    } else {
      // geen data beschikbaar -> even wachten
      if (!stream->connected()) break;
      delay(1);
    }

    // Als contentLength bekend is, kunnen we stoppen wanneer alles binnen is.
    if (fwUpdateTotal > 0 && fwUpdateBytes >= fwUpdateTotal) break;
  }

  if (!Update.end(true)) {
    fwLastError = "Update.end failed";
    https.end();
    fwUpdateInProgress = false; fwUpdateDone = true; fwUpdateSuccess = false; fwUpdateStatusMsg = "Update mislukt";
    return false;
  }

  if (!Update.isFinished()) {
    fwLastError = "Update not finished";
    https.end();
    fwUpdateInProgress = false; fwUpdateDone = true; fwUpdateSuccess = false; fwUpdateStatusMsg = "Update mislukt";
    return false;
  }

  fwUpdatePercent = 100;
  fwUpdateStatusMsg = "Update klaar";
  fwUpdateInProgress = false;
  fwUpdateDone = true;
  fwUpdateSuccess = true;

  https.end();
  return true;
}

// Call from loop() when fwUpdateTaskPending is set
static void fwUpdateTaskFn(void* pv) {
  (void)pv;
  fwUpdateTaskRunning = true;

  // Extra veiligheid: opnieuw checken vlak voor install.
  bool ok = fetchLatestFirmwareInfo();
  fwLastCheckIso = isoNowLocal();
  fwLastCheckMs  = millis();

  if (ok && fwUpdateAvailable) {
    fwUpdateStatusMsg = "Downloaden en installeren…";
    bool updOk = performFirmwareUpdateFromGitHub();
    if (updOk) {
      fwUpdateStatusMsg = "Herstart…";
      delay(800);
      ESP.restart();
    }
  }

  fwUpdateTaskRunning = false;
  fwUpdateTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

// Call from loop() when fwUpdateTaskPending is set

// Start GitHub OTA task safely (keeps webserver responsive).
// Returns true when a task is running (started or already running), false when start failed.
bool startFirmwareUpdateTaskNow() {
  // Backwards compatible name, but now starts a cooperative (non-task) OTA update.
  // If already running, don't start again.
  if (fwOtaCoopActive) {
    fwUpdateStatusMsg = "Update loopt al…";
    fwUpdateInProgress = true;
    fwUpdateDone = false;
    fwUpdateSuccess = false;
    return true;
  }

  // WiFi is required for GitHub/HTTP downloads.
  if (WiFi.status() != WL_CONNECTED) {
    fwLastError = "Geen WiFi verbinding";
    fwUpdateStatusMsg = "WiFi niet verbonden";
    fwUpdateInProgress = false;
    fwUpdateDone = true;
    fwUpdateSuccess = false;
    return false;
  }

  fwLastError = "";
  fwUpdateInProgress = true;
  fwUpdateDone = false;
  fwUpdateSuccess = false;
  fwUpdatePercent = 0;
  fwUpdateBytes = 0;
  fwUpdateTotal = 0;
  fwUpdateStatusMsg = "Verbinden met GitHub…";

  // Make sure any previous client is closed
  fwOtaHttp.end();
  fwOtaStream = nullptr;
  fwOtaHttpCode = 0;

  // Cache-bust the BIN URL (some gateways/proxies cache aggressively)
  String url = String(FW_BIN_URL) + "?cb=" + String(millis());

  fwOtaHttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  fwOtaHttp.setUserAgent("SolarController");
  fwOtaHttp.begin(url);

  fwOtaHttpCode = fwOtaHttp.GET();
  if (fwOtaHttpCode != HTTP_CODE_OK) {
    fwLastError = String("BIN HTTP ") + String(fwOtaHttpCode);
    fwUpdateInProgress = false;
    fwUpdateDone = true;
    fwUpdateSuccess = false;
    fwUpdateStatusMsg = "Update mislukt";
    fwOtaHttp.end();
    return false;
  }

  int len = fwOtaHttp.getSize();
  if (len <= 0) {
    // GitHub should give Content-Length for release assets; without it, OTA is unreliable.
    fwLastError = "BIN content-length ontbreekt";
    fwUpdateInProgress = false;
    fwUpdateDone = true;
    fwUpdateSuccess = false;
    fwUpdateStatusMsg = "Update mislukt";
    fwOtaHttp.end();
    return false;
  }

  // Start flash update
  if (!Update.begin((size_t)len)) {
    fwLastError = String("Update.begin failed (err ") + String(Update.getError()) + ")";
    fwUpdateInProgress = false;
    fwUpdateDone = true;
    fwUpdateSuccess = false;
    fwUpdateStatusMsg = "Update mislukt";
    fwOtaHttp.end();
    return false;
  }

  fwOtaStream = fwOtaHttp.getStreamPtr();
  fwOtaCoopActive = true;
  fwUpdateTaskRunning = true;    // keep field for UI/debug; now means "coop running"
  fwUpdateTaskHandle = nullptr;  // not used anymore
  fwOtaLastByteMs = millis();

  fwUpdateTotal = (size_t)len;
  fwUpdateStatusMsg = "Downloaden…";
  return true;
}

void processFirmwareUpdateTask() {
  // If a cooperative OTA is active, keep pumping bytes.
  if (fwOtaCoopActive && fwOtaStream != nullptr) {
    // Read a small chunk so we don't block the rest of the firmware too long.
    uint8_t buf[2048];
    size_t avail = fwOtaStream->available();
    if (avail > 0) {
      if (avail > sizeof(buf)) avail = sizeof(buf);
      int r = fwOtaStream->readBytes(buf, avail);
      if (r > 0) {
        size_t w = Update.write(buf, (size_t)r);
        fwUpdateBytes += w;
        fwOtaLastByteMs = millis();

        if (fwUpdateTotal > 0) {
          fwUpdatePercent = (int)((fwUpdateBytes * 100UL) / fwUpdateTotal);
          if (fwUpdatePercent > 100) fwUpdatePercent = 100;
        }
        fwUpdateStatusMsg = "Downloaden…";
      }
    }

    // Completion detection
    bool doneByCount = (fwUpdateTotal > 0 && fwUpdateBytes >= fwUpdateTotal);
    bool stalled = (millis() - fwOtaLastByteMs) > 15000UL; // 15s without bytes
    bool disconnected = !fwOtaHttp.connected();

    if (doneByCount) {
      bool ok = Update.end(true);
      fwOtaHttp.end();
      fwOtaStream = nullptr;
      fwOtaCoopActive = false;
      fwUpdateTaskRunning = false;

      fwUpdateDone = true;
      fwUpdateSuccess = ok;
      fwUpdateInProgress = false;

      if (!ok) {
        fwLastError = String("Update.end failed (err ") + String(Update.getError()) + ")";
        fwUpdateStatusMsg = "Update mislukt";
      } else {
        fwUpdateStatusMsg = "Update gereed, herstart…";
        fwUpdateRebootPending = true; // UI: show pending reboot
        // Force final progress state visible for UI before reboot
        if (fwUpdateTotal > 0) {
          fwUpdateBytes = fwUpdateTotal;
          fwUpdatePercent = 100;
        } else {
          fwUpdatePercent = 100;
        }
        fwUpdateDone = true;
        fwUpdateSuccess = true;
        fwUpdateInProgress = false;

        // Give browser/poller time to fetch the final state
        delay(1800);
        ESP.restart();
      }
      return;
    }

    if (stalled && (disconnected || avail == 0)) {
      // Abort on stall/disconnect
      Update.abort();
      fwOtaHttp.end();
      fwOtaStream = nullptr;
      fwOtaCoopActive = false;
      fwUpdateTaskRunning = false;

      fwLastError = "Download stalled/verbinding weg";
      fwUpdateStatusMsg = "Update mislukt";
      fwUpdateInProgress = false;
      fwUpdateDone = true;
      fwUpdateSuccess = false;
      return;
    }

    // Keep main loop responsive
    delay(0);
    return;
  }

  // Start a pending update request (UI/auto)
  if (!fwUpdateTaskPending) return;
  fwUpdateTaskPending = false;
  startFirmwareUpdateTaskNow();
}


// ==================================================
// API handlers voor GitHub OTA update
// ==================================================
void handleFwCheckJson() {
  server.sendHeader("Cache-Control", "no-store, max-age=0");

  bool ok = fetchLatestFirmwareInfo();
  fwLastCheckIso = isoNowLocal();
  fwLastCheckMs = millis();

  StaticJsonDocument<768> doc;
  doc["ok"] = ok;
  doc["installed"] = String(FW_VERSION);
  doc["latest"] = fwLatestVersion;
  doc["available"] = fwUpdateAvailable;
  doc["auto"] = fwAutoUpdateEnabled;
  doc["last_check"] = fwLastCheckIso;
  doc["notes"] = fwLatestNotes;
  doc["error"] = fwLastError;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json; charset=utf-8", out);
}


void handleFwProgressJson() {
  server.sendHeader("Cache-Control", "no-store, max-age=0");

  StaticJsonDocument<768> doc;
  doc["in_progress"] = fwUpdateInProgress;
  doc["pending"] = fwUpdateTaskPending;
  doc["task_running"] = fwUpdateTaskRunning;
  doc["start_count"] = fwUpdateStartCount;
  doc["last_start"] = fwUpdateLastStartIso;
  doc["last_start_via"] = fwUpdateLastStartVia;
  doc["percent"] = fwUpdatePercent;
  doc["bytes"] = fwUpdateBytes;
  doc["total"] = fwUpdateTotal;
  doc["msg"] = fwUpdateStatusMsg;
  doc["done"] = fwUpdateDone;
  doc["success"] = fwUpdateSuccess;
  doc["error"] = fwLastError;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json; charset=utf-8", out);
}


static bool fwUrlsConfigured() {
  String v = String(FW_VERSION_URL);
  String b = String(FW_BIN_URL);
  if (v.length() == 0 || b.length() == 0) return false;
  if (v.indexOf("<USER>") >= 0 || v.indexOf("<REPO>") >= 0) return false;
  if (b.indexOf("<USER>") >= 0 || b.indexOf("<REPO>") >= 0) return false;
  return true;
}


void handleFwStatusJson() {
  server.sendHeader("Cache-Control", "no-store, max-age=0");

  DynamicJsonDocument doc(768);
  doc["wifi_ok"] = (WiFi.status() == WL_CONNECTED);
  doc["ssid"] = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "";
  doc["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  doc["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
  doc["urls_ok"] = fwUrlsConfigured();
  doc["version_url"] = String(FW_VERSION_URL);
  doc["bin_url"] = String(FW_BIN_URL);
  doc["current_version"] = String(FW_VERSION);
  doc["in_progress"] = fwUpdateTaskRunning;
  doc["pending_reboot"] = fwUpdateRebootPending;
  doc["last_error"] = fwLastError;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}


void handleFwUpdateNow() {
  server.sendHeader("Cache-Control", "no-store, max-age=0");

  // Telemetry: record that API requested a start
  fwUpdateStartCount++;
  fwUpdateLastStartIso = isoNowLocal();
  fwUpdateLastStartVia = "api";

  bool started = startFirmwareUpdateTaskNow();

  StaticJsonDocument<256> doc;
  doc["ok"] = true;
  doc["starting"] = started;
  doc["in_progress"] = fwUpdateInProgress;
  doc["msg"] = fwUpdateStatusMsg;
  doc["error"] = fwLastError;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json; charset=utf-8", out);
}

// UI endpoints (zonder JavaScript): doen actie + redirect terug naar /update
// NOTE: Browser navigation uses HTTP GET. The GitHub OTA trigger is intentionally POST only.
// This helper endpoint makes it visible (and debuggable) when someone opens /api/fw/update in a browser.
void handleFwUpdateNowGet() {
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  StaticJsonDocument<256> doc;
  doc["ok"] = false;
  doc["error"] = "Method not allowed (use POST)";
  doc["hint"] = "Start GitHub OTA with HTTP POST to /api/fw/update (or use the 'Update nu' button in Settings).";
  String out;
  serializeJson(doc, out);
  server.send(405, "application/json; charset=utf-8", out);
}


void redirectToUpdatePage() {
  server.sendHeader("Location", "/update");
  server.send(303, "text/plain", "");
}

void handleFwCheckUi() {
  fetchLatestFirmwareInfo();
  fwLastCheckIso = isoNowLocal();
  fwLastCheckMs = millis();
  redirectToUpdatePage();
}

// Some browsers/extensions can trigger a GET navigation; accept it for robustness.
void handleFwUpdateNowUiGet() {
  handleFwUpdateNowUi();
}

void handleFwUpdateNowUi() {
  // Start update task (in loop) and show a progress UI (in dezelfde theme + met bottom navigation).
  fwUpdateStatusMsg = "Update gestart (wachten op taak)…";
  fwUpdateInProgress = true;
  fwUpdateDone = false;
  fwUpdateSuccess = false;
  fwUpdatePercent = 0;
  fwUpdateBytes = 0;
  fwUpdateTotal = 0;

  // Telemetry: record that UI requested a start
  fwUpdateStartCount++;
  fwUpdateLastStartIso = isoNowLocal();
  fwUpdateLastStartVia = "ui";

  bool started = startFirmwareUpdateTaskNow();

  if (!started) {
    // Fallback: keep pending so loop can retry
    fwUpdateTaskPending = true;
    fwUpdateRebootPending = false; // reset reboot flag at start of update
    if (fwLastError.length()) fwUpdateStatusMsg = String("Update start faalde: ") + fwLastError;
  } else {
    fwUpdateTaskPending = false;
    fwUpdateStatusMsg = "Update gestart…";
  }

  server.sendHeader("Cache-Control", "no-store, max-age=0");

  String page = htmlHeader("Online update");
  page += "<style>"
          ".wrap{padding-bottom:88px}"
          ".pbar{height:16px;border-radius:999px;background:rgba(255,255,255,.10);overflow:hidden;border:1px solid rgba(255,255,255,.14)}"
          ".pfill{height:100%;width:0%;background:linear-gradient(180deg,var(--accent2),var(--accent));transition:width .25s ease}"
          ".pfill.indet{width:45%;animation:indet 1.2s ease-in-out infinite}"
          "@keyframes indet{0%{transform:translateX(-120%)}50%{transform:translateX(60%)}100%{transform:translateX(260%)}}"
          ".mono{font-variant-numeric:tabular-nums}"
          "</style>";

  page += "<body><div class='wrap'>";
  page += "<h1>" + String(DEVICE_NAME) + "</h1>";
  page += "<div class='sub'>OTA update</div>";

  page += "<div class='card'><div class='hd'><b>Online update wordt uitgevoerd…</b><span class='tag'>GitHub</span></div><div class='bd'>";
  page += "<div class='help mono' id='msg'>Starten…</div>";
  page += "<div style='height:12px'></div>";
  page += "<div class='pbar'><div class='pfill' id='fill'></div></div>";
  page += "<div style='height:10px'></div>";
  page += "<div class='row'><div class='muted'>Voortgang</div><div id='pct' class='mono' style='font-weight:900'>0%</div></div>";
  page += "<div class='muted mono' id='bytes' style='margin-top:6px;opacity:.75'></div>";
  page += "<div style='height:14px'></div>";
  page += "<div class='help'>Na afronden herstart de Solar Controller automatisch. Je wordt daarna teruggebracht naar het dashboard.</div>";
  page += "<div style='height:14px'></div>";
  page += "<button class='btn dark' onclick='location.href=\"/\"'>Terug naar dashboard</button>";
  page += "</div></div>";

  page += "</div>";
  page += navHtml("set");

  page += R"JS(<script>
let reconnecting=false;

function fmtBytes(n){
  try{
    n = Number(n||0);
    if(n < 1024) return n + " B";
    if(n < 1024*1024) return (n/1024).toFixed(1) + " KB";
    return (n/1024/1024).toFixed(2) + " MB";
  }catch(e){ return ""; }
}

async function tryReconnect(){
  if(reconnecting) return;
  reconnecting=true;
  document.getElementById('msg').textContent='Herstart… wachten tot device terug online is';
  let tries=0;
  while(tries<120){
    try{
      let r=await fetch('/api/ping',{cache:'no-store'});
      if(r.ok){ location.href='/'; return; }
    }catch(e){}
    await new Promise(res=>setTimeout(res,1000));
    tries++;
  }
  document.getElementById('msg').textContent='Kan device nog niet bereiken. Open straks het dashboard handmatig.';
}

async function poll(){
  try{
    let r=await fetch('/api/fw/progress',{cache:'no-store'});
    let j=await r.json();

    let pct = (j.percent!==undefined)? Number(j.percent) : 0;
    let bytes = (j.bytes!==undefined)? Number(j.bytes) : 0;
    let total = (j.total!==undefined)? Number(j.total) : 0;

    const fill = document.getElementById('fill');
    const pctEl = document.getElementById('pct');
    const bytesEl = document.getElementById('bytes');

    // Theme-consistente status tekst
    document.getElementById('msg').textContent = j.msg || (j.in_progress?'Bezig…':'Starten…');

    if(total > 0){
      // Normale progressbar
      fill.classList.remove('indet');
      fill.style.transform = 'translateX(0)';
      fill.style.width = pct + '%';
      pctEl.textContent = pct + '%';
      bytesEl.textContent = fmtBytes(bytes) + ' / ' + fmtBytes(total);
    } else {
      // Unknown total: toon indeterminate bar + bytes
      fill.classList.add('indet');
      fill.style.width = '45%';
      pctEl.textContent = '…';
      bytesEl.textContent = bytes ? ('Ontvangen: ' + fmtBytes(bytes)) : '';
    }

    if(j.done){
      if(j.success){
        document.getElementById('msg').textContent='Update klaar. Herstart…';
        setTimeout(tryReconnect, 900);
        return;
      } else {
        document.getElementById('msg').textContent='Update mislukt: ' + (j.error||'');
        return;
      }
    }
  }catch(e){
    // Tijdens reboot gaat fetch falen → probeer reconnect flow
    setTimeout(tryReconnect, 900);
    return;
  }
  setTimeout(poll, 650);
}
poll();
</script></body></html>)JS";

  server.send(200, "text/html; charset=utf-8", page);
}


void handleFwSetAutoUi() {
  // Checkbox posts only when checked
  bool en = server.hasArg("enabled");
  fwAutoUpdateEnabled = en;

prefs.begin("boilerbox", false);
  prefs.putBool(PREF_FW_AUTO, fwAutoUpdateEnabled);
  prefs.end();

  redirectToUpdatePage();
}



// OTA
void handleUpdatePage() {
  server.sendHeader("Cache-Control", "no-store, max-age=0");

  String page =
    String("<!doctype html><html><head><meta charset='utf-8'>") +
    "<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1'>" +
    "<title>" + String(DEVICE_NAME) + " OTA</title>" +
    "<style>" + htmlCss() +
      ".wrap{padding-bottom:88px}" // keep content above bottom nav
      ".file{margin:10px 0}"
      ".row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}"
      ".autoRow{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-top:10px}"
      ".autoRow .autoText{flex:1}"
      ".kv{display:flex;gap:10px;flex-wrap:wrap;margin:8px 0}"
      ".kv b{min-width:110px;display:inline-block}"
      ".small{opacity:.85;font-size:.92rem}"
      ".btn2{display:inline-flex;align-items:center;justify-content:center;gap:8px;padding:10px 14px;border-radius:14px;border:1px solid rgba(0,0,0,.10);background:linear-gradient(180deg,var(--accent2),var(--accent));color:#fff;font-size:14px;font-weight:900;cursor:pointer;box-shadow:0 6px 14px rgba(0,0,0,.08)}"
      ".btn2.ghost{background:#fff;color:var(--text);border:1px solid rgba(0,0,0,.14);box-shadow:none}"
      "body.dark .btn2.ghost{background:rgba(26,34,29,.92);color:var(--text);border:1px solid rgba(232,241,236,.18);box-shadow:none}"
      ".btn2:active{transform:translateY(1px)}"
      ".btn2:disabled{opacity:.45;cursor:not-allowed;transform:none}"
      ".pill{display:inline-flex;align-items:center;gap:6px;padding:6px 10px;border-radius:999px;border:1px solid rgba(255,255,255,.12);background:rgba(255,255,255,.06);font-weight:800}"
      ".ok{color:#7CFC98}.bad{color:#FF8A8A}.muted{opacity:.75}"
    "</style>" +
    "</head><body" + String(uiDarkMode ? " class='dark'" : "") + "><div class='wrap'>" +
      "<h1>" + String(DEVICE_NAME) + "</h1>" +
      "<div class='sub'>OTA update</div>" +

      // GitHub OTA (pull) card
      "<div class='card'><div class='hd'><b>Online update</b><span class='tag'>GitHub</span></div><div class='bd'>" +
        "<div class='help'>Controleer of er een nieuwere firmware klaarstaat op GitHub. Je kunt handmatig updaten, of automatisch.</div>" +
        "<div class='kv small'>" +
          "<div><b>Installed:</b> " + String(FW_VERSION) + "</div>" +
          "<div><b>Latest:</b> " + (fwLatestVersion.length() ? fwLatestVersion : String("-")) +
            (fwUpdateAvailable ? " <span class='pill ok'>Update</span>" : "") +
          "</div>" +
        "</div>" +
        "<div class='small muted'>Laatste check: " + (fwLastCheckIso.length() ? fwLastCheckIso : String("-")) + "</div>" +
        (fwLatestNotes.length() ? ("<div class='small' style='margin-top:8px'>" + fwLatestNotes + "</div>") : "") +
        "<div class='row' style='margin-top:12px'>" +
          "<form method='POST' action='/fw/check' style='display:inline'><input type='hidden' name='_section' value='fase'><input type='hidden' name='_section' value='gas'><input type='hidden' name='_section' value='nordpool'><input type='hidden' name='_section' value='energy'><button id='btn_check' class='btn2 ghost' type='submit'>Check update</button></form>" +
          "<form method='POST' action='/fw/update' style='display:inline'><button id='btn_update_now' class='btn2' type='submit' " + String(fwUpdateAvailable ? "" : "disabled") + ">Update nu</button></form>" +
          "<span class='pill'>Status: <span class='muted'>" + String(fwLastCheckIso.length() ? (fwUpdateAvailable ? "Update beschikbaar" : "OK") : "-") + "</span></span>" +
        "</div>" +
        "<div id='fw_warn' class='small' style='display:none;margin-top:10px;padding:10px;border-radius:10px;background:rgba(255,165,0,.12);border:1px solid rgba(255,165,0,.35)'></div>" +
        "<form method='POST' action='/fw/auto' class='autoRow'>" +
          "<div class='autoText'><b>Auto update</b> <span class='muted'>(als AAN: bij update direct installeren)</span></div>" +
          "<div>" +
            "<label class='switch " + String(fwAutoUpdateEnabled ? "on" : "") + "'>" +
              "<input id='fw_auto' name='enabled' type='checkbox' value='1' " + String(fwAutoUpdateEnabled ? "checked" : "") + " onchange='this.form.submit()'>" +
              "<span class='slider'></span>" +
            "</label>" +
          "</div>" +
        "</form>" +
      "</div></div>" +

      // OTA upload card
      "<div class='card'><div class='hd'><b>Firmware-update</b><span class='tag'>OTA</span></div><div class='bd'>" +
        "<div class='help'>Kies het <b>.bin</b> bestand en upload. Het systeem reboot daarna automatisch.</div>" +
        "<form method='POST' action='/update' enctype='multipart/form-data'>" +
          "<input class='file' type='file' name='firmware' accept='.bin'>" +
          "<button class='btn' type='submit'>Upload</button>" +
        "</form>" +
      "</div></div>" +

    "</div>" +
    navHtml("set") +
    "</body></html>";

  server.send(200, "text/html; charset=utf-8", page);
}


void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    Update.begin();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    Update.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    Update.end(true);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
  }
}

void firmwareUpdateAutoCheckTick() {
  // Auto-update: periodiek checken en (indien beschikbaar) direct installeren.
  if (!fwAutoUpdateEnabled || !WiFi.isConnected()) return;

  const unsigned long ms = millis();
  if (fwLastCheckMs != 0 && (ms - fwLastCheckMs) <= FW_AUTO_CHECK_INTERVAL_MS) return;

  const bool ok = fetchLatestFirmwareInfo();
  fwLastCheckIso = isoNowLocal();
  fwLastCheckMs = ms;
  if (ok && fwUpdateAvailable) {
    const bool started = startFirmwareUpdateTaskNow();
    if (!started) {
      // Fallback: keep pending so loop can retry
      fwUpdateTaskPending = true;
      if (fwLastError.length()) fwUpdateStatusMsg = String("Update start faalde: ") + fwLastError;
    } else {
      fwUpdateTaskPending = false;
      fwUpdateStatusMsg = "Update gestart…";
    }
  }
}

void registerFirmwareUpdateRoutes(WebServer& serverRef) {
  serverRef.on("/fw/check", HTTP_POST, handleFwCheckUi);
  serverRef.on("/fw/update", HTTP_POST, handleFwUpdateNowUi);
  serverRef.on("/fw/update", HTTP_GET, handleFwUpdateNowUiGet);
  serverRef.on("/fw/auto", HTTP_POST, handleFwSetAutoUi);
  serverRef.on("/update", HTTP_GET, handleUpdatePage);
  serverRef.on("/update", HTTP_POST, []() {
    server.sendHeader("Cache-Control", "no-store, max-age=0");
    server.send(200, "text/html; charset=utf-8",
                htmlAutoReturnAfterReboot("Update OK", "Reboot… (je wordt zo teruggebracht naar het dashboard)", "/"));
    delay(1200);
    ESP.restart();
  }, handleUpdateUpload);

  serverRef.on("/api/fw/check", HTTP_GET, handleFwCheckJson);
  serverRef.on("/api/fw/progress", HTTP_GET, handleFwProgressJson);
  serverRef.on("/api/fw/status", HTTP_GET, handleFwStatusJson);
  serverRef.on("/api/fw/auto", HTTP_POST, handleFwSetAuto);
  serverRef.on("/api/fw/update", HTTP_GET, handleFwUpdateNowGet);
  serverRef.on("/api/fw/update", HTTP_POST, handleFwUpdateNow);
  serverRef.on("/api/ping", HTTP_GET, handleApiPing);
}
