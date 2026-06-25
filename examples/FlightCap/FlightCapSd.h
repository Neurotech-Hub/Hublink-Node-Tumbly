#pragma once

#include <HublinkNodeTumbly.h>

enum class FlightCapSdResult : uint8_t {
  Ready,
  DetectOpen,
  MountFailed,
};

/// Debounced card-detect (LOW = seated). Use instead of raw readSdDetect().
bool flightCapSdCardDetected(tumbly::HublinkNode &node);

/// Card detect + mount with retries. Does not log failures (safe to poll from menu loop).
FlightCapSdResult flightCapSdEnsure(tumbly::HublinkNode &node);

void flightCapSdLogEnsureFailure(FlightCapSdResult result);

void flightCapSdRelease(tumbly::HublinkNode &node);

/// Unmount, deassert CS, power off SD rail, brief settle before next mount.
void flightCapSdReset(tumbly::HublinkNode &node);
