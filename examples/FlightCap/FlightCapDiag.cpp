#include "FlightCapDiag.h"
#include "FlightCapSd.h"
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>

namespace {

const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
  case ESP_RST_POWERON:
    return "poweron";
  case ESP_RST_EXT:
    return "ext";
  case ESP_RST_SW:
    return "sw";
  case ESP_RST_PANIC:
    return "panic";
  case ESP_RST_INT_WDT:
    return "int_wdt";
  case ESP_RST_TASK_WDT:
    return "task_wdt";
  case ESP_RST_WDT:
    return "wdt";
  case ESP_RST_DEEPSLEEP:
    return "deepsleep";
  case ESP_RST_BROWNOUT:
    return "brownout";
  case ESP_RST_SDIO:
    return "sdio";
  default:
    return "unknown";
  }
}

const char *wakeCauseName(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
  case ESP_SLEEP_WAKEUP_UNDEFINED:
    return "undefined";
  case ESP_SLEEP_WAKEUP_EXT0:
    return "ext0";
  case ESP_SLEEP_WAKEUP_EXT1:
    return "ext1";
  case ESP_SLEEP_WAKEUP_TIMER:
    return "timer";
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    return "touchpad";
  case ESP_SLEEP_WAKEUP_ULP:
    return "ulp";
  case ESP_SLEEP_WAKEUP_GPIO:
    return "gpio";
  case ESP_SLEEP_WAKEUP_UART:
    return "uart";
  default:
    return "other";
  }
}

void formatDateTime(char out[20]) {
  out[0] = '\0';
  const time_t now = time(nullptr);
  if (now <= 0) {
    return;
  }
  struct tm tmNow {};
  if (localtime_r(&now, &tmNow) == nullptr) {
    return;
  }
  snprintf(out, 20, "%04d-%02d-%02d %02d:%02d:%02d", tmNow.tm_year + 1900, tmNow.tm_mon + 1,
           tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
}

void appendMemoryFields(char *line, size_t lineLen, const FlightCapMemoryStats &mem) {
  char suffix[128];
  snprintf(suffix, sizeof(suffix),
           ",millis=%lu,free=%u,min_free=%u,max_alloc=%u,heap=%u,largest=%u,int_free=%u,"
           "int_min=%u,stack_hw=%u",
           static_cast<unsigned long>(millis()), mem.freeHeap, mem.minFreeHeap, mem.maxAllocHeap,
           mem.heapSize, mem.largestBlock, mem.freeInternal, mem.minFreeInternal, mem.mainStackHw);
  strncat(line, suffix, lineLen - strlen(line) - 1);
}

bool ensureDiagHeader(tumbly::HublinkNode &node) {
  if (node.sd().exists(kFlightCapDiagPath)) {
    return true;
  }
  const char *header =
      "datetime,event,wake,reset,millis,free,min_free,max_alloc,heap,largest,int_free,int_min,"
      "stack_hw,pair_tick,pair_per_log,note";
  return node.sd().appendLine(kFlightCapDiagPath, header) == tumbly::ServiceStatus::Ok;
}

bool appendDiagLine(tumbly::HublinkNode &node, const char *line) {
  if (!node.readSdDetect()) {
    return false;
  }
  const bool wasMounted = node.sd().isMounted();
  if (!wasMounted && !node.sd().begin()) {
    return false;
  }
  if (!ensureDiagHeader(node)) {
    if (!wasMounted) {
      node.sd().end();
    }
    return false;
  }
  const tumbly::ServiceStatus status = node.sd().appendLine(kFlightCapDiagPath, line);
  if (!wasMounted) {
    node.sd().end();
  }
  return status == tumbly::ServiceStatus::Ok;
}

void appendDiagEvent(tumbly::HublinkNode &node, const char *event, const char *wakeName,
                     const char *resetName, uint32_t pairTickCounter, uint32_t pairTicksPerLog,
                     const char *note) {
  char datetime[20];
  formatDateTime(datetime);

  FlightCapMemoryStats mem{};
  flightCapCollectMemoryStats(mem);

  char line[320];
  snprintf(line, sizeof(line), "%s,%s,%s,%s", datetime[0] ? datetime : "", event,
           wakeName != nullptr ? wakeName : "", resetName != nullptr ? resetName : "");
  appendMemoryFields(line, sizeof(line), mem);

  char tail[96];
  snprintf(tail, sizeof(tail), ",pair_tick=%lu,pair_per_log=%lu,note=%s",
           static_cast<unsigned long>(pairTickCounter),
           static_cast<unsigned long>(pairTicksPerLog), note != nullptr ? note : "");
  strncat(line, tail, sizeof(line) - strlen(line) - 1);

  (void)appendDiagLine(node, line);
}

} // namespace

void flightCapCollectMemoryStats(FlightCapMemoryStats &out) {
  out.freeHeap = ESP.getFreeHeap();
  out.minFreeHeap = ESP.getMinFreeHeap();
  out.maxAllocHeap = ESP.getMaxAllocHeap();
  out.heapSize = ESP.getHeapSize();
  out.largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  out.freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  out.minFreeInternal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  out.mainStackHw = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
}

void flightCapLogMemoryStats(const char *tag) {
  FlightCapMemoryStats mem{};
  flightCapCollectMemoryStats(mem);
  Serial.printf(
      "FlightCap: mem %s millis=%lu free=%u min=%u max_alloc=%u heap=%u largest=%u int_free=%u "
      "int_min=%u stack_hw=%u\n",
      tag, static_cast<unsigned long>(millis()), mem.freeHeap, mem.minFreeHeap, mem.maxAllocHeap,
      mem.heapSize, mem.largestBlock, mem.freeInternal, mem.minFreeInternal, mem.mainStackHw);
  Serial.flush();
}

void flightCapDiagLogBoot(tumbly::HublinkNode &node) {
  appendDiagEvent(node, "boot", nullptr, resetReasonName(esp_reset_reason()), 0, 0, nullptr);
}

void flightCapDiagLogStartLogging(tumbly::HublinkNode &node, uint8_t pairCount,
                                  uint32_t logIntervalSec, uint32_t pairIntervalSec,
                                  uint32_t pairTicksPerLog) {
  char note[48];
  snprintf(note, sizeof(note), "pairs=%u log_s=%lu pair_s=%lu",
           static_cast<unsigned>(pairCount), static_cast<unsigned long>(logIntervalSec),
           static_cast<unsigned long>(pairIntervalSec));
  appendDiagEvent(node, "start_logging", nullptr, nullptr, 0, pairTicksPerLog, note);
}

void flightCapDiagLogWake(tumbly::HublinkNode &node, esp_sleep_wakeup_cause_t cause,
                          uint32_t pairTickCounter, uint32_t pairTicksPerLog) {
  appendDiagEvent(node, "wake", wakeCauseName(cause), nullptr, pairTickCounter, pairTicksPerLog,
                  nullptr);
}

void flightCapDiagLogEvent(tumbly::HublinkNode &node, const char *event,
                           esp_sleep_wakeup_cause_t wakeCause, uint32_t pairTickCounter,
                           const char *note) {
  appendDiagEvent(node, event, wakeCauseName(wakeCause), nullptr, pairTickCounter, 0, note);
}
