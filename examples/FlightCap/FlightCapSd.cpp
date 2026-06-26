#include "FlightCapSd.h"
#include "FlightCapLog.h"
#include <SD.h>

namespace {

constexpr uint8_t kSdMountAttempts = 5;
constexpr uint32_t kSdMountRetryDelayMs = 50;
constexpr uint32_t kSdMountFirstDelayMs = 150;
constexpr uint32_t kSdDetectDebounceMs = 10;
constexpr uint32_t kSdResetSettleMs = 100;

bool tryMountSd(tumbly::HublinkNode &node) {
  if (node.sd().isMounted() && node.sd().cardType() != CARD_NONE) {
    return true;
  }
  if (node.sd().isMounted()) {
    node.sd().end();
  }
  for (uint8_t attempt = 0; attempt < kSdMountAttempts; ++attempt) {
    if (attempt == 0) {
      delay(kSdMountFirstDelayMs);
    } else {
      delay(kSdMountRetryDelayMs);
    }
    if (node.sd().begin() && node.sd().cardType() != CARD_NONE) {
      Serial.print(F("FlightCap: SD mount ok type="));
      Serial.print(node.sd().cardType());
      Serial.print(F(" size="));
      Serial.println(node.sd().cardSizeBytes());
      return true;
    }
    node.sd().end();
  }
  return false;
}

} // namespace

bool flightCapSdCardDetected(tumbly::HublinkNode &node) {
  if (!node.readSdDetect()) {
    return false;
  }
  delay(kSdDetectDebounceMs);
  return node.readSdDetect();
}

bool flightCapSdMount(tumbly::HublinkNode &node) {
  if (!flightCapSdCardDetected(node)) {
    flightCapSdUnmount(node);
    return false;
  }
  if (tryMountSd(node)) {
    return true;
  }
  flightCapSdUnmount(node);
  return false;
}

void flightCapSdUnmount(tumbly::HublinkNode &node) {
  const bool wasMounted = node.sd().isMounted();
  if (wasMounted) {
    node.sd().end();
  }
  pinMode(tumbly::PIN_SD_CS, OUTPUT);
  digitalWrite(tumbly::PIN_SD_CS, HIGH);
  pinMode(tumbly::PIN_SD_EN, OUTPUT);
  digitalWrite(tumbly::PIN_SD_EN, HIGH);
  delay(kSdResetSettleMs);
  if (wasMounted) {
    flightCapLog(F("FlightCap: SD unmount"));
  }
}

FlightCapSdResult flightCapSdEnsure(tumbly::HublinkNode &node) {
  if (!flightCapSdCardDetected(node)) {
    flightCapSdUnmount(node);
    return FlightCapSdResult::DetectOpen;
  }
  if (flightCapSdMount(node)) {
    return FlightCapSdResult::Ready;
  }
  return FlightCapSdResult::MountFailed;
}

void flightCapSdLogEnsureFailure(FlightCapSdResult result) {
  switch (result) {
  case FlightCapSdResult::DetectOpen:
    flightCapLog(F("FlightCap: SD detect open"));
    break;
  case FlightCapSdResult::MountFailed:
    flightCapLog(F("FlightCap: SD mount failed"));
    break;
  case FlightCapSdResult::Ready:
    break;
  }
}

void flightCapSdRelease(tumbly::HublinkNode &node) {
  flightCapSdUnmount(node);
}

void flightCapSdReset(tumbly::HublinkNode &node) {
  flightCapSdUnmount(node);
}
