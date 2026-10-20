#include "legionella.h"
#include "app_globals.h"
#include "MultiController.h"
#include "RelayControl.h"

namespace {
  bool gLegRelayAutoCycleActive = false;
  bool gLegRelayOnPending = false;
  bool gLegRelayOffPending = false;
  unsigned long gLegRelayOnDeadlineMs = 0;
  unsigned long gLegRelayOffDeadlineMs = 0;

  void legRelayAutomationStart() {
    gLegRelayOnPending = false;
    gLegRelayOffPending = false;
    gLegRelayAutoCycleActive = false;

    if (!legRelayAutoEnabled || !relayIsEnabled()) return;

    gLegRelayAutoCycleActive = true;

    // Bij 0 minuten direct AAN. Bij een ingestelde vertraging blijft handmatig
    // schakelen tussendoor mogelijk, maar het automatische startmoment blijft staan.
    if (legRelayStartDelayMin <= 0) {
      relaySetAutomaticOn(true);
      return;
    }

    gLegRelayOnDeadlineMs = millis() + (unsigned long)legRelayStartDelayMin * 60000UL;
    gLegRelayOnPending = true;
  }

  void legRelayAutomationStop() {
    if (!gLegRelayAutoCycleActive) {
      gLegRelayOnPending = false;
      gLegRelayOffPending = false;
      return;
    }

    gLegRelayAutoCycleActive = false;
    gLegRelayOnPending = false;

    if (!legRelayAutoEnabled || !relayIsEnabled()) {
      gLegRelayOffPending = false;
      return;
    }

    // Handmatig schakelen tijdens de run blijft mogelijk. Als automatisch relais
    // is ingeschakeld, bepaalt het einde van de Legionella-run uiteindelijk wel
    // de UIT-status (direct of na de ingestelde nalooptijd).
    if (legRelayOffDelayMin <= 0) {
      relaySetAutomaticOn(false);
      gLegRelayOffPending = false;
      return;
    }

    gLegRelayOffDeadlineMs = millis() + (unsigned long)legRelayOffDelayMin * 60000UL;
    gLegRelayOffPending = true;
  }

  void legRelayAutomationTick() {
    if (gLegRelayOnPending) {
      if (!legRelayAutoEnabled || !relayIsEnabled()) {
        gLegRelayOnPending = false;
      } else if ((long)(millis() - gLegRelayOnDeadlineMs) >= 0) {
        relaySetAutomaticOn(true);
        gLegRelayOnPending = false;
      }
    }

    if (!gLegRelayOffPending) return;

    if (!legRelayAutoEnabled || !relayIsEnabled()) {
      gLegRelayOffPending = false;
      return;
    }

    if ((long)(millis() - gLegRelayOffDeadlineMs) >= 0) {
      relaySetAutomaticOn(false);
      gLegRelayOffPending = false;
    }
  }
}

void legStart(const String& reason) {
  if (multiControllerIsSlaveRole() && multiControllerHasNetworkPeersConfigured() && !multiControllerSlaveLegionellaOverrideEnabled()) {
    legReason = "MASTER_LEADING";
    legRunning = false;
    legHoldActive = false;
    legHoldEndEpoch = 0;
    legEndEpoch = 0;
    return;
  }

  legRelayAutomationStart();

  legRunning    = true;
  legHoldActive = false;
  legReason     = reason;

  time_t now = time(nullptr);
  if (timeIsValid()) {
    uint32_t today = dateIntLocal(now);
    legLastRunDateInt = today;
    legLastRunEpoch   = (uint32_t)now;
    saveConfig();
    legEndEpoch = now + (time_t)legDurationMin * 60;
  } else {
    legEndEpoch = 0;
  }

  setLegionellaOutput(true);
}

void legStop(const String& reason) {
  legRunning      = false;
  legHoldActive   = false;
  legHoldEndEpoch = 0;
  legEndEpoch     = 0;
  legReason       = reason;

  // Markeer 'succes' alleen bij echte voltooiing (target + hold)
  if (reason == "DONE" && timeIsValid()) {
    time_t now = time(nullptr);
    legLastSuccessEpoch = (uint32_t)now;
    saveConfig();
  }

  setBoilerDutyPercent(0.0f);
  legRelayAutomationStop();
}

void legTick() {
  if (!legRunning) return;

  updateTempIfNeeded();

  bool useTemp = (legUseTempSensor && ds18Available && tempIsValid(lastTempC));
  time_t now = time(nullptr);

  // fallback zonder tijdsync: millis duration
  static unsigned long legStartMsFallback = 0;
  if (!timeIsValid()) {
    if (legStartMsFallback == 0) legStartMsFallback = millis();
    unsigned long elapsedMs = millis() - legStartMsFallback;
    unsigned long durMs = (unsigned long)legDurationMin * 60000UL;
    if (elapsedMs >= durMs) {
      legStartMsFallback = 0;
      legStop("DONE");
      return;
    }
    setLegionellaOutput(true);
    return;
  }

  // time-based timeout (altijd)
  if (legEndEpoch > 0 && now >= legEndEpoch) {
    legStop("TIMEOUT");
    return;
  }

  // zonder sensor: blijf verwarmen tot timeout
  if (!useTemp) {
    setLegionellaOutput(true);
    return;
  }

  // met sensor: warm tot target, hold X min op target
  const float HYST = 0.3f;

  if (!legHoldActive) {
    if (lastTempC >= legTargetTempC) {
      legHoldActive = true;
      legHoldEndEpoch = now + (time_t)legHoldMin * 60;
    }
  }

  if (legHoldActive) {
    if (now >= legHoldEndEpoch) {
      legStop("DONE");
      return;
    }

    if (lastTempC < (legTargetTempC - HYST)) {
      setLegionellaOutput(true);
    } else if (lastTempC > (legTargetTempC + HYST)) {
      setLegionellaOutput(false);
    }
    return;
  }

  setLegionellaOutput(true);
}

