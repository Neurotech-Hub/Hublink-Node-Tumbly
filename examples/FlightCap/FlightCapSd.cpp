#include "FlightCapSd.h"
#include "FlightCapLog.h"

namespace {

constexpr uint8_t kSdMountAttempts = 5;
constexpr uint32_t kSdMountRetryDelayMs = 50;
constexpr uint32_t kSdMountFirstDelayMs = 150;
constexpr uint32_t kSdDetectDebounceMs = 10;
constexpr uint32_t kSdResetSettleMs = 100;

bool tryMountSd(tumbly::HublinkNode &node) {
  if (node.sd().isMounted()) {
    return true;
  }
  for (uint8_t attempt = 0; attempt < kSdMountAttempts; ++attempt) {
    if (attempt == 0) {
      delay(kSdMountFirstDelayMs);
    } else {
      delay(kSdMountRetryDelayMs);
    }
    if (node.sd().begin()) {
      return true;
    }
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

FlightCapSdResult flightCapSdEnsure(tumbly::HublinkNode &node) {
  if (!flightCapSdCardDetected(node)) {
    flightCapSdReset(node);
    return FlightCapSdResult::DetectOpen;
  }
  if (tryMountSd(node)) {
    return FlightCapSdResult::Ready;
  }
  flightCapSdReset(node);
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
  if (node.sd().isMounted()) {
    node.sd().end();
  }
}

void flightCapSdReset(tumbly::HublinkNode &node) {
  flightCapSdRelease(node);
  pinMode(tumbly::PIN_SD_CS, OUTPUT);
  digitalWrite(tumbly::PIN_SD_CS, HIGH);
  pinMode(tumbly::PIN_SD_EN, OUTPUT);
  digitalWrite(tumbly::PIN_SD_EN, HIGH);
  delay(kSdResetSettleMs);
}
