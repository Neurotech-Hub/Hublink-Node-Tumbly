#pragma once

#include <HublinkNodeTumbly.h>
#include <esp_sleep.h>
#include <stdint.h>

// Temporary crash/stability diagnostics — remove once logging is stable.
static constexpr const char *kFlightCapDiagPath = "/FC_DIAG.log";

struct FlightCapMemoryStats {
  uint32_t freeHeap = 0;
  uint32_t minFreeHeap = 0;
  uint32_t maxAllocHeap = 0;
  uint32_t heapSize = 0;
  uint32_t largestBlock = 0;
  uint32_t freeInternal = 0;
  uint32_t minFreeInternal = 0;
  uint32_t mainStackHw = 0;
};

void flightCapCollectMemoryStats(FlightCapMemoryStats &out);
void flightCapLogMemoryStats(const char *tag);

void flightCapDiagLogBoot(tumbly::HublinkNode &node);
void flightCapDiagLogStartLogging(tumbly::HublinkNode &node, uint8_t pairCount,
                                  uint32_t logIntervalSec, uint32_t pairIntervalSec,
                                  uint32_t pairTicksPerLog);
void flightCapDiagLogWake(tumbly::HublinkNode &node, esp_sleep_wakeup_cause_t cause,
                          uint32_t pairTickCounter, uint32_t pairTicksPerLog);
void flightCapDiagLogEvent(tumbly::HublinkNode &node, const char *event,
                           esp_sleep_wakeup_cause_t wakeCause, uint32_t pairTickCounter,
                           const char *note = nullptr);
