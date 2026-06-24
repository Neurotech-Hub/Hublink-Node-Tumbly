#include "FlightCapLogging.h"
#include "FlightCapBle.h"
#include "FlightCapLog.h"
#include "FlightCapSd.h"
#include "FlightCapUi.h"
#include <SD.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

static tumbly::CsvFieldMask kFlightCapCsvMask = tumbly::csvFields({
    tumbly::CsvField::DateTime,
    tumbly::CsvField::BattV,
    tumbly::CsvField::BattPer,
    tumbly::CsvField::Lux,
    tumbly::CsvField::TempC,
    tumbly::CsvField::DistanceMm,
    tumbly::CsvField::Interactions,
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

static void loggingRestoreScreen(tumbly::HublinkNode &node) {
  delay(50);
  (void)node.screen().begin();
}

static bool isBootHeld() {
  return digitalRead(tumbly::PIN_BOOT_BUTTON) == LOW;
}

static bool isAnyUserButtonHeld() {
  return digitalRead(tumbly::PIN_BNT_0) == LOW || digitalRead(tumbly::PIN_BNT_1) == LOW ||
         digitalRead(tumbly::PIN_BNT_2) == LOW;
}

static bool anyWakeInputHeld() {
  return isBootHeld() || isAnyUserButtonHeld();
}

static void waitWakeInputsReleased() {
  while (anyWakeInputHeld()) {
    delay(10);
  }
  delay(50);
}

static void disableLoggingGpioWake() {
  gpio_wakeup_disable((gpio_num_t)tumbly::PIN_BNT_0);
  gpio_wakeup_disable((gpio_num_t)tumbly::PIN_BNT_1);
  gpio_wakeup_disable((gpio_num_t)tumbly::PIN_BNT_2);
  gpio_wakeup_disable((gpio_num_t)tumbly::PIN_BOOT_BUTTON);
}

static void configureLoggingSleepWake(uint32_t pairIntervalSec) {
  waitWakeInputsReleased();

  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(pairIntervalSec) * 1000000ULL);

  gpio_wakeup_enable((gpio_num_t)tumbly::PIN_BNT_0, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)tumbly::PIN_BNT_1, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)tumbly::PIN_BNT_2, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)tumbly::PIN_BOOT_BUTTON, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
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
  if (!node.sd().begin()) {
    flightCapLog(F("FlightCap: log pass SD mount failed"));
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
      } else {
        row.hasFlightCapReading = false;
      }
      (void)logger.appendCsvSample(path, row, ctx.csvMask);
    }
  }

  flightCapBleBeginLogInterval();
  node.sd().end();
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

static bool runLoggingPeek(tumbly::HublinkNode &node, FlightCapLoggingContext &ctx) {
  flightCapLog(F("FlightCap: logging peek"));
  loggingPowerOnI2c(node);
  loggingRestoreScreen(node);
  if (!flightCapSdReady(node)) {
    flightCapLog(F("FlightCap: exit logging (SD missing)"));
    loggingTeardownDisplayAndSd(node);
    return false;
  }
  while (anyWakeInputHeld()) {
    if (!flightCapSdReady(node)) {
      flightCapLog(F("FlightCap: exit logging (SD missing)"));
      loggingTeardownDisplayAndSd(node);
      return false;
    }
    if (isBootHeld()) {
      flightCapLog(F("FlightCap: peek exit (BOOT)"));
      loggingTeardownDisplayAndSd(node);
      return false;
    }
    flightCapUiRenderLoggingPeek(node, ctx.pairs);
    delay(50);
  }
  loggingTeardownDisplayAndSd(node);
  flightCapLog(F("FlightCap: peek done, resume sleep"));
  return true;
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

  if (node.sd().begin()) {
    if (ctx.pairs.count == 0) {
      (void)ensureDeviceCsvHeader(node, logger, kHubLogId, ctx.csvMask);
      flightCapLog(F("FlightCap: hub log file /FC_LOG.csv"));
    } else {
      for (uint8_t i = 0; i < ctx.pairs.count; ++i) {
        (void)ensureDeviceCsvHeader(node, logger, ctx.pairs.ids[i], ctx.csvMask);
      }
    }
    node.sd().end();
  }
  return true;
}

AppState flightCapLoggingEnterLoop(tumbly::HublinkNode &node, tumbly::DataLoggerHelper &logger,
                                   FlightCapLoggingContext &ctx) {
  flightCapLog(F("FlightCap: enter logging sleep loop"));
  Serial.flush();

  flightCapBleStopForSleep();
  loggingTeardownDisplayAndSd(node);
  flightCapLog(F("FlightCap: I2C off, display/SD off"));
  Serial.flush();

  node.buttons().end();
  flightCapLog(F("FlightCap: button ISRs off"));
  Serial.flush();

  while (true) {
    configureLoggingSleepWake(ctx.config.pairIntervalSec);
    flightCapLog(F("FlightCap: light sleep"));
    Serial.flush();
    esp_light_sleep_start();

    disableLoggingGpioWake();
    loggingWakeLedPulse();
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    flightCapLogWake(cause);

    if (isBootHeld()) {
      flightCapLog(F("FlightCap: exit logging (BOOT)"));
      break;
    }

    if (isAnyUserButtonHeld()) {
      if (!runLoggingPeek(node, ctx)) {
        break;
      }
      continue;
    }

    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
      if (!flightCapSdReady(node)) {
        flightCapLog(F("FlightCap: exit logging (SD missing)"));
        break;
      }
      ++ctx.pairTickCounter;
      if (ctx.pairTickCounter >= ctx.pairTicksPerLog) {
        ctx.pairTickCounter = 0;
        loggingPowerOnI2c(node);
        (void)runLogPass(node, logger, ctx);
        node.setI2CPowerEnabled(false);
      }
      runPairScanPass(ctx);
    }
  }

  disableLoggingGpioWake();
  node.sd().end();
  loggingPowerOnI2c(node);
  node.buttons().begin();
  loggingRestoreScreen(node);
  flightCapBleStartContinuousScan();
  flightCapLog(F("FlightCap: logging loop ended"));
  return AppState::MainMenu;
}
