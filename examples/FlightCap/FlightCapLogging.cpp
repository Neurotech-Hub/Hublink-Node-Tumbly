#include "FlightCapLogging.h"
#include "FlightCapBle.h"
#include "FlightCapConfig.h"
#include "FlightCapDiag.h"
#include "FlightCapLog.h"
#include "FlightCapPairs.h"
#include "FlightCapSd.h"
#include <SD.h>
#include <cstring>
#include <esp_sleep.h>

RTC_DATA_ATTR static bool sLoggingActive = false;
RTC_DATA_ATTR static uint32_t sPairTickCounter = 0;

static tumbly::CsvFieldMask kFlightCapCsvMask = tumbly::csvFields({
    tumbly::CsvField::DateTime,
    tumbly::CsvField::BattV,
    tumbly::CsvField::BattPer,
    tumbly::CsvField::Lux,
    tumbly::CsvField::TempC,
    tumbly::CsvField::DistanceMm,
    tumbly::CsvField::Interactions,
    tumbly::CsvField::CapBattV,
});

static void buildDeviceCsvPath(const char *id, char *out, size_t outLen) {
  snprintf(out, outLen, "/FC_%s.csv", id);
}

static bool ensureDeviceCsvHeader(tumbly::HublinkNode &node, tumbly::DataLoggerHelper &logger,
                                  const char *id, tumbly::CsvFieldMask mask) {
  char path[32];
  buildDeviceCsvPath(id, path, sizeof(path));
  if (node.sd().exists(path)) {
    return true;
  }
  const String header = tumbly::DataLoggerHelper::csvHeader(mask);
  return node.sd().appendLine(path, header) == tumbly::ServiceStatus::Ok;
}

static void loggingPowerOnI2c(tumbly::HublinkNode &node) {
  node.setI2CPowerEnabled(true);
  delay(100);
}

static void loggingInitSensorsForLogPass(tumbly::HublinkNode &node, tumbly::DataLoggerHelper &logger) {
  loggingPowerOnI2c(node);
  node.beginI2C();
  delay(100);
  (void)node.rtc().begin();
  (void)node.powerGauge().begin();
  (void)logger.begin();
}

static void loggingTeardownDisplayAndSd(tumbly::HublinkNode &node) {
  node.setStatusLeds(false);
  node.servo().detach();
  node.screen().end();
  node.sd().end();
  node.set5VPowerEnabled(false);
  node.setI2CPowerEnabled(false);
}

static void loggingWakeLedPulse() {
  digitalWrite(tumbly::PIN_LED_FRONT, HIGH);
  delay(50);
  digitalWrite(tumbly::PIN_LED_FRONT, LOW);
}

static bool isAnyUserButtonHeld() {
  return digitalRead(tumbly::PIN_BNT_0) == LOW || digitalRead(tumbly::PIN_BNT_1) == LOW ||
         digitalRead(tumbly::PIN_BNT_2) == LOW;
}

static bool isBootHeld() {
  return digitalRead(tumbly::PIN_BOOT_BUTTON) == LOW;
}

static bool anyWakeInputHeld() {
  return isBootHeld() || isAnyUserButtonHeld();
}

static constexpr uint32_t kWakeInputReleaseDebounceMs = 100;

static void waitWakeInputsReleased() {
  while (anyWakeInputHeld()) {
    delay(10);
  }
  delay(kWakeInputReleaseDebounceMs);
}

static void configureLoggingSleepWake(uint32_t pairIntervalSec) {
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(pairIntervalSec) * 1000000ULL);
}

static void loggingInitBleForWake(FlightCapLoggingContext &ctx) {
  flightCapBleInit();
  flightCapBleSetPairList(&ctx.pairs);
  flightCapBleBeginLogInterval();
}

