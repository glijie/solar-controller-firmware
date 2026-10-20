#pragma once
#include <Arduino.h>

bool pwmInit();
void pwmWriteDuty(uint32_t duty);
float multiControllerGroupTargetMaxForScenario(uint8_t scenario);
void setBoilerDutyPercentForScenario(float targetPercent, uint8_t forcedScenario);
void setBoilerDutyPercent(float targetPercent);
void setBoilerGroupDutyPercent(float targetPercent);
