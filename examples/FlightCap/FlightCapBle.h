#pragma once

#include "FlightCapApp.h"
#include "FlightCapPairs.h"
#include "TelemetryAdv.h"
#include <NimBLEDevice.h>
#include <freertos/portmacro.h>
#include <stdint.h>

enum class FlightCapBleMode : uint8_t {
  Off,
  IdleMenu,
  PairActive,
  LoggingWindow,
  ActiveScanner,
};

struct ActiveScannerCap {
  bool locked = false;
  uint8_t device_addr[6] = {};
  int16_t distance_mm = 0;
  uint16_t interactions = 0;
  uint16_t seq = 0;
  uint8_t flags = 0;
  uint16_t vbatt_mv = 0;
  int8_t rssi = 0;
  uint32_t last_data_ms = 0;
  uint32_t last_seen_ms = 0;
};

struct PairedDeviceState {
  uint8_t device_addr[6];
  char id[13];
  uint16_t last_seq;
  int16_t distance_mm;
  uint16_t interactions;
  uint8_t flags;
  uint16_t vbatt_mv;
  int8_t rssi;
  uint32_t last_seen_ms;
  bool valid;
  bool seenThisInterval;
};

void flightCapBleInit();
void flightCapBleEnsureInit();
void flightCapBleStopForSleep();
void flightCapBleSetMode(FlightCapBleMode mode);
FlightCapBleMode flightCapBleMode();
void flightCapBleSetPairList(const FlightCapPairList *list);

void flightCapBleClearPendingPairAdds();
bool flightCapBleTakePendingPairAdd(uint8_t deviceAddr[6], TelemetryAdv *advOut);
void flightCapBleNotePairSessionCommit(const TelemetryAdv &adv);

void flightCapBleStartContinuousScan();
void flightCapBleStopScan();
bool flightCapBleRunScanWindow(uint32_t durationMs);

void flightCapBleBeginLogInterval();
void flightCapBleEndLogInterval();
PairedDeviceState *flightCapBleDeviceStates();
uint8_t flightCapBleDeviceCount();

void flightCapBleApplyStaleTimeout();
bool flightCapBleAllPairsSeenThisInterval();

void flightCapBleBeginActiveScanner();
void flightCapBleEndActiveScanner();
void flightCapBleGetActiveScannerCap(ActiveScannerCap *out);
uint32_t flightCapBleActiveScannerSecondsSinceData();
