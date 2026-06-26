#pragma once

#include <HublinkNodeTumbly.h>

enum class FlightCapSdResult : uint8_t {
  Ready,
  DetectOpen,
  MountFailed,
};

/// Debounced card-detect (LOW = seated). Use instead of raw readSdDetect().
bool flightCapSdCardDetected(tumbly::HublinkNode &node);

/// Detect + power rail + retry mount + cardType check. Returns true when mounted.
bool flightCapSdMount(tumbly::HublinkNode &node);

/// Full teardown: unmount filesystem, SPI.end, CS/EN high, settle delay.
void flightCapSdUnmount(tumbly::HublinkNode &node);

/// Card detect + mount with retries. Does not log failures (safe to poll from menu loop).
FlightCapSdResult flightCapSdEnsure(tumbly::HublinkNode &node);

void flightCapSdLogEnsureFailure(FlightCapSdResult result);

/// Alias for flightCapSdUnmount().
void flightCapSdRelease(tumbly::HublinkNode &node);

/// Alias for flightCapSdUnmount().
void flightCapSdReset(tumbly::HublinkNode &node);
