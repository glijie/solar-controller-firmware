#pragma once

#include <Arduino.h>

String htmlCss();
String htmlHeader(const String& pageTitle);
String iconSvg(const String& id);
String topTabs(const String& active);
String navHtml(const String& active);
String htmlFooter(const String& activeNav);
String rowHtml(const String& k, const String& v);
String htmlAutoReturnAfterReboot(const String& title, const String& msg, const String& targetUrl = "/");

void handlePricesPage();
void handleNotFound();
void registerWebRoutes();
