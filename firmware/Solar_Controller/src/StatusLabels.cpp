#include "StatusLabels.h"

#include "MultiController.h"
#include "app_globals.h"

String modeLabel() {
  if (mcEnabled && multiControllerIsRemoteTargetControlActive()) {
    const uint8_t remoteScenario = multiControllerGetScenario();
    if (remoteScenario == MC_SCENARIO_PRIORITY_HEAT) return "LEGIONELLA";
    if (remoteScenario == MC_SCENARIO_MANUAL_FORCE) return "FORCE_HEAT";
    if (remoteScenario == MC_SCENARIO_CHEAP || remoteScenario == MC_SCENARIO_NEGATIVE) return "PRICE_BOOST";
    if (remoteScenario == MC_SCENARIO_SURPLUS) return "NORMAL";
  }
  if (legRunning) return "LEGIONELLA";
  if (controlMode == MODE_FORCE_HEAT) return "FORCE_HEAT";
  if (npBoostActiveNow) return "PRICE_BOOST";
  if (sunSchedMode == 3) return "NORMAL_OFF";
  if (!appNormalAllowedByScheduleNow()) return "NORMAL_BLOCKED";
  return "NORMAL";
}

String legStateLabel() {
  return legRunning ? "RUNNING" : "IDLE";
}

String normalAlgLabel() {
  return (normalAlg == 1) ? "STEP" : "DIRECT";
}

String dowName(int d) {
  switch (d) {
    case 0: return "Zondag";
    case 1: return "Maandag";
    case 2: return "Dinsdag";
    case 3: return "Woensdag";
    case 4: return "Donderdag";
    case 5: return "Vrijdag";
    case 6: return "Zaterdag";
  }
  return "Zondag";
}
