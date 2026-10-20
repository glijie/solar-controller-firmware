#pragma once
#include <Arduino.h>

// Stap 3b (veilig): MQTT blijft via wrappers gekoppeld aan de bestaande .ino code.
// Nog geen echte verplaatsing van MQTT-internals om afhankelijkheden niet te breken.

namespace MQTT {
  bool handleTuningMaxStepPct(const String& t, const String& p, const String& base);
  bool handleLegScheduleEnabled(const String& t, const String& p, const String& base);
  bool handleLegDow(const String& t, const String& p, const String& base);
  bool handleLegStartHour(const String& t, const String& p, const String& base);
  bool handleLegStartMinute(const String& t, const String& p, const String& base);
  bool handleLegDurationMin(const String& t, const String& p, const String& base);
  bool handleLegUseTempSensor(const String& t, const String& p, const String& base);
  bool handleLegTargetTempC(const String& t, const String& p, const String& base);
  bool handleLegHoldMin(const String& t, const String& p, const String& base);
    bool handleElecPriceSource(const String& t, const String& p, const String& base);
  bool handleElecFixedCtKwhAllIn(const String& t, const String& p, const String& base);
  bool handleHeatAutoEnabled(const String& t, const String& p, const String& base);
  bool handleHeatExecElec(const String& t, const String& p, const String& base);
  bool handleHeatCheapBlocksEnabled(const String& t, const String& p, const String& base);
  bool handleHeatCheapUseBoilerTarget(const String& t, const String& p, const String& base);
  bool handleHeatCheapBlocksMax(const String& t, const String& p, const String& base);
  bool handleNpEnabled(const String& t, const String& p, const String& base);
  bool handleNpArea(const String& t, const String& p, const String& base);
  bool handleNpResMin(const String& t, const String& p, const String& base);
  bool handleNpInclTaxes(const String& t, const String& p, const String& base);
  bool handleNpEnergyTaxCt(const String& t, const String& p, const String& base);
  bool handleNpInkoopCt(const String& t, const String& p, const String& base);
  bool handleNpBtwPct(const String& t, const String& p, const String& base);
  bool handleNpBoostEnabled(const String& t, const String& p, const String& base);
  bool handleNpBoostPriceSource(const String& t, const String& p, const String& base);
  bool handleNpBoostThresholdCt(const String& t, const String& p, const String& base);
  bool handleNpBoostNBlocks(const String& t, const String& p, const String& base);
  bool handleSunSchedMode(const String& t, const String& p, const String& base);
  bool handleSunManualStart(const String& t, const String& p, const String& base);
  bool handleSunManualEnd(const String& t, const String& p, const String& base);
  bool handleSunCheapestBlocks(const String& t, const String& p, const String& base);
  bool handleSunDaylightMarginMin(const String& t, const String& p, const String& base);
  bool handleSunLat(const String& t, const String& p, const String& base);
  bool handleSunLon(const String& t, const String& p, const String& base);
  bool handleSunDashboardLabel(const String& t, const String& p, const String& base);
  bool handleNormalTargetExportW(const String& t, const String& p, const String& base);
  bool handleNormalMaxBoilerPowerW(const String& t, const String& p, const String& base);
  bool handleNormalMinStepPercent(const String& t, const String& p, const String& base);
  bool handleNormalMinIntervalS(const String& t, const String& p, const String& base);
  bool handleLimitsMaxOutputPct(const String& t, const String& p, const String& base);
  bool handleLimitsLegionellaOutputPct(const String& t, const String& p, const String& base);
  bool handleTuningAlgorithm(const String& t, const String& p, const String& base);
  bool handleTuningStepFactor(const String& t, const String& p, const String& base);
  bool handleTuningDeadbandW(const String& t, const String& p, const String& base);
  bool handleTuningSet(const String& t, const String& p, const String& base);
  bool handleLegionellaConfigSet(const String& t, const String& p, const String& base);
  bool handleLegionellaCancel(const String& t, const String& p, const String& base);
  bool handleLegionellaRunNow(const String& t, const String& p, const String& base);
  bool handleForceHeat(const String& t, const String& p, const String& base);
  bool handleRelaySet(const String& t, const String& p, const String& base);
  void begin();
  void loop();
  bool ensureConnected();
}
