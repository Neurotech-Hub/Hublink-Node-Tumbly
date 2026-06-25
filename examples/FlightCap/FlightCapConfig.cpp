#include "FlightCapConfig.h"
#include "FlightCapSd.h"
#include <ArduinoJson.h>

bool flightCapLoadConfig(tumbly::HublinkNode &node, FlightCapConfig &out) {
  out.logIntervalSec = kDefaultLogIntervalSec;
  out.pairIntervalSec = kDefaultPairIntervalSec;

  if (!flightCapSdCardDetected(node)) {
    return false;
  }
  if (!node.sd().isMounted() && !node.sd().begin()) {
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
