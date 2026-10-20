#include "TimeSchedule.h"
#include "app_globals.h"
#include "nordpool.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool timeIsValid() {
  time_t now = time(nullptr);
  return (now > 1700000000);
}

uint32_t dateIntLocal(time_t t) {
  struct tm tmNow;
  localtime_r(&t, &tmNow);
  int y = tmNow.tm_year + 1900;
  int m = tmNow.tm_mon + 1;
  int d = tmNow.tm_mday;
  return (uint32_t)(y * 10000 + m * 100 + d);
}

String fmtTimeLocal(time_t t) {
  if (t <= 0) return String("");
  struct tm tmNow;
  localtime_r(&t, &tmNow);
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d-%02d-%04d %02d:%02d",
           tmNow.tm_mday, tmNow.tm_mon + 1, tmNow.tm_year + 1900,
           tmNow.tm_hour, tmNow.tm_min);
  return String(buf);
}

time_t computeNextLegionellaEpoch() {
  if (!timeIsValid()) return 0;
  time_t now = time(nullptr);
  struct tm tmNow;
  localtime_r(&now, &tmNow);

  for (int addDays = 0; addDays < 8; addDays++) {
    struct tm cand = tmNow;
    cand.tm_sec = 0;
    cand.tm_min = legStartMinute;
    cand.tm_hour = legStartHour;
    cand.tm_mday = tmNow.tm_mday + addDays;
    time_t candEpoch = mktime(&cand);

    struct tm cand2;
    localtime_r(&candEpoch, &cand2);

    if (cand2.tm_wday == legDow) {
      if (candEpoch >= now) return candEpoch;
    }
  }
  return 0;
}

String isoDateLocal(time_t t){
  struct tm tmNow; localtime_r(&t, &tmNow);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday);
  return String(buf);
}

String dmyDateLocal(time_t t){
  struct tm tmNow; localtime_r(&t, &tmNow);
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d-%02d-%04d", tmNow.tm_mday, tmNow.tm_mon + 1, tmNow.tm_year + 1900);
  return String(buf);
}

String gasDayIsoLocal(time_t t){
  time_t adj = t - (time_t)GAS_DAY_START_HOUR * 3600;
  return isoDateLocal(adj);
}

static bool isLeapYear(int y){ return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0); }
static int daysInMonth(int y, int m){
  static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (m == 2) return mdays[m-1] + (isLeapYear(y) ? 1 : 0);
  return mdays[m-1];
}

String isoDateAddDays(const String& iso, int deltaDays){
  if (iso.length() < 10) return iso;
  int y = iso.substring(0,4).toInt();
  int m = iso.substring(5,7).toInt();
  int d = iso.substring(8,10).toInt();
  if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return iso;

  int delta = deltaDays;
  while (delta != 0){
    if (delta > 0){
      int dim = daysInMonth(y, m);
      if (d < dim){ d++; delta--; }
      else { d = 1; m++; if (m > 12){ m = 1; y++; } delta--; }
    } else {
      if (d > 1){ d--; delta++; }
      else { m--; if (m < 1){ m = 12; y--; } d = daysInMonth(y, m); delta++; }
    }
  }

  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
  return String(buf);
}

static int lastSundayOfMonth(int year, int month1to12){
  struct tm tmX {};
  tmX.tm_year = year - 1900;
  tmX.tm_mon  = month1to12 - 1;
  tmX.tm_mday = 31;
  tmX.tm_hour = 12;
  time_t t = mktime(&tmX);
  localtime_r(&t, &tmX);
  int lastDay = tmX.tm_mday;
  int w = tmX.tm_wday;
  int delta = w;
  return lastDay - delta;
}

static bool isDstEuropeAmsterdam(const struct tm& localTm){
  int year = localTm.tm_year + 1900;
  int m = localTm.tm_mon + 1;
  if (m < 3 || m > 10) return false;
  if (m > 3 && m < 10) return true;

  if (m == 3){
    int ls = lastSundayOfMonth(year, 3);
    if (localTm.tm_mday > ls) return true;
    if (localTm.tm_mday < ls) return false;
    return (localTm.tm_hour >= 2);
  } else {
    int ls = lastSundayOfMonth(year, 10);
    if (localTm.tm_mday < ls) return true;
    if (localTm.tm_mday > ls) return false;
    return (localTm.tm_hour < 3);
  }
}

