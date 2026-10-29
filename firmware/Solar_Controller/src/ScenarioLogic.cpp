#include "ScenarioLogic.h"

#include <math.h>

#include "MultiController.h"
#include "nordpool.h"
#include "sensors.h"

static bool multiControllerSlaveShouldFollowMasterOnly() {
  return multiControllerIsSlaveRole() && multiControllerHasNetworkPeersConfigured() && !multiControllerSlaveLegionellaOverrideActive();
}

static bool multiControllerSlaveHasFreshMasterTargets() {
  return multiControllerSlaveShouldFollowMasterOnly() && multiControllerIsRemoteTargetControlActive();
}

uint8_t currentPriorityModeNow() {
  if (multiControllerSlaveShouldFollowMasterOnly()) {
    if (!multiControllerSlaveHasFreshMasterTargets()) return PMODE_NONE;
    const uint8_t scenario = multiControllerGetScenario();
    if (scenario == MC_SCENARIO_MANUAL_FORCE) return PMODE_FORCE;
    if (scenario == MC_SCENARIO_PRIORITY_HEAT) return PMODE_LEGIONELLA;
    if (scenario == MC_SCENARIO_CHEAP || scenario == MC_SCENARIO_NEGATIVE) return PMODE_BOOST;
    return PMODE_NONE;
  }
  if (legRunning) return PMODE_LEGIONELLA;
  if (controlMode == MODE_FORCE_HEAT) return PMODE_FORCE;
  if (npIsBoostActiveNow()) return PMODE_BOOST;
  if (heatExecElecActiveNow()) return PMODE_AUTO_ELEC;
  return PMODE_NONE;
}

bool priorityHeatingActiveNow() {
  if (multiControllerSlaveShouldFollowMasterOnly()) {
    if (!multiControllerSlaveHasFreshMasterTargets()) return false;
    const uint8_t scenario = multiControllerGetScenario();
    return scenario == MC_SCENARIO_MANUAL_FORCE ||
           scenario == MC_SCENARIO_PRIORITY_HEAT ||
           scenario == MC_SCENARIO_CHEAP ||
           scenario == MC_SCENARIO_NEGATIVE;
  }
  return (controlMode == MODE_FORCE_HEAT) || legRunning || npBoostActiveNow || heatExecElecActiveNow();
}

uint8_t multiControllerCurrentAppScenario() {
  if (multiControllerSlaveShouldFollowMasterOnly()) {
    if (!multiControllerSlaveHasFreshMasterTargets()) return MC_SCENARIO_IDLE;
    return multiControllerGetScenario();
  }
  if (legRunning) return MC_SCENARIO_PRIORITY_HEAT;
  if (controlMode == MODE_FORCE_HEAT) return MC_SCENARIO_MANUAL_FORCE;
  if (npBoostActiveNow) {
    const float elecNow = elecCtKwhNow();
    if (!isnan(elecNow) && elecNow < 0.0f) return MC_SCENARIO_NEGATIVE;
    return MC_SCENARIO_CHEAP;
  }
  if (heatExecElecActiveNow()) return MC_SCENARIO_PRIORITY_HEAT;
  if (appNormalAllowedByScheduleNow()) return MC_SCENARIO_SURPLUS;
  return MC_SCENARIO_IDLE;
}

static bool multiControllerFollowerShouldYieldSurplusTargetToCoordinator() {
  if (multiControllerSlaveShouldFollowMasterOnly()) return true;
  return mcEnabled && multiControllerHasNetworkPeersConfigured() && !multiControllerIsInternalCoordinator();
}

static bool multiControllerHandleFollowerSurplusFromMaster() {
  if (!multiControllerFollowerShouldYieldSurplusTargetToCoordinator()) return false;
  lastErrorW = 0.0f;
  lastOvershootW = 0.0f;
  multiControllerSetAppScenario(MC_SCENARIO_SURPLUS);
  // In netwerk-Multi Controller volgt een niet-master bij zonne-overschot
  // uitsluitend de master. De lokale follower-targetExportW mag de
  // groepsregeling dus niet meer tegenwerken of begrenzen.
  multiControllerApplyRequestedGroupPercentForScenario(multiControllerGetRequestedGroupPercent(), MC_SCENARIO_SURPLUS);
  return true;
}

static bool multiControllerApplySurplusGroupClosedLoop(long netPowerW, bool stepTuned) {
  if (!multiControllerShouldUseGroupControlForSurplus()) return false;

  const float errorW = targetExportW - (float)netPowerW;
  lastErrorW = errorW;

  float overschotW = errorW;
  if (overschotW < 0.0f) overschotW = 0.0f;
  lastOvershootW = overschotW;

  if (fabsf(errorW) <= deadbandW) {
    return true;
  }

  float ratio = 0.0f;
  if (maxBoilerPowerW > 1.0f) ratio = errorW / maxBoilerPowerW;

  float gain = stepTuned ? stepFactor : 1.0f;
  if (gain < 0.01f) gain = 0.01f;

  float deltaPct = gain * ratio * 100.0f;
  if (deltaPct >  maxStepPctPerUpdatePerUpdate) deltaPct =  maxStepPctPerUpdatePerUpdate;
  if (deltaPct < -maxStepPctPerUpdatePerUpdate) deltaPct = -maxStepPctPerUpdatePerUpdate;

  // Belangrijk voor master-slave NORMAL:
  // hier regelen we niet meer eerst naar een absolute "groeps-PWM = f(targetExportW)".
  // De master verhoogt of verlaagt nu direct de actuele groeps-output op basis van
  // de P1-fout richting targetExportW. Alleen de veiligheidslagen mogen daarna nog
  // begrenzen.
  float currentGroupPercent = multiControllerGetRequestedGroupPercent();
  if (currentGroupPercent < 0.0f) currentGroupPercent = 0.0f;
  if (currentGroupPercent <= 0.0f) currentGroupPercent = multiControllerGetAssignedGroupPercent();

  float newGroupDuty = currentGroupPercent + deltaPct;
  const float maxGroupPercent = multiControllerGetSurplusGroupMaxPercent();
  if (newGroupDuty > maxGroupPercent) newGroupDuty = maxGroupPercent;
  if (newGroupDuty < 0.0f) newGroupDuty = 0.0f;
  setBoilerGroupDutyPercent(newGroupDuty);
  return true;
}

