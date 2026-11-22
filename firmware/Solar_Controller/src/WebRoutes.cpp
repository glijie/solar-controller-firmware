#include "WebRoutes.h"
#include <Arduino.h>
#include "DashboardModern2027.h"
#include "DashboardModern2027Artwork.h"
#include "WebPackedAssets.h"

#include <ArduinoJson.h>
#include <WebServer.h>

#include "app_globals.h"

extern WebServer server;
extern String npArea;
extern int npResMin;
extern String npUpdatedAt;

// Handlers still defined in the main .ino; this module only centralises shared Web/UI glue and route registration.
extern void handleDashboard();
extern void handleControlPage();
extern void handleLegionellaPage();
extern void handleSettingsPage();
extern void handleToggleForce();
extern void handleControlSaveLimits();
extern void handleControlSaveTemperatureGate();
extern void handleLegionellaStart();
extern void handleLegionellaCancel();
extern void handleLegionellaSave();
extern void handleLegionellaResetSuccess();
extern void handleRelayToggle();
extern void handleSettingsSaveReboot();
extern void handleSettingsSave();
extern void handleSettingsSaveSensors();
extern void handleSettingsHaMqttCleanup();

String htmlBaseStylesheet() {
  return F("<link rel='stylesheet' href='" WEB_BASE_CSS_PATH "'>");
}

String htmlHeader(const String& pageTitle, bool isDashboard) {
  String s;
  s.reserve(2000);
  s += "<!doctype html><html><head><meta charset='utf-8'>";
  s += "<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1'>";
  s += String("<title>") + DEVICE_NAME + "</title>";
  s += htmlBaseStylesheet();
  s += F("<link rel='stylesheet' href='" DASHBOARD_MODERN_2027_CSS_PATH "'>");
  s += F("<script src='" WEB_PAGE_SCRIPT_PATH "'></script>");
  s += "</head><body class='modern-2027'>";
  s += isDashboard ? "<div class='wrap'>" : "<div class='wrap modern-subpage'>";
  s += "<header class='modern-hero'><div class='modern-brand'>" + iconSvg("solar") + "</div><div class='modern-hero-copy'>";
  s += String("<h1>") + DEVICE_NAME + "</h1></div></header>";
  if (!isDashboard) s += "<h2 class='modern-page-title'>" + pageTitle + "</h2>";
  return s;
}


String iconSvg(const String& id) {
  // Lightweight inline SVG icons (no external assets) — keeps UI fast.
  if (id == "dash") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M3 11.5 12 4l9 7.5'/><path d='M6 10.5V20h12v-9.5'/></svg>";
  if (id == "prices") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M4 19V5'/><path d='M4 19h16'/><path d='M7 15l3-3 3 2 4-6'/></svg>";
  if (id == "control") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M4 7h10'/><path d='M4 17h16'/><path d='M14 7v6'/><path d='M10 17V9'/></svg>";
  if (id == "leg") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M10 14.5a4 4 0 1 0 4 0V5a2 2 0 0 0-4 0v9.5'/><path d='M12 6v8'/></svg>";
  if (id == "set") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M12 15.5a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7Z'/><path d='M19.4 15a7.8 7.8 0 0 0 .1-6l2-1.2-2-3.4-2.3.8a7.9 7.9 0 0 0-5.2-2.2L11.4 1H8.6L8 3.8A7.9 7.9 0 0 0 2.8 6l-2.3-.8-2 3.4 2 1.2a7.8 7.8 0 0 0 .1 6l-2 1.2 2 3.4 2.3-.8a7.9 7.9 0 0 0 5.2 2.2L8.6 23h2.8l.6-2.8a7.9 7.9 0 0 0 5.2-2.2l2.3.8 2-3.4-2-1.2Z'/></svg>";
  if (id == "solar") return "<svg viewBox='0 0 48 48' aria-hidden='true'><g class='modern-sun'><path d='M13 2v5M3 8l4 3M4 19H0M23 8l-4 3'/><path d='M5 24a9 9 0 1 1 15-9'/></g><path d='m16 19 29-5-9 23-29 7zM13 29l28-5M25 18l-9 24M35 16l-9 23'/></svg>";
  if (id == "bolt") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M13 2 5 13h6l-1 9 9-12h-6z'/></svg>";
  if (id == "sun") return "<svg viewBox='0 0 24 24' aria-hidden='true'><circle cx='12' cy='12' r='4'/><path d='M12 1v3m0 16v3M1 12h3m16 0h3M4 4l2 2m12 12 2 2M4 20l2-2M18 6l2-2'/></svg>";
  if (id == "wave") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M2 16c4 0 4-9 8-9s4 9 8 9c2 0 3-3 4-5'/></svg>";
  if (id == "layers") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='m12 3 9 5-9 5-9-5zM3 12l9 5 9-5M3 16l9 5 9-5'/></svg>";
  if (id == "leaf") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M4 17c-1-6 3-10 7-10 3 0 6-1 9-3 0 8-3 14-9 15-2 0-4 0-7-2ZM3 21l12-12'/></svg>";
  if (id == "coins") return "<svg viewBox='0 0 24 24' aria-hidden='true'><ellipse cx='12' cy='6' rx='8' ry='3'/><path d='M4 6v6c0 4 16 4 16 0V6M4 12v6c0 4 16 4 16 0v-6'/></svg>";
  if (id == "normal") return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M4 20v-5m5 5V9m5 11V4m5 16V8'/></svg>";
  if (id == "network") return "<svg viewBox='0 0 24 24' aria-hidden='true'><circle cx='12' cy='5' r='3'/><circle cx='5' cy='19' r='3'/><circle cx='19' cy='19' r='3'/><path d='M12 8v5m-7 3v-3h14v3'/></svg>";
  return "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M4 12h16'/></svg>";
}