static int tzOffsetMinutesAmsterdam(const struct tm& localTm){
  return isDstEuropeAmsterdam(localTm) ? 120 : 60;
}

static inline double d2r(double d){ return d * (M_PI / 180.0); }
static inline double r2d(double r){ return r * (180.0 / M_PI); }

static bool calcSunriseSunsetMinutes(int year, int month, int day, float lat, float lon,
                                     int tzOffsetMin,
                                     int &outSunriseMin, int &outSunsetMin){
  static const int monthDaysNorm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  auto isLeap = [&](int y){ return ((y%4==0 && y%100!=0) || (y%400==0)); };
  int N = 0;
  for(int m=1;m<month;m++){
    N += monthDaysNorm[m-1];
    if (m==2 && isLeap(year)) N += 1;
  }
  N += day;

  double lngHour = lon / 15.0;
  auto calcOne = [&](bool sunrise, int &outMin)->bool{
    double t = N + ((sunrise ? 6.0 : 18.0) - lngHour) / 24.0;
    double M = (0.9856 * t) - 3.289;
    double L = M + (1.916 * sin(d2r(M))) + (0.020 * sin(2*d2r(M))) + 282.634;
    while (L < 0) L += 360;
    while (L >= 360) L -= 360;

    double RA = r2d(atan(0.91764 * tan(d2r(L))));
    while (RA < 0) RA += 360;
    while (RA >= 360) RA -= 360;

    double Lquadrant  = floor(L/90.0) * 90.0;
    double RAquadrant = floor(RA/90.0)* 90.0;
    RA = RA + (Lquadrant - RAquadrant);
    RA = RA / 15.0;

    double sinDec = 0.39782 * sin(d2r(L));
    double cosDec = cos(asin(sinDec));
    const double zenith = 90.833;
    double cosH = (cos(d2r(zenith)) - (sinDec * sin(d2r(lat)))) / (cosDec * cos(d2r(lat)));
    if (cosH > 1) return false;
    if (cosH < -1) return false;

    double H = sunrise ? (360.0 - r2d(acos(cosH))) : r2d(acos(cosH));
    H = H / 15.0;

    double T = H + RA - (0.06571 * t) - 6.622;
    double UT = T - lngHour;
    while (UT < 0) UT += 24;
    while (UT >= 24) UT -= 24;

    double localT = UT + (tzOffsetMin / 60.0);
    while (localT < 0) localT += 24;
    while (localT >= 24) localT -= 24;

    outMin = (int)lround(localT * 60.0);
    if (outMin < 0) outMin = 0;
    if (outMin > 24*60) outMin = 24*60;
    return true;
  };

  int sr= -1, ss= -1;
  if (!calcOne(true, sr)) return false;
  if (!calcOne(false, ss)) return false;
  outSunriseMin = sr;
  outSunsetMin  = ss;
  return true;
}

static int minutesSinceMidnightLocal(time_t now){
  struct tm tmNow; localtime_r(&now, &tmNow);
  return tmNow.tm_hour*60 + tmNow.tm_min;
}

static bool isInWindowMinutes(int nowMin, int startMin, int endMin){
  startMin = (startMin % (24*60) + (24*60)) % (24*60);
  endMin   = (endMin   % (24*60) + (24*60)) % (24*60);
  if (startMin == endMin) return true;
  if (startMin < endMin)  return (nowMin >= startMin && nowMin < endMin);
  return (nowMin >= startMin || nowMin < endMin);
}