void applyNormalDirect(long netPowerW) {
  if (multiControllerHandleFollowerSurplusFromMaster()) return;
  if (multiControllerApplySurplusGroupClosedLoop(netPowerW, false)) return;

  float errorW = targetExportW - (float)netPowerW;
  lastErrorW = errorW;

  float overschotW = errorW;
  if (overschotW < 0.0f) overschotW = 0.0f;
  lastOvershootW = overschotW;

  float targetPercent = 0.0f;
  if (overschotW > 0.0f) {
    targetPercent = (overschotW / maxBoilerPowerW) * 100.0f;
    if (targetPercent > 100.0f) targetPercent = 100.0f;
  }
  setBoilerDutyPercent(targetPercent);
}

void applyNormalStep(long netPowerW) {
  if (multiControllerHandleFollowerSurplusFromMaster()) return;
  if (multiControllerApplySurplusGroupClosedLoop(netPowerW, true)) return;

  float errorW = targetExportW - (float)netPowerW;
  lastErrorW = errorW;

  float overschotW = errorW;
  if (overschotW < 0.0f) overschotW = 0.0f;
  lastOvershootW = overschotW;

  if (fabs(errorW) <= deadbandW) {
    return;
  }

  float ratio = 0.0f;
  if (maxBoilerPowerW > 1.0f) ratio = errorW / maxBoilerPowerW;

  float deltaPct = stepFactor * ratio * 100.0f;

  if (deltaPct >  maxStepPctPerUpdatePerUpdate) deltaPct =  maxStepPctPerUpdatePerUpdate;
  if (deltaPct < -maxStepPctPerUpdatePerUpdate) deltaPct = -maxStepPctPerUpdatePerUpdate;

  float newDuty = lastDutyPercent + deltaPct;
  setBoilerDutyPercent(newDuty);
}

void applyNormalMode(long netPowerW) {
  if (normalAlg == 1) applyNormalStep(netPowerW);
  else                applyNormalDirect(netPowerW);
}

void applyForceHeatMode() {
  lastOvershootW = 0.0f;
  lastErrorW = 0.0f;
  if (multiControllerSlaveShouldFollowMasterOnly()) {
    multiControllerSetAppScenario(MC_SCENARIO_MANUAL_FORCE);
    multiControllerApplyRequestedGroupPercentForScenario(0.0f, MC_SCENARIO_MANUAL_FORCE);
    return;
  }
  if (multiControllerShouldUseGroupControlForScenario(MC_SCENARIO_MANUAL_FORCE)) {
    setBoilerDutyPercentForScenario(multiControllerGroupTargetMaxForScenario(MC_SCENARIO_MANUAL_FORCE), MC_SCENARIO_MANUAL_FORCE);
    return;
  }
  setBoilerDutyPercent(maxOutputPercentLimit);
}

void applyPriceBoostMode() {
  lastOvershootW = 0.0f;
  lastErrorW = 0.0f;
  const uint8_t mcScenario = multiControllerCurrentAppScenario();
  if (multiControllerSlaveShouldFollowMasterOnly()) {
    multiControllerSetAppScenario(mcScenario);
    multiControllerApplyRequestedGroupPercentForScenario(0.0f, mcScenario);
    return;
  }
  if ((mcScenario == MC_SCENARIO_CHEAP || mcScenario == MC_SCENARIO_NEGATIVE || mcScenario == MC_SCENARIO_PRIORITY_HEAT) &&
      multiControllerShouldUseGroupControlForScenario(mcScenario)) {
    setBoilerDutyPercentForScenario(multiControllerGroupTargetMaxForScenario(mcScenario), mcScenario);
    return;
  }
  setBoilerDutyPercent(maxOutputPercentLimit);
}