String navHtml(const String& active) {
  auto tab = [&](const String& href, const String& name, const String& id) -> String {
    String a = "<a href='" + href + "'";
    if (active == id) a += " class='active'";
    if (active == id) a += " aria-current='page'";
    a += "><span class='ico'>" + iconSvg(id) + "</span><span>" + name + "</span></a>";
    return a;
  };

  String s;
  s += "<div class='nav'><div class='navin'>";
  s += tab("/", "Dashboard", "dash");
  s += tab("/prices", "Prijzen", "prices");
  s += tab("/control", "Control", "control");
  s += tab("/legionella", "Legionella", "leg");
  s += tab("/settings", "Instellingen", "set");
  s += "</div></div>";
  return s;
}


String htmlFooter(const String& activeNav) {
  String s;
  s += "</div>";
  s += navHtml(activeNav);
  s += "</body></html>";
  return s;
}

String rowHtml(const String& k, const String& v) {
  return "<div class='row'><div class='k'>" + k + "</div><div class='v'>" + v + "</div></div>";
}

String htmlAutoReturnAfterReboot(const String& title, const String& msg, const String& targetUrl) {
  String p = htmlHeader("Herstart");
  p += "<div class='card'>";
  p += "<div class='hd'><b>" + title + "</b><span class='tag'>Systeem</span></div>";
  p += "<div class='bd'>";
  p += "<div class='help' id='msg'";
  p += ">" + msg + "</div>";
  p += String("<div class='help' style='margin-top:10px' id='hint'>Wachten tot ") + DEVICE_NAME + " opnieuw online is…</div>";
  p += "<a class='btn' href='" + targetUrl + "' style='display:block;text-align:center;text-decoration:none'>Ga naar Dashboard</a>";
  p += "</div></div>";

  // Pas redirecten NA een echte offline moment (reboot)
  p += "<script>";
  p += "(()=>{";
  p += "const target='" + targetUrl + "';";
  p += "let seenFail=false;";
  p += "let tries=0;";
  p += "const t0=Date.now();";
  p += "const timeoutMs=120000;";
  p += "const hint=document.getElementById('hint');";
  p += "async function ping(){";
  p += "tries++;";
  p += "try{";
  p += "const r=await fetch('/api/status?ts='+Date.now(),{cache:'no-store'});";
  p += "if(r.ok){";
  p += "if(seenFail){ window.location.replace(target); return; }";
  p += "} else { seenFail=true; }";
  p += "}catch(e){ seenFail=true; }";
  p += "if(Date.now()-t0>timeoutMs){";
  p += "hint.textContent='Nog geen verbinding. Check IP (DHCP) of open handmatig het dashboard.';";
  p += "clearInterval(intv);";
  p += "} else {";
  p += "hint.textContent='Wachten… ('+tries+')';";
  p += "}";
  p += "}";
  p += "const intv=setInterval(ping,1000);";
  p += "setTimeout(ping,1200);";
  p += "})();";
  p += "</script>";

  p += htmlFooter("set");
  return p;
}

