#include "WebUtils.h"

#include <WebServer.h>

extern WebServer server;

String fmtHmMin(int minOfDay) {
  if (minOfDay < 0) minOfDay = 0;
  minOfDay = minOfDay % (24 * 60);
  int h = minOfDay / 60;
  int m = minOfDay % 60;
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
  return String(buf);
}

void redirectTo(const String& url) {
  server.sendHeader("Location", url);
  server.send(303);
}

String htmlAttrEscape(const String& in) {
  String s = in;
  s.replace("&", "&amp;");
  s.replace("\"", "&quot;");
  s.replace("'", "&#39;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  return s;
}

String fmtDot(float v, int decimals) {
  return String(v, decimals);
}