// ================================================================================================
// Slim ELEC uitvoeren: goedkoopste aaneengesloten blokken.
// Met 1 dagdeel blijft de bestaande planning vanaf NU tot 24:00 exact behouden.
// Met 2 dagdelen wordt onafhankelijk gepland tot de eerstvolgende ingestelde tijdgrens.
//
// step315 regels:
// - N is een maximum, geen minimum.
// - Bij een nieuw plan wordt vanaf NU tot 24:00 naar ALLE blokken gekeken waarvoor ELEC goedkoper is.
// - De gekozen blokken moeten aaneengesloten zijn; een GAS-blok mag nooit in het venster zitten.
// - Bestaat ergens vandaag een reeks van N geschikte blokken, kies dan de goedkoopste reeks van precies N.
// - Bestaat nergens een reeks van N, kies dan de langste beschikbare geschikte reeks (< N) en daarvan
//   de goedkoopste variant. Zo wordt bijvoorbeeld bij N=4 en maximaal 3 opeenvolgende ELEC-blokken toch 3 gebruikt.
// - Om 00:00 begint altijd een nieuw dagplan (nieuwe dag = nieuwe kansen).
// - Wordt GAS tussendoor goedkoper, dan stopt de uitvoering direct en vervalt het huidige plan.
// - Wordt ELEC later dezelfde dag opnieuw goedkoper, dan ontstaat een nieuwe kans en wordt opnieuw vanaf
//   dat moment tot 24:00 geoptimaliseerd.
// - Voor temperatuur gebruikt deze regeling uitsluitend de expliciet gekozen boilersensor (legTempAddrStr).
//   Er is bewust GEEN fallback naar controller-temperatuurvrijgave of de eerste DS18B20.
// - Eigen toggle heatCheapUseBoilerTarget bepaalt of de boiler-doeltemperatuur de N-run mag stoppen.
//   De Legionella-toggle "Temperatuur-modus gebruiken" staat hier volledig los van.
// - Na opwarming kan dezelfde boilersensor een nieuwe warmtevraag openen zodra de temperatuur minimaal
//   5 °C onder de ingestelde boiler-doeltemperatuur komt.
// ================================================================================================

static String heatCheapPeriodDate = "";
static constexpr float HEAT_CHEAP_REARM_DELTA_C = 5.0f;

struct HeatCheapDayPartWindow {
  String key;
  String deadlineDate;
  int deadlineMin = 0;
};

// Bij 2 dagdelen zijn de twee ingestelde tijden de grenzen van de dagdelen.
// Voor 07:00 en 19:00 ontstaan dus automatisch 19:00->07:00 en 07:00->19:00.
static bool heatCheapGetCurrentDayPartWindow(HeatCheapDayPartWindow& out) {
  if (heatCheapDayParts != 2 || !timeIsValid()) return false;

  int a = constrain(heatCheapBoundary1Min, 0, 23 * 60 + 59);
  int b = constrain(heatCheapBoundary2Min, 0, 23 * 60 + 59);
  if (a == b) return false;
  if (a > b) { const int t = a; a = b; b = t; }

  const time_t now = time(nullptr);
  const String today = isoDateLocal(now);
  struct tm tmNow;
  localtime_r(&now, &tmNow);
  const int nowMin = tmNow.tm_hour * 60 + tmNow.tm_min;

  if (nowMin < a) {
    out.deadlineDate = today;
    out.deadlineMin = a;
  } else if (nowMin < b) {
    out.deadlineDate = today;
    out.deadlineMin = b;
  } else {
    out.deadlineDate = isoDateAddDays(today, 1);
    out.deadlineMin = a;
  }

  out.key = out.deadlineDate + "@" + String(out.deadlineMin);
  return true;
}

// Temperatuur-warmtevraag hoort bij de expliciet geselecteerde boilersensor uit Legionella-instellingen.
// De geselecteerde sensor mag leeg zijn; dan blijft prijsplanning volledig leidend.
static bool   heatCheapTempDemandInitialized = false;
static bool   heatCheapTempDemandActive = true;
static bool   heatCheapTempModeEnabled = false;
static bool   heatCheapTempPassiveRearmArmed = false;
static bool   heatCheapTempSensorAssigned = false;
static bool   heatCheapTempSensorValid = false;
static String heatCheapTempSensorAddr = "";
static String heatCheapTempSensorName = "";
static float  heatCheapTempCurrentC = NAN;
static float  heatCheapTempStartC = NAN;
static float  heatCheapTempStopC = NAN;

static void heatCheapPlanClearWindow() {
  heatCheapPlanValid = false;
  heatCheapPlanStartDate = "";
  heatCheapPlanEndDate = "";
  heatCheapPlanStartIdx = -1;
  heatCheapPlanEndIdxExcl = -1;
  heatCheapPlanIntervalMin = 0;
  heatCheapPlanSelectedBlocks = 0;
}

static void heatCheapResetPeriodKeepTemperatureDemand() {
  heatCheapPeriodLatched = false;
  heatCheapPeriodDate = "";
  heatCheapPlanClearWindow();
}

void heatCheapPlanInvalidate() {
  heatCheapResetPeriodKeepTemperatureDemand();
}

static bool heatCheapFloatChanged(float a, float b) {
  if (!isfinite(a) && !isfinite(b)) return false;
  if (!isfinite(a) || !isfinite(b)) return true;
  return fabsf(a - b) > 0.01f;
}

