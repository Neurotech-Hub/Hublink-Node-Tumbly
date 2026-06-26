#include "FlightCapConfig.h"
#include <ArduinoJson.h>

bool flightCapLoadConfig(tumbly::HublinkNode &node, FlightCapConfig &out) {
  out.logIntervalSec = kDefaultLogIntervalSec;
  out.pairIntervalSec = kDefaultPairIntervalSec;

  if (!node.sd().isMounted()) {
    return false;
  }

  StaticJsonDocument<512> doc;
  if (!tumbly::loadMetaJson(node.sd(), doc)) {
    return false;
  }

  uint32_t value = 0;
  if (tumbly::metaGetUInt32(doc, "flightcap.log_interval_seconds", value) && value > 0) {
    out.logIntervalSec = value;
  }
  if (tumbly::metaGetUInt32(doc, "flightcap.pair_interval_seconds", value) && value > 0) {
    out.pairIntervalSec = value;
  }
  return true;
}
