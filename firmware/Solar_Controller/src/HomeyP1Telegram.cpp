#include "HomeyP1Telegram.h"

#include <string.h>

namespace {
constexpr uint16_t kMaxTelegramBytes = 4096;
constexpr unsigned kPowerFields = 0x03;
constexpr unsigned kPhaseFields = 0x1c;

bool digit(char c) { return c >= '0' && c <= '9'; }

// DSMR sends kW and A with at most three decimal places. Store W and mA.
bool readScaled(const char* p, const char* suffix, uint32_t& value) {
  if (!digit(*p)) return false;
  uint32_t whole = 0;
  while (digit(*p)) {
    if (whole > 99999) return false;
    whole = whole * 10 + (*p++ - '0');
  }
  uint32_t fraction = 0;
  uint32_t factor = 100;
  if (*p == '.') {
    if (!digit(*++p)) return false;
    while (digit(*p)) {
      if (!factor) return false;
      fraction += factor * (*p++ - '0');
      factor /= 10;
    }
  }
  if (strcmp(p, suffix) != 0) return false;
  value = whole * 1000 + fraction;
  return true;
}

int hexDigit(uint8_t byte) {
  if (byte >= '0' && byte <= '9') return byte - '0';
  byte |= 0x20;
  return byte >= 'a' && byte <= 'f' ? byte - 'a' + 10 : -1;
}
}

void HomeyP1Telegram::start() {
  telegramBytes = crc = receivedCrc = 0;
  fields = crcDigits = lineLength = 0;
  inCrc = crcCarriageReturn = false;
}

void HomeyP1Telegram::readLine() {
  static constexpr const char* prefixes[] = {
    "1-0:1.7.0(", "1-0:2.7.0(", "1-0:31.7.0(", "1-0:51.7.0(", "1-0:71.7.0(",
  };
  if (lineLength >= sizeof(line)) return;
  if (lineLength && line[lineLength - 1] == '\r') --lineLength;
  line[lineLength] = '\0';
  for (unsigned i = 0; i < 5; ++i) {
    const size_t length = strlen(prefixes[i]);
    if (strncmp(line, prefixes[i], length) != 0) continue;
    const unsigned bit = 1u << i;
    if ((fields & bit) || !readScaled(line + length, i < 2 ? "*kW)" : "*A)", values[i])) {
      reset();
      return;
    }
    fields |= bit;
    return;
  }
}

bool HomeyP1Telegram::push(uint8_t byte, HomeyP1Sample& sample) {
  if (byte == '/' && (!telegramBytes || !lineLength || inCrc)) {
    start();
  } else if (!telegramBytes) {
    return false;
  }
  if (!byte || ++telegramBytes > kMaxTelegramBytes) {
    reset();
    return false;
  }

  if (inCrc) {
    if (byte == '\n') {
      reset();
      if (crcDigits != 4 || receivedCrc != crc || (fields & kPowerFields) != kPowerFields) return false;
      sample.netPowerW = static_cast<long>(values[0]) - static_cast<long>(values[1]);
      sample.hasPhases = (fields & kPhaseFields) == kPhaseFields;
      if (sample.hasPhases) {
        for (unsigned i = 0; i < 3; ++i) sample.phaseA[i] = values[i + 2] / 1000.0f;
      }
      return true;
    }
    if (crcDigits == 4 && byte == '\r' && !crcCarriageReturn) {
      crcCarriageReturn = true;
      return false;
    }
    const int nibble = hexDigit(byte);
    if (crcDigits == 4 || nibble < 0) {
      reset();
      return false;
    }
    receivedCrc = (receivedCrc << 4) | nibble;
    ++crcDigits;
    return false;
  }

  // DSMR CRC-16 covers the slash through the exclamation mark, inclusive.
  crc ^= byte;
  for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (crc & 1 ? 0xa001 : 0);
  if (byte == '!' && !lineLength) {
    inCrc = true;
  } else if (byte == '\n') {
    readLine();
    lineLength = 0;
  } else if (lineLength < sizeof(line)) {
    if (lineLength < sizeof(line) - 1) line[lineLength] = static_cast<char>(byte);
    ++lineLength;
  }
  return false;
}