// Geeft true terug als temperatuur de slimme ELEC-run toestaat.
// Zonder expliciet gekozen/geldige boilersensor is temperatuur niet leidend en blijft prijsplanning volledig werken.
// demandRestarted wordt true bij een echte nieuwe warmtevraag nadat de boiler eerst boven de herstartgrens kwam
// en later weer <= (doeltemperatuur - 5 °C) is gezakt.
// De eigen toggle heatCheapUseBoilerTarget bepaalt uitsluitend of bereiken van de doeltemperatuur de AUTO_ELEC-run stopt.
static bool heatCheapTemperatureDemandAllowsHeating(bool& demandRestarted) {
  demandRestarted = false;

  String assignedAddr = legTempAddrStr;
  assignedAddr.trim();
  assignedAddr.toLowerCase();
  const bool modeEnabledNow = heatCheapUseBoilerTarget;
  const bool modeChanged = (heatCheapTempModeEnabled != modeEnabledNow);

  // Geen expliciete boilersensor gekozen: geen temperatuurbegrenzing en geen temperatuur-warmtevraagdetectie.
  if (assignedAddr.length() != 16) {
    heatCheapTempDemandInitialized = false;
    heatCheapTempDemandActive = true;
    heatCheapTempModeEnabled = modeEnabledNow;
    heatCheapTempPassiveRearmArmed = false;
    heatCheapTempSensorAssigned = false;
    heatCheapTempSensorValid = false;
    heatCheapTempSensorAddr = "";
    heatCheapTempSensorName = "";
    heatCheapTempCurrentC = NAN;
    heatCheapTempStartC = NAN;
    heatCheapTempStopC = NAN;
    return true;
  }

  heatCheapTempSensorAssigned = true;
  updateTempIfNeeded();

  float tempC = NAN;
  String sensorName = "";
  bool sensorValid = false;
  for (int i = 0; i < tempSensorCount; i++) {
    if (tempSensors[i].addrStr != assignedAddr) continue;
    sensorName = tempSensors[i].name.length() ? tempSensors[i].name : (String("DS18B20 ") + String(i + 1));
    if (tempSensors[i].present && tempIsValid(tempSensors[i].tempC)) {
      tempC = tempSensors[i].tempC;
      sensorValid = true;
    }
    break;
  }

  float stopC = legTargetTempC;
  if (!isfinite(stopC)) stopC = 60.0f;
  if (stopC < 40.0f) stopC = 40.0f;
  if (stopC > 75.0f) stopC = 75.0f;
  float startC = stopC - HEAT_CHEAP_REARM_DELTA_C;

  heatCheapTempSensorValid = sensorValid;
  heatCheapTempCurrentC = sensorValid ? tempC : NAN;
  heatCheapTempSensorName = sensorName;

  const bool configChanged =
      (heatCheapTempSensorAddr != assignedAddr) ||
      heatCheapFloatChanged(heatCheapTempStartC, startC) ||
      heatCheapFloatChanged(heatCheapTempStopC, stopC) ||
      modeChanged;

  if (configChanged) {
    heatCheapTempDemandInitialized = false;
    heatCheapTempPassiveRearmArmed = false;
    heatCheapTempSensorAddr = assignedAddr;
    heatCheapTempStartC = startC;
    heatCheapTempStopC = stopC;
  }
  heatCheapTempModeEnabled = modeEnabledNow;

  // Tijdelijk ongeldige/afwezige gekozen sensor mag AUTO_ELEC niet permanent blokkeren.
  // De regeling valt dan terug op prijsplanning, maar gebruikt nooit een andere sensor als fallback.
  if (!sensorValid || !isfinite(tempC)) {
    heatCheapTempDemandActive = true;
    return true;
  }

  // Eigen boiler-doeltemperatuur-toggle UIT:
  // - doeltemperatuur mag een gekozen N-run NOOIT afbreken;
  // - sensor blijft wel bruikbaar als warmtevraagdetector. Na eerst boven de herstartgrens te zijn geweest
  //   opent een daling tot/onder doeltemperatuur - 5 °C een nieuw plan (bijv. na douchen).
  if (!modeEnabledNow) {
    heatCheapTempDemandActive = true;

    if (!heatCheapTempDemandInitialized) {
      heatCheapTempDemandInitialized = true;
      heatCheapTempPassiveRearmArmed = (tempC > startC);
      if (configChanged && heatCheapPeriodLatched) demandRestarted = true;
    } else if (tempC > startC) {
      heatCheapTempPassiveRearmArmed = true;
    } else if (tempC <= startC && heatCheapTempPassiveRearmArmed) {
      heatCheapTempPassiveRearmArmed = false;
      demandRestarted = true;
    }

    return true;
  }

  // Eigen boiler-doeltemperatuur-toggle AAN:
  // doeltemperatuur is de stopgrens; nieuwe warmtevraag ontstaat pas weer <= doeltemperatuur - 5 °C.
  heatCheapTempPassiveRearmArmed = false;
  if (!heatCheapTempDemandInitialized) {
    heatCheapTempDemandActive = (tempC < stopC);
    heatCheapTempDemandInitialized = true;
    if (configChanged && heatCheapTempDemandActive) demandRestarted = true;
  } else if (heatCheapTempDemandActive) {
    if (tempC >= stopC) {
      heatCheapTempDemandActive = false;
    }
  } else if (tempC <= startC) {
    heatCheapTempDemandActive = true;
    demandRestarted = true;
  }

  return heatCheapTempDemandActive;
}

// Alleen resterende prijsblokken van de lokale kalenderdag teruggeven.
// Morgen kan een goedkoper prijsblok de planning van vandaag dus nooit uitstellen.
static bool heatCheapGetPointOffsetFromNow(int offset, String& date, int& idx, int& intervalMin, float& elecCt) {
  if (offset < 0 || !timeIsValid() || !npEnabled || elecPriceSource != 0 || npTodayN <= 0) return false;

  const time_t now = time(nullptr);
  const String today = isoDateLocal(now);
  if (today != npDateToday) return false;

  int todayInterval = npIntervalMinToday;
  if (todayInterval != 15 && todayInterval != 60) todayInterval = npResMin;
  if (todayInterval != 15 && todayInterval != 60) return false;

  struct tm tmNow;
  localtime_r(&now, &tmNow);
  const int nowMin = tmNow.tm_hour * 60 + tmNow.tm_min;
  const int currentIdx = nowMin / todayInterval;
  if (currentIdx < 0 || currentIdx >= npTodayN) return false;

  const int idxToday = currentIdx + offset;
  if (idxToday < currentIdx || idxToday >= npTodayN) return false;

  idx = idxToday;
  intervalMin = todayInterval;
  date = npDateToday;
  const float spot = npTodayCt[idx];
  elecCt = npInclTaxEnabled ? npTotalCtKwh(spot) : spot;
  return isfinite(elecCt);
}

