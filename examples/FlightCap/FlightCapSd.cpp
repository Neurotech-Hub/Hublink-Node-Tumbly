#include "FlightCapSd.h"

bool flightCapSdReady(tumbly::HublinkNode &node) {
  if (!node.readSdDetect()) {
    if (node.sd().isMounted()) {
      node.sd().end();
    }
    return false;
  }
  return node.sd().isMounted() || node.sd().begin();
}
