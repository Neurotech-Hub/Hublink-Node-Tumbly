#pragma once

#include "FlightCapApp.h"
#include "TelemetryAdv.h"
#include <HublinkNodeTumbly.h>
#include <stdint.h>

constexpr char kPairsJsonPath[] = "/pairs.json";

struct FlightCapPairList {
  char ids[kMaxPairedDevices][13];
  uint8_t count = 0;
};

/// Format stable device_addr[6] as 12 uppercase hex chars (MSB-first, same as C9:21:...).
void deviceAddrToId(const uint8_t deviceAddr[6], char out[13]);
bool idToDeviceAddr(const char *id, uint8_t out[6]);

bool flightCapPairsLoad(tumbly::HublinkNode &node, FlightCapPairList &out);
bool flightCapPairsSave(tumbly::HublinkNode &node, const FlightCapPairList &list);
bool flightCapPairsAdd(tumbly::HublinkNode &node, FlightCapPairList &list, const char *id);
bool flightCapPairsRemoveAt(tumbly::HublinkNode &node, FlightCapPairList &list, uint8_t index);
void flightCapPairsRemoveAll(FlightCapPairList &list);
bool flightCapPairsContains(const FlightCapPairList &list, const char *id);
bool flightCapPairsContainsDeviceAddr(const FlightCapPairList &list, const uint8_t deviceAddr[6]);
bool flightCapPairsTryAddDeviceAddr(tumbly::HublinkNode &node, FlightCapPairList &list,
                                    const uint8_t deviceAddr[6], char addedId[13]);