// Variant voor 2 dagdelen: loopt vanaf het huidige blok door in morgen als de prijzen al beschikbaar zijn.
// De bestaande 1-dagdeelmodus gebruikt deze functie niet en behoudt dus exact de oude daggrens.
static bool heatCheapGetPointOffsetFromNowAcrossTomorrow(int offset, String& date, int& idx, int& intervalMin, float& elecCt) {
  if (offset < 0 || !timeIsValid() || !npEnabled || elecPriceSource != 0 || npTodayN <= 0) return false;

  const time_t now = time(nullptr);
  const String today = isoDateLocal(now);
  if (today != npDateToday) return false;

  int todayInterval = npIntervalMinToday;
  if (todayInterval != 15 && todayInterval != 60) todayInterval = npResMin;
  if (todayInterval != 15 && todayInterval != 60) return false;

  struct tm tmNow;
  localtime_r(&now, &tmNow);
  const int nowMin = tmNow.tm_hour * 60 + tmNow.tm_min;
  const int currentIdx = nowMin / todayInterval;
  if (currentIdx < 0 || currentIdx >= npTodayN) return false;

  const int remainingToday = npTodayN - currentIdx;
  if (offset < remainingToday) {
    idx = currentIdx + offset;
    intervalMin = todayInterval;
    date = npDateToday;
    const float spot = npTodayCt[idx];
    elecCt = npInclTaxEnabled ? npTotalCtKwh(spot) : spot;
    return isfinite(elecCt);
  }

  const int tomorrowOffset = offset - remainingToday;
  if (tomorrowOffset < 0 || npTomorrowN <= 0) return false;
  if (npDateTomorrow != isoDateAddDays(today, 1)) return false;

  int tomorrowInterval = npIntervalMinTomorrow;
  if (tomorrowInterval != 15 && tomorrowInterval != 60) tomorrowInterval = npResMin;
  if (tomorrowInterval != todayInterval) return false;
  if (tomorrowOffset >= npTomorrowN) return false;

  idx = tomorrowOffset;
  intervalMin = tomorrowInterval;
  date = npDateTomorrow;
  const float spot = npTomorrowCt[idx];
  elecCt = npInclTaxEnabled ? npTotalCtKwh(spot) : spot;
  return isfinite(elecCt);
}

static bool heatCheapEconomicDecisionForPoint(const String& date, int idx, int intervalMin, float elecCt,
                                               bool& elecCheaper, float* elecHeatCostOut = nullptr) {
  elecCheaper = false;
  if (date.length() < 10 || idx < 0 || intervalMin <= 0 || !isfinite(elecCt)) return false;

  const int minuteOfDay = idx * intervalMin;
  const float gasEur = gasEurM3ForControlAtLocal(date, minuteOfDay, gasTotalEnabled);
  if (!isfinite(gasEur)) return false;

  float ge = gasBoilerEff;
  if (ge < 0.05f) ge = 0.05f;
  if (ge > 1.0f) ge = 1.0f;

  float ee = elecBoilerEff;
  if (ee < 0.05f) ee = 0.05f;
  if (ee > 10.0f) ee = 10.0f;

  float kwhm3 = gasKwhPerM3;
  if (kwhm3 < 1.0f) kwhm3 = 1.0f;

  const float elecHeatCt = elecCt / ee;
  const float gasHeatCt = (gasEur * 100.0f) / (kwhm3 * ge);
  if (!isfinite(elecHeatCt) || !isfinite(gasHeatCt)) return false;

  if (elecHeatCostOut) *elecHeatCostOut = elecHeatCt;
  elecCheaper = (elecHeatCt + heatElecMarginCt) < gasHeatCt;
  return true;
}

static bool heatCheapPlanContainsCurrentBlock() {
  if (!heatCheapPlanValid || !timeIsValid()) return false;

  String date;
  int idx = -1;
  int intervalMin = 0;
  float elecCt = NAN;
  if (!heatCheapGetPointOffsetFromNow(0, date, idx, intervalMin, elecCt)) return false;
  if (intervalMin != heatCheapPlanIntervalMin) return false;

  if (heatCheapPlanStartDate == heatCheapPlanEndDate) {
    return date == heatCheapPlanStartDate &&
           idx >= heatCheapPlanStartIdx && idx < heatCheapPlanEndIdxExcl;
  }

  // Een 2-dagdeelvenster mag over 00:00 lopen. De selectie blijft aaneengesloten;
  // daarom horen op de startdag alle blokken vanaf startIdx en op de einddag alle
  // blokken vóór endIdxExcl bij hetzelfde gekozen venster.
  if (date == heatCheapPlanStartDate) return idx >= heatCheapPlanStartIdx;
  if (date == heatCheapPlanEndDate) return idx < heatCheapPlanEndIdxExcl;
  return false;
}