static void loggingReloadContext(tumbly::HublinkNode &node, FlightCapLoggingContext &ctx) {
  ctx.config.logIntervalSec = kDefaultLogIntervalSec;
  ctx.config.pairIntervalSec = kDefaultPairIntervalSec;
  ctx.pairs.count = 0;
  memset(ctx.pairs.ids, 0, sizeof(ctx.pairs.ids));

  if (flightCapSdEnsure(node) == FlightCapSdResult::Ready) {
    (void)flightCapLoadConfig(node, ctx.config);
    (void)flightCapPairsLoad(node, ctx.pairs);
    flightCapSdRelease(node);
  }

  ctx.csvMask = kFlightCapCsvMask;
  ctx.pairTicksPerLog = ctx.config.logIntervalSec / ctx.config.pairIntervalSec;
  if (ctx.pairTicksPerLog == 0) {
    ctx.pairTicksPerLog = 1;
  }
  ctx.pairTickCounter = sPairTickCounter;
}

static void loggingExitToMenu(tumbly::HublinkNode &node) {
  flightCapBleStopForSleep();
  flightCapSdReset(node);
}

static void enterLoggingDeepSleep(tumbly::HublinkNode &node, FlightCapLoggingContext &ctx) {
  flightCapBleStopForSleep();
  loggingTeardownDisplayAndSd(node);
  flightCapSdReset(node);
  configureLoggingSleepWake(ctx.config.pairIntervalSec);
  flightCapLog(F("FlightCap: deep sleep"));
  Serial.flush();
  esp_deep_sleep_start();
}

static constexpr char kHubLogId[] = "LOG";

static bool appendHubLogRow(tumbly::HublinkNode &node, tumbly::DataLoggerHelper &logger,
                            tumbly::CsvFieldMask mask, const tumbly::CompositeSample &sample) {
  (void)ensureDeviceCsvHeader(node, logger, kHubLogId, mask);
  char path[32];
  buildDeviceCsvPath(kHubLogId, path, sizeof(path));
  tumbly::CompositeSample row = sample;
  row.hasFlightCapReading = false;
  return logger.appendCsvSample(path, row, mask) == tumbly::ServiceStatus::Ok;
}

static bool runLogPass(tumbly::HublinkNode &node, tumbly::DataLoggerHelper &logger,
                       FlightCapLoggingContext &ctx) {
  flightCapLog(F("FlightCap: log pass"));
  const FlightCapSdResult sd = flightCapSdEnsure(node);
  if (sd != FlightCapSdResult::Ready) {
    flightCapSdLogEnsureFailure(sd);
    flightCapLog(F("FlightCap: log pass skipped (SD unavailable)"));
    return false;
  }

  tumbly::CompositeSample base = logger.captureSample();

  PairedDeviceState *devices = flightCapBleDeviceStates();
  const uint8_t deviceCount = flightCapBleDeviceCount();
  Serial.print(F("FlightCap: writing "));
  Serial.print(deviceCount == 0 ? 1 : deviceCount);
  Serial.println(F(" log file(s)"));

  if (deviceCount == 0) {
    (void)appendHubLogRow(node, logger, ctx.csvMask, base);
  } else {
    for (uint8_t i = 0; i < deviceCount; ++i) {
      char path[32];
      buildDeviceCsvPath(devices[i].id, path, sizeof(path));
      (void)ensureDeviceCsvHeader(node, logger, devices[i].id, ctx.csvMask);

      tumbly::CompositeSample row = base;
      if (devices[i].seenThisInterval) {
        row.hasFlightCapReading = true;
        row.distanceMm = devices[i].distance_mm;
        row.interactions = devices[i].interactions;
        if (devices[i].flags & FLAG_VBATT_VALID) {
          row.hasCapBatt = true;
          row.capBattV = devices[i].vbatt_mv / 1000.0f;
        }
      } else {
        row.hasFlightCapReading = false;
      }
      (void)logger.appendCsvSample(path, row, ctx.csvMask);
    }
  }

  flightCapBleBeginLogInterval();
  flightCapSdRelease(node);
  return true;
}

static void runPairScanPass(FlightCapLoggingContext &ctx) {
  (void)ctx;
  if (flightCapBleDeviceCount() == 0) {
    return;
  }
  flightCapLog(F("FlightCap: pair scan window"));
  (void)flightCapBleRunScanWindow(kPairScanWindowMs);
  flightCapBleStopForSleep();
}