void handlePricesPage() {
  String page = htmlHeader("Prijzen");

  // (UI) Top tabs verwijderd – bottom nav is genoeg

  // ===== Samenvatting =====
  page += "<div class='card'><div class='hd'><b>Nordpool prijzen (ct/kWh)</b><span class='tag'>samenvatting</span></div><div class='bd'>";
  page += rowHtml("Nu", "<span id='sum_now'>-</span> ct/kWh");
  page += rowHtml("Goedkoopste (vandaag)", "<span id='sum_min'>-</span>");
  page += rowHtml("Duurste (vandaag)", "<span id='sum_max'>-</span>");
  page += rowHtml("Morgen beschikbaar", "<span id='sum_tom'>-</span>");
  page += "</div></div>";

  // ===== Vandaag =====
  page += "<div class='card'><div class='hd'><b>Vandaag</b><span class='tag' id='tag_today'>-</span></div><div class='bd'>";
  page += "<div class='spark priceSpark'><div class='chartTip' id='tip_today'></div><canvas id='c_today'></canvas></div>";
  page += "<div class='help' id='h_today'>-</div>";
  page += "<details class='acc'><summary>details</summary><div class='bd'><div class='tiny' id='d_today'>-</div></div></details>";
  page += "</div></div>";

  // ===== Morgen =====
  page += "<div class='card' id='card_tom'><div class='hd'><b>Morgen</b><span class='tag' id='tag_tom'>-</span></div><div class='bd'>";
  page += "<div class='spark priceSpark'><div class='chartTip' id='tip_tom'></div><canvas id='c_tom'></canvas></div>";
  page += "<div class='help' id='h_tom'>-</div>";
  page += "<details class='acc'><summary>details</summary><div class='bd'><div class='tiny' id='d_tom'>-</div></div></details>";
  page += "</div></div>";

  // ===== Gas (laatste 15 dagen incl. vandaag) =====
  page += "<div class='card' id='card_gas'><div class='hd'><b>Gas prijzen</b><span class='tag'>EUR/m³</span></div><div class='bd'>";
  page += "<div class='tiny'>Laatste update: <span id='gas_upd'>-</span></div>";
  page += "<div class='spark priceSpark' style='margin-top:10px'><div class='chartTip' id='tip_gas'></div><canvas id='c_gas15'></canvas></div>";
  page += "<div class='help' id='h_gas'>-</div>";
  page += "<details class='acc'><summary>details</summary><div class='bd'><div class='tiny' id='d_gas'>-</div></div></details>";
  page += "</div></div>";
  // ===== Instellingen (verplaatst, inklapbaar) =====
  page += "<details class='acc card' id='np_settings'><summary><span>Instellingen</span><span class='tag'>Nordpool</span></summary><div class='bd'>";
  page += rowHtml("Gebied", "<span id='np_area'>" + npArea + "</span>");
  page += rowHtml("Resolutie", "<span id='np_res'>" + String(npResMin) + "</span> min");
  page += rowHtml("Laatste update", "<span id='np_upd'>" + npUpdatedAt + "</span>");
  page += rowHtml("Status", "<span id='np_stat'>-</span>");
  page += "</div></details>";


  page += F("<script src='" WEB_PRICES_PATH "'></script>");

  page += htmlFooter("prices");
  server.sendHeader("Cache-Control", "no-store, max-age=0");
  server.send(200, "text/html; charset=utf-8", page);
}


void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

String htmlDashboardScripts() {
  String scripts;
  scripts += F("<script src='" WEB_DASHBOARD_MODERN_PATH "'></script>");
  scripts += F("<script src='" WEB_DASHBOARD_PATH "'></script>");
  return scripts;
}