// Maak een nieuw plan vanaf NU tot 24:00.
// Alle resterende blokken worden bekeken; alleen aaneengesloten blokken waarin ELEC economisch goedkoper
// is mogen samen één venster vormen. Daardoor kan een later, goedkoper ELEC-venster bewust worden gekozen.
static bool heatCheapBuildBestPlanForRestOfToday(const String& currentDate) {
  static const int kMaxDayPoints = 128; // > 100 kwartieren op een 25-uurs DST-dag, ruim voldoende
  bool cheaper[kMaxDayPoints];
  float cost[kMaxDayPoints];
  int count = 0;
  int commonInterval = 0;

  for (int off = 0; off < kMaxDayPoints; off++) {
    String date;
    int idx = -1;
    int intervalMin = 0;
    float elecCt = NAN;
    if (!heatCheapGetPointOffsetFromNow(off, date, idx, intervalMin, elecCt)) break;
    if (date != currentDate) break;
    if (commonInterval == 0) commonInterval = intervalMin;
    if (intervalMin != commonInterval) break;

    bool isCheaper = false;
    float elecHeatCt = NAN;
    const bool valid = heatCheapEconomicDecisionForPoint(date, idx, intervalMin, elecCt, isCheaper, &elecHeatCt);
    cheaper[count] = valid && isCheaper;
    cost[count] = (valid && isfinite(elecHeatCt)) ? elecHeatCt : NAN;
    count++;
  }

  heatCheapPlanClearWindow();
  if (count <= 0 || commonInterval <= 0) return false;

  int wanted = heatCheapNBlocks;
  if (wanted < 1) wanted = 1;
  if (wanted > 96) wanted = 96;

  // Eerst bepalen hoeveel opeenvolgende geschikte ELEC-blokken maximaal nog beschikbaar zijn.
  int longestRun = 0;
  int currentRun = 0;
  for (int i = 0; i < count; i++) {
    if (cheaper[i]) {
      currentRun++;
      if (currentRun > longestRun) longestRun = currentRun;
    } else {
      currentRun = 0;
    }
  }
  if (longestRun <= 0) return false;

  // N is maximum. Als N nergens haalbaar is, nemen we de langste geldige reeks die wel bestaat.
  const int windowLen = (longestRun < wanted) ? longestRun : wanted;

  // Kies van alle geldige vensters met die lengte de goedkoopste.
  double bestSum = 1e30;
  int bestOffset = -1;
  for (int startOff = 0; startOff + windowLen <= count; startOff++) {
    double sum = 0.0;
    bool validWindow = true;
    for (int k = 0; k < windowLen; k++) {
      const int pos = startOff + k;
      if (!cheaper[pos] || !isfinite(cost[pos])) {
        validWindow = false;
        break;
      }
      sum += cost[pos];
    }
    if (validWindow && (bestOffset < 0 || sum < bestSum)) {
      bestOffset = startOff;
      bestSum = sum;
    }
  }
  if (bestOffset < 0) return false;

  String startDate, endDate;
  int startIdx = -1, endIdx = -1;
  int startInterval = 0, endInterval = 0;
  float dummyCt = NAN;
  if (!heatCheapGetPointOffsetFromNow(bestOffset, startDate, startIdx, startInterval, dummyCt)) return false;
  if (!heatCheapGetPointOffsetFromNow(bestOffset + windowLen - 1, endDate, endIdx, endInterval, dummyCt)) return false;
  if (startInterval != endInterval || startDate != endDate) return false;

  heatCheapPlanStartDate = startDate;
  heatCheapPlanEndDate = endDate;
  heatCheapPlanStartIdx = startIdx;
  heatCheapPlanEndIdxExcl = endIdx + 1;
  heatCheapPlanIntervalMin = startInterval;
  heatCheapPlanSelectedBlocks = windowLen;
  heatCheapPlanValid = true;
  return true;
}

