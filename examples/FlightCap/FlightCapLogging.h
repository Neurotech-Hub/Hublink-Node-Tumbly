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

bool flightCapLoggingPrepare(tumbly::HublinkNode &node, tumbly::DataLoggerHelper &logger,
                             FlightCapLoggingContext &ctx);
AppState flightCapLoggingEnterLoop(tumbly::HublinkNode &node, tumbly::DataLoggerHelper &logger,
                                   FlightCapLoggingContext &ctx);
