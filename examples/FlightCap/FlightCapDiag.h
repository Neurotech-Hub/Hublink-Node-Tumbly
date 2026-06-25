#pragma once

#include <HublinkNodeTumbly.h>
#include <stdint.h>

// Temporary crash/stability diagnostics — remove once logging is stable.
// SD: boot events only (/FC_DIAG.log). Serial heap stats: flightCapLogMemoryStats().
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