// Maak bij 2 dagdelen een plan vanaf NU tot de eerstvolgende ingestelde tijdgrens.
// De selectie-algoritme is bewust gelijk aan de bestaande dagplanning: N blijft een maximum,
// GAS-blokken breken een reeks en bij te weinig N wordt de langste beschikbare reeks gebruikt.
static bool heatCheapBuildBestPlanUntilBoundary(const String& currentDate, const HeatCheapDayPartWindow& window) {
  static const int kMaxWindowPoints = 128;
  bool cheaper[kMaxWindowPoints];
  float cost[kMaxWindowPoints];
  int count = 0;
  int commonInterval = 0;

  for (int off = 0; off < kMaxWindowPoints; off++) {
    String date;
    int idx = -1;
    int intervalMin = 0;
    float elecCt = NAN;
    if (!heatCheapGetPointOffsetFromNowAcrossTomorrow(off, date, idx, intervalMin, elecCt)) break;

    const int pointMinute = idx * intervalMin;
    if (date == window.deadlineDate && pointMinute >= window.deadlineMin) break;
    if (date != currentDate && date != window.deadlineDate) break;

    if (commonInterval == 0) commonInterval = intervalMin;
    if (intervalMin != commonInterval) break;

    bool isCheaper = false;
    float elecHeatCt = NAN;
    const bool valid = heatCheapEconomicDecisionForPoint(date, idx, intervalMin, elecCt, isCheaper, &elecHeatCt);
    cheaper[count] = valid && isCheaper;
    cost[count] = (valid && isfinite(elecHeatCt)) ? elecHeatCt : NAN;
    count++;
  }

  heatCheapPlanClearWindow();
  if (count <= 0 || commonInterval <= 0) return false;

  int wanted = heatCheapNBlocks;
  if (wanted < 1) wanted = 1;
  if (wanted > 96) wanted = 96;

  int longestRun = 0;
  int currentRun = 0;
  for (int i = 0; i < count; i++) {
    if (cheaper[i]) {
      currentRun++;
      if (currentRun > longestRun) longestRun = currentRun;
    } else {
      currentRun = 0;
    }
  }
  if (longestRun <= 0) return false;

  const int windowLen = (longestRun < wanted) ? longestRun : wanted;

  double bestSum = 1e30;
  int bestOffset = -1;
  for (int startOff = 0; startOff + windowLen <= count; startOff++) {
    double sum = 0.0;
    bool validWindow = true;
    for (int k = 0; k < windowLen; k++) {
      const int pos = startOff + k;
      if (!cheaper[pos] || !isfinite(cost[pos])) {
        validWindow = false;
        break;
      }
      sum += cost[pos];
    }
    if (validWindow && (bestOffset < 0 || sum < bestSum)) {
      bestOffset = startOff;
      bestSum = sum;
    }
  }
  if (bestOffset < 0) return false;

  String startDate, endDate;
  int startIdx = -1, endIdx = -1;
  int startInterval = 0, endInterval = 0;
  float dummyCt = NAN;
  if (!heatCheapGetPointOffsetFromNowAcrossTomorrow(bestOffset, startDate, startIdx, startInterval, dummyCt)) return false;
  if (!heatCheapGetPointOffsetFromNowAcrossTomorrow(bestOffset + windowLen - 1, endDate, endIdx, endInterval, dummyCt)) return false;
  if (startInterval != endInterval) return false;

  heatCheapPlanStartDate = startDate;
  heatCheapPlanEndDate = endDate;
  heatCheapPlanStartIdx = startIdx;
  heatCheapPlanEndIdxExcl = endIdx + 1;
  heatCheapPlanIntervalMin = startInterval;
  heatCheapPlanSelectedBlocks = windowLen;
  heatCheapPlanValid = true;
  return true;
}

static bool heatCheapEnsurePlanForRestOfToday() {
  String currentDate;
  int currentIdx = -1;
  int currentInterval = 0;
  float currentElecCt = NAN;
  if (!heatCheapGetPointOffsetFromNow(0, currentDate, currentIdx, currentInterval, currentElecCt)) {
    // Ontbrekende prijsdata is fail-safe UIT, maar vernietigt een bestaand plan niet.
    return false;
  }

  bool currentElecCheaper = false;
  if (!heatCheapEconomicDecisionForPoint(currentDate, currentIdx, currentInterval, currentElecCt, currentElecCheaper)) {
    // Ook hier: ontbrekende geldige gasprijs => fail-safe UIT, plan bewaren.
    return false;
  }

  // Temperatuur-warmtevraag ook tijdens een GAS-periode blijven volgen. Zo onthouden we bijvoorbeeld
  // dat de boiler via gas/zon de doeltemperatuur heeft bereikt en starten we later niet opnieuw zolang
  // hij nog niet minimaal 5 °C onder de doeltemperatuur is afgekoeld.
  bool demandRestarted = false;
  const bool temperatureAllowsHeating = heatCheapTemperatureDemandAllowsHeating(demandRestarted);

  if (!currentElecCheaper) {
    // Echte economische omslag naar GAS: direct stoppen. Een latere ELEC-periode op dezelfde dag
    // krijgt opnieuw een volledig nieuw plan vanaf dat moment tot 24:00.
    heatCheapResetPeriodKeepTemperatureDemand();
    return false;
  }

  // Stand 1 behoudt de bestaande kalenderdag-latch. Bij stand 2 is de eerstvolgende
  // tijdgrens de latch-sleutel, zodat 07:00/19:00 onafhankelijk nieuwe kansen openen
  // en een nachtvenster juist wél over 00:00 kan doorlopen.
  HeatCheapDayPartWindow dayPartWindow;
  const bool useTwoDayParts = (heatCheapDayParts == 2) && heatCheapGetCurrentDayPartWindow(dayPartWindow);
  const String periodKey = useTwoDayParts ? dayPartWindow.key : currentDate;
  if (heatCheapPeriodLatched && heatCheapPeriodDate.length() && heatCheapPeriodDate != periodKey) {
    heatCheapResetPeriodKeepTemperatureDemand();
  }

  if (demandRestarted) {
    // Na opnieuw afkoelen tot minimaal 5 °C onder de boiler-doeltemperatuur krijgt deze warmtevraag een nieuw dagplan,
    // ook als het economische ELEC-advies zelf ononderbroken doorloopt.
    heatCheapResetPeriodKeepTemperatureDemand();
  }

  if (!temperatureAllowsHeating) {
    // Alleen wanneer "Gebruik boiler-doeltemperatuur bij ELEC-blokken" AAN staat kan
    // temperatureAllowsHeating false worden: doeltemperatuur bereikt = huidige heating stoppen.
    // Met deze toggle UIT blijft dit true en wordt het gekozen N-venster gewoon afgemaakt.
    heatCheapPlanClearWindow();
    return false;
  }

  // Een nieuw goedkoop-blokkenplan wordt pas gestart wanneer het bestaande zichtbare advies werkelijk ELEC is.
  // De ruwe economische vergelijking hierboven blijft wél direct leidend om bij GAS meteen te stoppen.
  if (heatRecommendGasNow) return false;

  if (heatCheapPeriodLatched) {
    return heatCheapPlanContainsCurrentBlock();
  }

  // Eerste activering, nieuwe ELEC-periode of nieuwe temperatuur-warmtevraag.
  // Stand 1: bestaande planning vanaf NU tot 24:00.
  // Stand 2: planning vanaf NU tot de eerstvolgende ingestelde dagdeelgrens.
  heatCheapPeriodLatched = true;
  heatCheapPeriodDate = periodKey;
  const bool built = useTwoDayParts
      ? heatCheapBuildBestPlanUntilBoundary(currentDate, dayPartWindow)
      : heatCheapBuildBestPlanForRestOfToday(currentDate);
  if (!built) {
    // In stand 2 kan morgen-data later beschikbaar komen. Zonder geldig plan laten we
    // daarom een volgende tick opnieuw proberen; stand 1 blijft exact de oude latch behouden.
    if (useTwoDayParts) heatCheapPeriodLatched = false;
    return false;
  }

  return heatCheapPlanContainsCurrentBlock();
}