String htmlSettingsScript() {
  return F("<script src='" WEB_SETTINGS_PATH "'></script>");
}

static void serveGzip(const char* type, const unsigned char* data, size_t length) {
  server.sendHeader("Cache-Control", "public, max-age=3600, immutable");
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, type, reinterpret_cast<const char*>(data), length);
}

static void serveGzipScript(const unsigned char* data, size_t length) {
  serveGzip(PSTR("application/javascript; charset=utf-8"), data, length);
}

void registerWebRoutes() {
  server.on("/", HTTP_GET, handleDashboard);
  server.on(WEB_BASE_CSS_PATH, HTTP_GET, []() {
    serveGzip(PSTR("text/css; charset=utf-8"), WEB_BASE_CSS_GZ, sizeof(WEB_BASE_CSS_GZ));
  });
  server.on(DASHBOARD_MODERN_2027_CSS_PATH, HTTP_GET, []() {
    serveGzip(PSTR("text/css; charset=utf-8"), DASHBOARD_MODERN_2027_CSS_GZ, sizeof(DASHBOARD_MODERN_2027_CSS_GZ));
  });
  server.on(WEB_DASHBOARD_PATH, HTTP_GET, []() {
    serveGzipScript(WEB_DASHBOARD_GZ, sizeof(WEB_DASHBOARD_GZ));
  });
  server.on(WEB_DASHBOARD_MODERN_PATH, HTTP_GET, []() {
    serveGzipScript(WEB_DASHBOARD_MODERN_GZ, sizeof(WEB_DASHBOARD_MODERN_GZ));
  });
  server.on(WEB_PAGE_SCRIPT_PATH, HTTP_GET, []() {
    serveGzipScript(WEB_PAGE_SCRIPT_GZ, sizeof(WEB_PAGE_SCRIPT_GZ));
  });
  server.on(WEB_PRICES_PATH, HTTP_GET, []() {
    serveGzipScript(WEB_PRICES_GZ, sizeof(WEB_PRICES_GZ));
  });
  server.on(WEB_SETTINGS_PATH, HTTP_GET, []() {
    serveGzipScript(WEB_SETTINGS_GZ, sizeof(WEB_SETTINGS_GZ));
  });
  server.on(MODERN_ENERGY_ART_PATH, HTTP_GET, []() {
    server.sendHeader("Cache-Control", "public, max-age=86400, immutable");
    server.send_P(200, PSTR("image/jpeg"), reinterpret_cast<const char*>(MODERN_ENERGY_ART_JPEG), sizeof(MODERN_ENERGY_ART_JPEG));
  });
  server.on("/control", HTTP_GET, handleControlPage);
  server.on("/legionella", HTTP_GET, handleLegionellaPage);
  server.on("/settings", HTTP_GET, handleSettingsPage);
  server.on("/prices", HTTP_GET, handlePricesPage);

  server.on("/control/toggle_force", HTTP_POST, handleToggleForce);
  server.on("/control/save_limits", HTTP_POST, handleControlSaveLimits);
  server.on("/control/save_temp_gate", HTTP_POST, handleControlSaveTemperatureGate);

  server.on("/legionella/start", HTTP_POST, handleLegionellaStart);
  server.on("/legionella/cancel", HTTP_POST, handleLegionellaCancel);
  server.on("/legionella/save", HTTP_POST, handleLegionellaSave);
  server.on("/legionella/reset_success", HTTP_POST, handleLegionellaResetSuccess);

  server.on("/relay/toggle", HTTP_POST, handleRelayToggle);

  server.on("/settings/save_reboot", HTTP_POST, handleSettingsSaveReboot);
  server.on("/settings/save", HTTP_POST, handleSettingsSave);
  server.on("/settings/save_sensors", HTTP_POST, handleSettingsSaveSensors);
  server.on("/settings/ha_mqtt_cleanup", HTTP_POST, handleSettingsHaMqttCleanup);
  server.on("/settings/save", HTTP_POST, handleSettingsSave);

  server.onNotFound(handleNotFound);
}