void recomputeCheapestDaylightWindowIfNeeded(){
  if (!timeIsValid()) return;
  if (!npEnabled) return;
  if (npTodayN <= 0) return;

  time_t now = time(nullptr);
  String today = isoDateLocal(now);
  if (sunWinDate == today && sunCheapestStartMin >= 0 && sunCheapestEndMin >= 0) return;

  struct tm tmNow; localtime_r(&now, &tmNow);
  int tzMin = tzOffsetMinutesAmsterdam(tmNow);

  int sunriseMin=-1, sunsetMin=-1;
  if (!calcSunriseSunsetMinutes(tmNow.tm_year+1900, tmNow.tm_mon+1, tmNow.tm_mday, sunLat, sunLon, tzMin, sunriseMin, sunsetMin)) {
    sunWinDate = today;
    sunDaylightStartMin = -1;
    sunDaylightEndMin   = -1;
    sunCheapestStartMin = sunWinStartMin;
    sunCheapestEndMin   = sunWinEndMin;
    return;
  }

  sunriseMin += sunDaylightMarginMin;
  sunsetMin  -= sunDaylightMarginMin;
  if (sunriseMin < 0) sunriseMin = 0;
  if (sunsetMin > 24*60) sunsetMin = 24*60;
  if (sunsetMin <= sunriseMin) {
    sunWinDate = today;
    sunDaylightStartMin = sunriseMin;
    sunDaylightEndMin   = sunsetMin;
    sunCheapestStartMin = sunWinStartMin;
    sunCheapestEndMin   = sunWinEndMin;
    return;
  }

  sunDaylightStartMin = sunriseMin;
  sunDaylightEndMin   = sunsetMin;

  int intervalMin = npIntervalMinToday;
  if (intervalMin != 15 && intervalMin != 60) intervalMin = 60;

  int startIdx = sunriseMin / intervalMin;
  int endIdxExcl = (sunsetMin + intervalMin - 1) / intervalMin;
  if (startIdx < 0) startIdx = 0;
  if (endIdxExcl > npTodayN) endIdxExcl = npTodayN;

  int nBlocks = sunCheapNBlocks;
  if (nBlocks < 1) nBlocks = 1;
  if (nBlocks > npTodayN) nBlocks = npTodayN;

  double bestSum = 1e18;
  int bestIdx = -1;

  auto priceCt = [&](int i)->double{
    float spot = npTodayCt[i];
    return (npInclTaxEnabled) ? (double)npTotalCtKwh(spot) : (double)spot;
  };

  for (int i = startIdx; i + nBlocks <= endIdxExcl; i++){
    double sum = 0;
    for (int k=0;k<nBlocks;k++) sum += priceCt(i+k);
    if (sum < bestSum){
      bestSum = sum;
      bestIdx = i;
    }
  }

  if (bestIdx < 0) {
    sunCheapestStartMin = sunWinStartMin;
    sunCheapestEndMin   = sunWinEndMin;
  } else {
    sunCheapestStartMin = bestIdx * intervalMin;
    sunCheapestEndMin   = (bestIdx + nBlocks) * intervalMin;
  }

  sunWinDate = today;
}

bool normalAllowedByScheduleNow(){
  if (sunSchedMode == 0) return true;
  if (sunSchedMode == 3) return false;
  if (!timeIsValid()) return false;

  time_t now = time(nullptr);
  int nowMin = minutesSinceMidnightLocal(now);

  if (sunSchedMode == 1){
    return isInWindowMinutes(nowMin, sunWinStartMin, sunWinEndMin);
  }

  if (sunSchedMode == 2){
    recomputeCheapestDaylightWindowIfNeeded();
    if (sunCheapestStartMin < 0 || sunCheapestEndMin < 0) return false;
    return isInWindowMinutes(nowMin, sunCheapestStartMin, sunCheapestEndMin);
  }

  return false;
}

bool appNormalAllowedByScheduleNow() {
  return normalAllowedByScheduleNow();
}

String fmtDateDMYFromIso(const String& iso){
  if (iso.length() < 10) return iso;
  String y = iso.substring(0,4);
  String m = iso.substring(5,7);
  String d = iso.substring(8,10);
  if (y.length()!=4 || m.length()!=2 || d.length()!=2) return iso;
  return d + "-" + m + "-" + y;
}

int isoDeltaMinutes(const String& startIso, const String& endIso){
  if (startIso.length() < 16 || endIso.length() < 16) return 60;
  int sh = startIso.substring(11,13).toInt();
  int sm = startIso.substring(14,16).toInt();
  int eh = endIso.substring(11,13).toInt();
  int em = endIso.substring(14,16).toInt();
  int d = (eh*60 + em) - (sh*60 + sm);
  if (d <= 0) d += 24*60;
  return d;
}