bool heatCheapTemperatureSensorAssignedNow() { return heatCheapTempSensorAssigned; }
bool heatCheapTemperatureModeEnabledNow() { return heatCheapTempModeEnabled; }
bool heatCheapTemperatureSensorValidNow() { return heatCheapTempSensorValid; }
bool heatCheapTemperatureDemandActiveNow() { return heatCheapTempDemandActive; }
float heatCheapTemperatureCurrentCNow() { return heatCheapTempCurrentC; }
float heatCheapTemperatureStartCNow() { return heatCheapTempStartC; }
float heatCheapTemperatureStopCNow() { return heatCheapTempStopC; }
String heatCheapTemperatureSensorNameNow() { return heatCheapTempSensorName; }

void heatCompareUpdateNow() {
  heatPriceDecisionValidNow = false;
  heatGasCtPerKwhHeatNow  = NAN;
  heatElecCtPerKwhHeatNow = NAN;

  if (!heatAutoEnabled) return;

  const float elecCt = elecCtKwhNow();
  const float gasEur = gasEurM3ForControlNow(gasTotalEnabled);
  if (isnan(elecCt) || isnan(gasEur)) return;

  float ge = gasBoilerEff;
  if (ge < 0.05f) ge = 0.05f;
  if (ge > 1.0f) ge = 1.0f;

  float ee = elecBoilerEff;
  if (ee < 0.05f) ee = 0.05f;
  if (ee > 10.0f) ee = 10.0f;

  float kwhm3 = gasKwhPerM3;
  if (kwhm3 < 1.0f) kwhm3 = 1.0f;

  heatElecCtPerKwhHeatNow = elecCt / ee;
  heatGasCtPerKwhHeatNow  = (gasEur * 100.0f) / (kwhm3 * ge);
  if (isnan(heatGasCtPerKwhHeatNow) || isnan(heatElecCtPerKwhHeatNow)) return;

  // Vanaf hier is werkelijk met twee geldige, actuele prijzen vergeleken.
  heatPriceDecisionValidNow = true;

  const unsigned long nowMs = millis();
  const unsigned long minMs = (unsigned long)max(0, heatMinSwitchMin) * 60UL * 1000UL;

  bool wantGas = heatRecommendGasNow;
  if (!isnan(heatGasCtPerKwhHeatNow) && !isnan(heatElecCtPerKwhHeatNow)) {
    const float elecEff = heatElecCtPerKwhHeatNow + heatElecMarginCt;
    wantGas = !(elecEff < heatGasCtPerKwhHeatNow);
  }

  if (wantGas != heatRecommendGasNow) {
    if (heatLastSwitchMs == 0 || (nowMs - heatLastSwitchMs) >= minMs) {
      heatRecommendGasNow = wantGas;
      heatLastSwitchMs = nowMs;
    }
  }
}

const char* heatRecommendationLabelNow() {
  if (!heatPriceDecisionValidNow) return "unknown";
  return heatRecommendGasNow ? "gas" : "elec";
}

bool heatExecElecActiveNow() {
  if (!heatAutoEnabled) { if (heatCheapPeriodLatched || heatCheapPlanValid) heatCheapPlanInvalidate(); return false; }
  if (!heatExecElecWhenCheaper) { if (heatCheapPeriodLatched || heatCheapPlanValid) heatCheapPlanInvalidate(); return false; }
  if (legRunning) return false;
  if (controlMode == MODE_FORCE_HEAT) return false;

  heatCompareUpdateNow();
  if (!heatPriceDecisionValidNow) return false;

  // Toggle uit = exact de bestaande step309-werking behouden.
  if (!heatCheapBlocksEnabled) {
    if (heatCheapPeriodLatched || heatCheapPlanValid) heatCheapPlanInvalidate();
    return !heatRecommendGasNow;
  }

  // Met een vaste elektraprijs zijn er geen toekomstige prijsblokken om te optimaliseren.
  // In dat geval blijft de bestaande ELEC-uitvoering leidend.
  if (elecPriceSource != 0) {
    if (heatCheapPeriodLatched || heatCheapPlanValid) heatCheapPlanInvalidate();
    return !heatRecommendGasNow;
  }

  // De bestaande advies-switch blijft bepalen wanneer ELEC mag starten.
  // De slimme planning voegt daar twee dingen aan toe:
  //  1) alleen het goedkoopste aaneengesloten N-venster uitvoeren;
  //  2) bij een actuele economische omslag naar GAS direct stoppen.
  const bool inSelectedCheapWindow = heatCheapEnsurePlanForRestOfToday();
  return inSelectedCheapWindow && !heatRecommendGasNow;
}