bool flightCapLoggingIsActive() {
  return sLoggingActive;
}

void flightCapLoggingWaitWakeInputsReleased() {
  waitWakeInputsReleased();
}

void flightCapLoggingClearActive() {
  sLoggingActive = false;
  sPairTickCounter = 0;
}

bool flightCapLoggingPrepare(tumbly::HublinkNode &node, tumbly::DataLoggerHelper &logger,
                             FlightCapLoggingContext &ctx) {
  (void)logger;
  (void)flightCapLoadConfig(node, ctx.config);
  (void)flightCapPairsLoad(node, ctx.pairs);
  ctx.csvMask = kFlightCapCsvMask;

  ctx.pairTicksPerLog = ctx.config.logIntervalSec / ctx.config.pairIntervalSec;
  if (ctx.pairTicksPerLog == 0) {
    ctx.pairTicksPerLog = 1;
  }
  ctx.pairTickCounter = 0;

  Serial.print(F("FlightCap: logging prepare, pairs="));
  Serial.println(ctx.pairs.count);
  Serial.print(F("FlightCap: log interval s="));
  Serial.print(ctx.config.logIntervalSec);
  Serial.print(F(" pair interval s="));
  Serial.print(ctx.config.pairIntervalSec);
  Serial.print(F(" (log every "));
  Serial.print(ctx.pairTicksPerLog);
  Serial.println(F(" timer wakes)"));

  flightCapBleSetPairList(&ctx.pairs);
  flightCapBleBeginLogInterval();

  if (flightCapSdEnsure(node) == FlightCapSdResult::Ready) {
    if (ctx.pairs.count == 0) {
      (void)ensureDeviceCsvHeader(node, logger, kHubLogId, ctx.csvMask);
      flightCapLog(F("FlightCap: hub log file /FC_LOG.csv"));
    } else {
      for (uint8_t i = 0; i < ctx.pairs.count; ++i) {
        (void)ensureDeviceCsvHeader(node, logger, ctx.pairs.ids[i], ctx.csvMask);
      }
    }
    flightCapSdRelease(node);
  }
  flightCapSdReset(node);
  return true;
}

void flightCapLoggingStartDeepSleep(tumbly::HublinkNode &node, FlightCapLoggingContext &ctx) {
  sLoggingActive = true;
  sPairTickCounter = 0;
  ctx.pairTickCounter = 0;
  flightCapLog(F("FlightCap: enter logging deep sleep"));
  Serial.flush();
  enterLoggingDeepSleep(node, ctx);
}

bool flightCapLoggingHandleWakeSetup(tumbly::HublinkNode &node, tumbly::DataLoggerHelper &logger,
                                     FlightCapLoggingContext &ctx) {
  loggingWakeLedPulse();
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  flightCapLogWake(cause);
  flightCapLogMemoryStats("wake");
  node.buttons().end();

  loggingReloadContext(node, ctx);

  if (cause != ESP_SLEEP_WAKEUP_TIMER) {
    flightCapLog(F("FlightCap: non-timer wake, back to sleep"));
    enterLoggingDeepSleep(node, ctx);
    return true;
  }

  if (!flightCapSdCardDetected(node)) {
    flightCapSdLogEnsureFailure(FlightCapSdResult::DetectOpen);
    flightCapLog(F("FlightCap: exit logging (SD unavailable)"));
    flightCapLoggingClearActive();
    loggingExitToMenu(node);
    return false;
  }

  loggingInitBleForWake(ctx);
  ++sPairTickCounter;
  ctx.pairTickCounter = sPairTickCounter;
  const bool doLogPass = (sPairTickCounter >= ctx.pairTicksPerLog);
  if (doLogPass) {
    sPairTickCounter = 0;
    ctx.pairTickCounter = 0;
  }

  runPairScanPass(ctx);

  if (doLogPass) {
    flightCapBleStopForSleep();
    delay(100);
    flightCapSdReset(node);
    loggingInitSensorsForLogPass(node, logger);
    (void)runLogPass(node, logger, ctx);
    node.setI2CPowerEnabled(false);
  }

  enterLoggingDeepSleep(node, ctx);
  return true;
}
