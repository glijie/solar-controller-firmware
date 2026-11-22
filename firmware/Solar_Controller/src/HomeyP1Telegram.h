#pragma once

#include <stddef.h>
#include <stdint.h>

struct HomeyP1Sample {
  long netPowerW;
  float phaseA[3];
  bool hasPhases;
};

// A WebSocket message can contain part of a telegram or several telegrams.
class HomeyP1Telegram {
public:
  bool push(uint8_t byte, HomeyP1Sample& sample);
  void reset() { telegramBytes = 0; }

private:
  void start();
  void readLine();

  uint32_t values[5]{};
  uint16_t telegramBytes = 0;
  uint16_t crc = 0;
  uint16_t receivedCrc = 0;
  uint8_t fields = 0;
  uint8_t crcDigits = 0;
  uint8_t lineLength = 0;
  bool inCrc = false;
  bool crcCarriageReturn = false;
  char line[48]{};
};
