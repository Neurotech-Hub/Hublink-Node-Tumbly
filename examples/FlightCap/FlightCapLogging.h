#pragma once

#include "FlightCapApp.h"
#include "FlightCapConfig.h"
#include "FlightCapPairs.h"
#include <HublinkNodeTumbly.h>

struct FlightCapLoggingContext {
  FlightCapConfig config;
  FlightCapPairList pairs;
  tumbly::CsvFieldMask csvMask = 0;
  /// Timer wakes at pair_interval; log every pairTicksPerLog wakes (ESP32 sleep timer).
  uint32_t pairTicksPerLog = 6;
  uint32_t pairTickCounter = 0;
};

bool flightCapLoggingIsActive();
void flightCapLoggingClearActive();

/// Block until menu buttons are released before entering logging sleep.
void flightCapLoggingWaitWakeInputsReleased();

bool flightCapLoggingPrepare(tumbly::HublinkNode &node, tumbly::DataLoggerHelper &logger,
                             FlightCapLoggingContext &ctx);

/// Menu entry after prepare: sets RTC flag, teardown, deep sleep (does not return).
void flightCapLoggingStartDeepSleep(tumbly::HublinkNode &node, FlightCapLoggingContext &ctx);

/// setup() early branch on logging wake. Returns false if logging exited (run menu init).
/// On resume, enters deep sleep and does not return.
bool flightCapLoggingHandleWakeSetup(tumbly::HublinkNode &node, tumbly::DataLoggerHelper &logger,
                                     FlightCapLoggingContext &ctx);
