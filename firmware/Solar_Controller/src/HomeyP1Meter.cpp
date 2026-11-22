#include "HomeyP1Meter.h"

#include <Arduino.h>
#include <IPAddress.h>
#include "third_party/WebSockets/src/WebSocketsClient.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr unsigned long kReconnectMs = 5000;
constexpr unsigned long kSampleMaxAgeMs = 4000;
constexpr uint32_t kTaskStackBytes = 4096;

struct Endpoint {
  uint32_t ip = 0;
  uint32_t generation = 0;
};

portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
Endpoint requested;
bool taskRunning = false;
bool sampleReady = false;
HomeyP1Sample latest{};
unsigned long lastReceivedMs = 0;

bool endpointCurrent(const Endpoint& endpoint) {
  portENTER_CRITICAL(&stateLock);
  const bool current = requested.generation == endpoint.generation;
  portEXIT_CRITICAL(&stateLock);
  return current;
}

void receiveFromDongle(const Endpoint& endpoint) {
  HomeyP1Telegram parser;
  WebSocketsClient socket;
  socket.begin(IPAddress(endpoint.ip), 80, "/ws", "");
  socket.setReconnectInterval(kReconnectMs);
  socket.enableHeartbeat(10000, 3000, 2);
  socket.onEvent([&](WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
      case WStype_DISCONNECTED:
        parser.reset();
        portENTER_CRITICAL(&stateLock);
        if (requested.generation == endpoint.generation) sampleReady = false;
        portEXIT_CRITICAL(&stateLock);
        return;
      case WStype_TEXT:
      case WStype_BIN:
      case WStype_FRAGMENT_TEXT_START:
      case WStype_FRAGMENT_BIN_START:
      case WStype_FRAGMENT:
      case WStype_FRAGMENT_FIN:
        break;
      default:
        return;
    }

    HomeyP1Sample sample{};
    for (size_t i = 0; i < length; ++i) {
      if (!parser.push(payload[i], sample)) continue;
      const unsigned long now = millis();
      portENTER_CRITICAL(&stateLock);
      if (requested.generation == endpoint.generation) {
        latest = sample;
        lastReceivedMs = now;
        sampleReady = true;
      }
      portEXIT_CRITICAL(&stateLock);
    }
  });

  // Connection attempts and partial frame reads may block the library.
  // Keep that work outside the controller's main loop.
  while (endpointCurrent(endpoint)) {
    socket.loop();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  socket.disconnect();
}

void receiveTask(void*) {
  for (;;) {
    portENTER_CRITICAL(&stateLock);
    const Endpoint endpoint = requested;
    if (!endpoint.ip) taskRunning = false;
    portEXIT_CRITICAL(&stateLock);
    if (!endpoint.ip) break;
    receiveFromDongle(endpoint);
  }
  vTaskDelete(nullptr);
}
}

void homeyP1Update(const char* address) {
  IPAddress ip;
  const uint32_t nextIp = address && ip.fromString(address) ? static_cast<uint32_t>(ip) : 0;
  const unsigned long now = millis();
  static unsigned long lastStartTry = 0UL - kReconnectMs;

  portENTER_CRITICAL(&stateLock);
  if (requested.ip != nextIp) {
    requested.ip = nextIp;
    ++requested.generation;
    sampleReady = false;
    lastStartTry = now - kReconnectMs;
  }
  const bool start = nextIp && !taskRunning && (now - lastStartTry >= kReconnectMs);
  if (start) taskRunning = true;
  portEXIT_CRITICAL(&stateLock);

  if (!start) return;
  lastStartTry = now;
  if (xTaskCreate(receiveTask, "homey_p1", kTaskStackBytes, nullptr, 1, nullptr) != pdPASS) {
    portENTER_CRITICAL(&stateLock);
    taskRunning = false;
    portEXIT_CRITICAL(&stateLock);
  }
}

bool homeyP1Read(HomeyP1Sample& sample, unsigned long& receivedMs) {
  portENTER_CRITICAL(&stateLock);
  const bool ready = sampleReady;
  if (ready) {
    sample = latest;
    receivedMs = lastReceivedMs;
  }
  portEXIT_CRITICAL(&stateLock);
  return ready && (millis() - receivedMs <= kSampleMaxAgeMs);
}