void legSchedulerTick() {
  legRelayAutomationTick();
  if (configMode) return;
  if (legRunning) return;
  if (!timeIsValid()) return;

  // STEP 5 (Legionella): Temperature is leading in TEMP-mode.
  // If TEMP-mode is enabled, we treat "target temp held for holdMin minutes" as a weekly success,
  // even if the boiler reached this temperature via PV/normal heating (so we can skip unnecessary force-heat).
  if (legUseTempSensor) {
    updateTempIfNeeded();
    bool sensor_ok = ds18Available && tempIsValid(lastTempC);
    if (sensor_ok) {
      const float HYST = 0.2f; // small hysteresis to avoid chattering around the threshold
      static time_t aboveSince = 0;

      time_t now = time(nullptr);

      if (lastTempC >= legTargetTempC) {
        if (aboveSince == 0) aboveSince = now;
      } else if (lastTempC < (legTargetTempC - HYST)) {
        aboveSince = 0;
      }

      if (aboveSince > 0) {
        const uint32_t need_s = (uint32_t)legHoldMin * 60UL;
        if (need_s == 0 || (uint32_t)(now - aboveSince) >= need_s) {
          // Avoid excessive flash writes: only update success timestamp if it's new or sufficiently old.
          const uint32_t min_update_gap_s = 30UL * 60UL; // 30 minutes
          bool shouldSet = (legLastSuccessEpoch == 0) ||
                           ((uint32_t)now >= legLastSuccessEpoch &&
                            ((uint32_t)now - legLastSuccessEpoch) >= min_update_gap_s);

          if (shouldSet) {
            legLastSuccessEpoch = (uint32_t)now;
            // Optional: keep last "reason" informative for MQTT/UI (not used for logic).
            legReason = "PASSIVE_TEMP_OK";
            saveConfig();
          }
        }
      }
    }
  }

  // If automatic execution is disabled, we stop here (but passive success above still works).
  if (!legScheduleEnabled) return;

  // In slave-modus blijft de master standaard leidend. Alleen als de expliciete
  // override-optie aan staat, mag de slave zijn lokale legionella-programma
  // autonoom starten op basis van zijn eigen sensor- en 7-dagenlogica.
  if (multiControllerIsSlaveRole() && multiControllerHasNetworkPeersConfigured() && !multiControllerSlaveLegionellaOverrideEnabled()) return;

  time_t now = time(nullptr);
  struct tm tmNow;
  localtime_r(&now, &tmNow);

  // STEP 4 (Legionella): If planning is enabled AND temperature-mode is enabled,
  // only execute the scheduled run when we're "DUE" (no successful cycle in last 7 days).
  // - If the temp sensor is missing/invalid, we treat it as DUE (so planning still works without a sensor).
  if (legUseTempSensor) {
    bool sensor_ok = ds18Available && tempIsValid(lastTempC);
    bool due = true;

    if (sensor_ok && legLastSuccessEpoch > 0) {
      // 7 days = 168 hours
      const uint32_t window_s = 7UL * 24UL * 3600UL;

      // If the clock moved backwards, be conservative and treat as DUE.
      if (now >= (time_t)legLastSuccessEpoch) {
        due = ((uint32_t)(now - (time_t)legLastSuccessEpoch) >= window_s);
      } else {
        due = true;
      }
    }

    if (!due) return; // Skip scheduled start; we're still within the safe window.
  }

  // Run this check only once per new minute (consumer-proof, no need per second)
  static int lastCheckedMinute = -1;
  static uint32_t lastCheckedDate = 0;
  uint32_t today = dateIntLocal(now);

  if (tmNow.tm_min == lastCheckedMinute && today == lastCheckedDate) return;
  lastCheckedMinute = tmNow.tm_min;
  lastCheckedDate   = today;

  // Only on the configured weekday
  if (tmNow.tm_wday != legDow) return;

  // Build today's scheduled epoch at HH:MM:00 local
  struct tm sched = tmNow;
  sched.tm_sec  = 0;
  sched.tm_min  = legStartMinute;
  sched.tm_hour = legStartHour;

  time_t schedEpoch = mktime(&sched);

  // Trigger if we're within the scheduled minute
  if (now >= schedEpoch && now < (schedEpoch + 60)) {
    legStart("SCHEDULE");
  }
}


void setLegionellaOutput(bool heatOn) {
  float pct = 0.0f;
  if (heatOn) {
    if (multiControllerShouldUseGroupControlForScenario(MC_SCENARIO_PRIORITY_HEAT)) {
      pct = multiControllerGroupTargetMaxForScenario(MC_SCENARIO_PRIORITY_HEAT);
    } else {
      pct = legOutputLimitPercent;
      if (pct > maxOutputPercentLimit) pct = maxOutputPercentLimit;
    }
  }
  setBoilerDutyPercentForScenario(pct, MC_SCENARIO_PRIORITY_HEAT);
}
