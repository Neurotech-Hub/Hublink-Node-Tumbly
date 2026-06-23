#pragma once

#include "FlightCapApp.h"
#include <HublinkNodeTumbly.h>

struct FlightCapConfig {
  uint32_t logIntervalSec = kDefaultLogIntervalSec;
  uint32_t pairIntervalSec = kDefaultPairIntervalSec;
};

bool flightCapLoadConfig(tumbly::HublinkNode &node, FlightCapConfig &out);
