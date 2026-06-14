#include <Hublink.h>
#include <HublinkNodeTumbly.h>
#include <esp_sleep.h>

// HubWheelHublink:
// - Wheel logging + deep-sleep loop
// - Optional runtime tuning via meta.json through Hublink
// - Assumes Hublink library is installed
//
// Example meta.json used by this sketch:
//
// {
//   "hublink": {
//     "advertise": "HUBLINK",
//     "advertise_every": 120,
//     "advertise_for": 30,
//     "try_reconnect": false,
//     "reconnect_attempts": 2,
//     "reconnect_every": 30,
//     "upload_path": "/RAVEN",
//     "append_path": "device:id",
//     "disable": false   // true: skip runHublinkSyncWindow (Hublink.begin() still runs)
//   },
//   "wheel": {
//     "sleep_time_seconds": 10,
//     "sync_every_seconds": 21600,
//     "sync_for_seconds": 30
//   },
//   "logger": {
//     "log_base_name": "HUBWHEEL",
//     "log_file_mode": "daily",
//     "inc_on_reboot": false,
//     "log_fields": [
//       "datetime",
//       "magnet_passes",
//       "passes_min",
//       "batt_v",
//       "batt_per",
//      "temp_c",
//      "lux"
//     ]
//   },
//   "device": {
//     "id": "001"
//   }
// }
//
// Keys consumed directly by this sketch:
// - hublink.disable (skip gateway sync window when true; still read by Hublink library in begin())
// - wheel.sleep_time_seconds, wheel.sync_every_seconds, wheel.sync_for_seconds
// - logger.log_base_name, logger.log_file_mode, logger.inc_on_reboot, logger.log_fields
// Power-on / reset wake uses kHublinkSyncSecondsOnReset for sync duration; timer wakes use
// wheel.sync_for_seconds (meta) / gSyncForSeconds default. Before that long reset sync, a short
// LED hold polls PIN_BOOT_BUTTON — holding boot skips the long sync for this wake only.
// These wheel/logger/hublink.disable keys are read from /meta.json by this library via tumbly::loadMetaJson
// (typed getters in MetaConfigReader), so sketches do not rely on Hublink for namespaces the
// device firmware owns here.
//
// hublink.* and device.* are still handled inside the Hublink library via hublink.begin().

tumbly::HublinkNode node;
tumbly::DataLoggerHelper logger(node);
tumbly::MetaConfigEditor metaEditor;
Hublink hublink(tumbly::PIN_SD_CS);

// Hardcoded defaults (meta.json can override these in beginHublink()).
uint32_t gSleepTimeSeconds = 10;
uint32_t gSyncEverySeconds = 21600;
uint32_t gSyncForSeconds = 30;
/// Hublink `sync()` duration after power-on / reset wake (`ESP_SLEEP_WAKEUP_UNDEFINED`).
constexpr uint32_t kHublinkSyncSecondsOnReset = 120;
/// Fast LED blink window before long reset sync; hold `PIN_BOOT_BUTTON` LOW to skip that sync.
constexpr uint32_t kLongSyncBootHoldMs = 5000;
constexpr uint32_t kLongSyncBootBlinkHalfMs = 60;
String gLogBaseName = "HUBWHEEL";
tumbly::FileNameMode gLogFileMode = tumbly::FileNameMode::Daily;
bool gIncOnReboot = false;
/// When true (from meta `hublink.disable`), skip `runHublinkSyncWindow` but still advance sync cadence.
bool gHublinkDisable = false;
// Default CSV columns until meta.json `logger.log_fields` overrides (deep-sleep wheel + gauge).
static constexpr tumbly::CsvFieldMask kCsvFieldMask = tumbly::csvFields({
    tumbly::CsvField::RtcUnix,
    tumbly::CsvField::UlpEdges,
    tumbly::CsvField::MagnetPasses,
    tumbly::CsvField::PassesPerMin,
    tumbly::CsvField::BattV,
    tumbly::CsvField::BattPer,
});

RTC_DATA_ATTR uint32_t gLogCount = 0;

struct LogContext
{
  tumbly::LogFilePolicy filePolicy;
  tumbly::CsvFieldMask fieldMask = 0;
};

LogContext gLogContext = {
    {nullptr, tumbly::FileNameMode::Daily, 0, false},
    kCsvFieldMask,
};

static const __FlashStringHelper *wakeCauseText(esp_sleep_wakeup_cause_t cause)
{
  switch (cause)
  {
  case ESP_SLEEP_WAKEUP_UNDEFINED:
    return F("power_on_or_reset");
  case ESP_SLEEP_WAKEUP_EXT0:
    return F("ext0");
  case ESP_SLEEP_WAKEUP_EXT1:
    return F("ext1");
  case ESP_SLEEP_WAKEUP_TIMER:
    return F("timer");
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    return F("touchpad");
  case ESP_SLEEP_WAKEUP_ULP:
    return F("ulp");
  case ESP_SLEEP_WAKEUP_GPIO:
    return F("gpio");
  case ESP_SLEEP_WAKEUP_UART:
    return F("uart");
  default:
    return F("unknown");
  }
}

static void blinkPowerOnPattern(esp_sleep_wakeup_cause_t cause)
{
  if (cause != ESP_SLEEP_WAKEUP_UNDEFINED)
  {
    return;
  }
  pinMode(tumbly::PIN_LED_BACK, OUTPUT);
  for (uint8_t i = 0; i < 3; ++i)
  {
    digitalWrite(tumbly::PIN_LED_FRONT, HIGH);
    digitalWrite(tumbly::PIN_LED_BACK, HIGH);
    delay(100);
    digitalWrite(tumbly::PIN_LED_FRONT, LOW);
    digitalWrite(tumbly::PIN_LED_BACK, LOW);
    delay(100);
  }
  digitalWrite(tumbly::PIN_LED_FRONT, HIGH);
}

static void blinkMissingSdCard()
{
  Serial.println(F("HubWheelHublink: SD card not present. Halting."));
  pinMode(tumbly::PIN_LED_BACK, OUTPUT);
  while (true)
  {
    digitalWrite(tumbly::PIN_LED_FRONT, HIGH);
    digitalWrite(tumbly::PIN_LED_BACK, LOW);
    delay(100);
    digitalWrite(tumbly::PIN_LED_FRONT, LOW);
    digitalWrite(tumbly::PIN_LED_BACK, HIGH);
    delay(100);
  }
}

static void applyLogPolicyDefaults()
{
  gLogContext.filePolicy.baseName = gLogBaseName.c_str();
  gLogContext.filePolicy.mode = gLogFileMode;
  gLogContext.filePolicy.incOnReboot = gIncOnReboot;
}

static tumbly::FileNameMode parseLogFileMode(const String &modeText)
{
  String mode = modeText;
  mode.toLowerCase();
  mode.trim();
  if (mode == "daily")
  {
    return tumbly::FileNameMode::Daily;
  }
  if (mode == "hourly")
  {
    return tumbly::FileNameMode::Hourly;
  }
  if (mode == "manual")
  {
    return tumbly::FileNameMode::Manual;
  }
  if (mode == "disabled")
  {
    return tumbly::FileNameMode::Disabled;
  }
  return gLogFileMode;
}

static const __FlashStringHelper *logFileModeText(tumbly::FileNameMode mode)
{
  switch (mode)
  {
  case tumbly::FileNameMode::Daily:
    return F("daily");
  case tumbly::FileNameMode::Hourly:
    return F("hourly");
  case tumbly::FileNameMode::Manual:
    return F("manual");
  case tumbly::FileNameMode::Disabled:
    return F("disabled");
  }
  return F("unknown");
}

static void onTimestampReceived(uint32_t timestamp)
{
  // Lets gateway-provided timestamps update on-device RTC.
  node.rtc().adjust(DateTime(timestamp));
}

// Apply wheel/logger keys from meta.json via Tumbly helpers (SD + typed reads).
static void applyWheelLoggerMetaFromSd()
{
  JsonDocument metaDoc;
  if (!tumbly::loadMetaJson(node.sd(), metaDoc))
  {
    return;
  }

  bool hublinkDisable = false;
  if (tumbly::metaGetBool(metaDoc, String("hublink.disable"), hublinkDisable))
  {
    gHublinkDisable = hublinkDisable;
    Serial.print(F("hublink.disable: "));
    Serial.println(gHublinkDisable ? F("true") : F("false"));
  }

  uint32_t u = 0;
  if (tumbly::metaGetUInt32(metaDoc, String("wheel.sleep_time_seconds"), u))
  {
    gSleepTimeSeconds = u;
    Serial.print(F("wheel.sleep_time_seconds: "));
    Serial.println(gSleepTimeSeconds);
  }
  if (tumbly::metaGetUInt32(metaDoc, String("wheel.sync_every_seconds"), u))
  {
    gSyncEverySeconds = u;
    Serial.print(F("wheel.sync_every_seconds: "));
    Serial.println(gSyncEverySeconds);
  }
  if (tumbly::metaGetUInt32(metaDoc, String("wheel.sync_for_seconds"), u))
  {
    gSyncForSeconds = u;
    Serial.print(F("wheel.sync_for_seconds: "));
    Serial.println(gSyncForSeconds);
  }

  String s;
  if (tumbly::metaGetString(metaDoc, String("logger.log_base_name"), s))
  {
    gLogBaseName = s;
    Serial.print(F("logger.log_base_name: "));
    Serial.println(gLogBaseName);
  }

  String modeStr;
  if (tumbly::metaGetString(metaDoc, String("logger.log_file_mode"), modeStr))
  {
    gLogFileMode = parseLogFileMode(modeStr);
    Serial.print(F("logger.log_file_mode: "));
    Serial.println(logFileModeText(gLogFileMode));
  }

  bool rebootInc = false;
  if (tumbly::metaGetBool(metaDoc, String("logger.inc_on_reboot"), rebootInc))
  {
    gIncOnReboot = rebootInc;
    Serial.print(F("logger.inc_on_reboot: "));
    Serial.println(gIncOnReboot ? F("true") : F("false"));
  }

  JsonArrayConst fields{};
  if (tumbly::metaGetJsonArray(metaDoc, String("logger.log_fields"), fields))
  {
    const size_t fieldCount = fields.size();
    if (fieldCount > 0)
    {
      auto *fieldNames = new String[fieldCount];
      for (size_t fi = 0; fi < fieldCount; ++fi)
      {
        fieldNames[fi] = fields[fi].as<String>();
      }
      gLogContext.fieldMask = tumbly::buildCsvFieldMaskFromNames(
          fieldNames, fieldCount, gLogContext.fieldMask, &Serial);
      delete[] fieldNames;
      Serial.print(F("logger.log_fields applied: "));
      Serial.println(tumbly::DataLoggerHelper::csvHeader(gLogContext.fieldMask));
    }
  }
}

static void beginHublink()
{
  if (!hublink.begin())
  {
    Serial.println(F("HubWheelHublink: Hublink begin failed."));
    return;
  }

  Serial.println(F("HubWheelHublink: Hublink initialized."));
  hublink.setTimestampCallback(onTimestampReceived);

  applyWheelLoggerMetaFromSd();
  applyLogPolicyDefaults();
}

static void runHublinkSyncWindow(uint32_t syncForSeconds)
{
  // Keep node characteristic battery level fresh before each sync attempt.
  // Dashboard policy for this sketch:
  // - Valid cell reading -> report actual SOC.
  // - No valid reading but USB present -> report 100% (externally powered lab setup).
  // - No valid reading and no USB -> report 0% to avoid stale/null data.
  const bool usbPresent = node.readUsbSense();
  const tumbly::BatteryReading battery = node.powerGauge().readSample();
  Serial.print(F("HubWheelHublink: battery status="));
  Serial.print(tumbly::statusToString(battery.status));
  Serial.print(F(" hasCell="));
  Serial.print(battery.hasCellReading ? F("true") : F("false"));
  Serial.print(F(" soc="));
  Serial.print(battery.stateOfChargePct, 1);
  Serial.print(F(" usbPresent="));
  Serial.println(usbPresent ? F("true") : F("false"));

  if (battery.status == tumbly::ServiceStatus::Ok && battery.hasCellReading &&
      battery.stateOfChargePct > 0.0f)
  {
    const int batteryPct = static_cast<int>(battery.stateOfChargePct + 0.5f);
    hublink.setBatteryLevel(static_cast<uint8_t>(constrain(batteryPct, 0, 100)));
    Serial.print(F("HubWheelHublink: setBatteryLevel from gauge="));
    Serial.println(batteryPct);
  }
  else if (usbPresent)
  {
    hublink.setBatteryLevel(100);
    Serial.println(F("HubWheelHublink: setBatteryLevel fallback=100 (USB present)"));
  }
  else
  {
    hublink.setBatteryLevel(0);
    Serial.println(F("HubWheelHublink: setBatteryLevel fallback=0 (no USB / no valid gauge)"));
  }
  // Uses existing Hublink config from meta.json/hardcoded defaults.
  hublink.sync(syncForSeconds);
}

/// Alternating front/back quickly; returns true if boot button held (skip long reset sync).
static bool waitBootHoldToSkipLongSync()
{
  const uint32_t startMs = millis();
  uint32_t lastToggleMs = startMs;
  bool frontHigh = true;
  digitalWrite(tumbly::PIN_LED_FRONT, frontHigh ? HIGH : LOW);
  digitalWrite(tumbly::PIN_LED_BACK, frontHigh ? LOW : HIGH);
  while (static_cast<uint32_t>(millis() - startMs) < kLongSyncBootHoldMs)
  {
    if (digitalRead(tumbly::PIN_BOOT_BUTTON) == LOW)
    {
      Serial.println(F("HubWheelHublink: boot held — skipping long reset sync"));
      digitalWrite(tumbly::PIN_LED_FRONT, HIGH);
      digitalWrite(tumbly::PIN_LED_BACK, LOW);
      return true;
    }
    const uint32_t nowMs = millis();
    if (static_cast<uint32_t>(nowMs - lastToggleMs) >= kLongSyncBootBlinkHalfMs)
    {
      lastToggleMs = nowMs;
      frontHigh = !frontHigh;
      digitalWrite(tumbly::PIN_LED_FRONT, frontHigh ? HIGH : LOW);
      digitalWrite(tumbly::PIN_LED_BACK, frontHigh ? LOW : HIGH);
    }
    delay(1);
  }
  digitalWrite(tumbly::PIN_LED_FRONT, HIGH);
  digitalWrite(tumbly::PIN_LED_BACK, LOW);
  return false;
}

static void appendWheelLogRow()
{
  tumbly::SampleFields sample;
  sample = logger.captureSample();
  sample.passesPerMin =
      tumbly::computePassesPerMinute(sample.magnetPassCount, gSleepTimeSeconds);

  String logPath;
  const tumbly::ServiceStatus pathStatus =
      tumbly::resolveLogFilePath(node.sd(), gLogContext.filePolicy, sample.rtc, logPath);
  if (pathStatus != tumbly::ServiceStatus::Ok)
  {
    Serial.print(F("HubWheelHublink: log write failed ("));
    Serial.print(tumbly::statusToString(pathStatus));
    Serial.println(F(")"));
    return;
  }

  if (!node.sd().exists(logPath.c_str()))
  {
    const String header = gLogContext.fieldMask == 0
                              ? tumbly::DataLoggerHelper::csvHeader()
                              : tumbly::DataLoggerHelper::csvHeader(gLogContext.fieldMask);
    const tumbly::ServiceStatus headerStatus = node.sd().appendLine(logPath.c_str(), header);
    if (headerStatus != tumbly::ServiceStatus::Ok)
    {
      Serial.print(F("HubWheelHublink: log write failed ("));
      Serial.print(tumbly::statusToString(headerStatus));
      Serial.println(F(")"));
      return;
    }
  }

  const tumbly::ServiceStatus rowStatus = gLogContext.fieldMask == 0
                                             ? logger.appendCsvSample(logPath.c_str(), sample)
                                             : logger.appendCsvSample(logPath.c_str(), sample,
                                                                      gLogContext.fieldMask);
  if (rowStatus != tumbly::ServiceStatus::Ok)
  {
    Serial.print(F("HubWheelHublink: log write failed ("));
    Serial.print(tumbly::statusToString(rowStatus));
    Serial.println(F(")"));
    return;
  }

  Serial.print(F("HubWheelHublink: wheel count this sleep = "));
  Serial.print(sample.magnetPassCount);
  Serial.print(F(" (edges="));
  Serial.print(sample.ulpEdgeCount);
  Serial.println(F(")"));

  gLogCount++;
  Serial.println(F("HubWheelHublink: log write OK"));
}

static void enterSleep()
{
  digitalWrite(tumbly::PIN_LED_FRONT, LOW);
  node.magnetCounter().clearCount();
  node.magnetCounter().begin();
  node.magnetCounter().start();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(gSleepTimeSeconds) * 1000000ULL);
  esp_deep_sleep_start();
}

void setup()
{
  Serial.begin(115200);
  pinMode(tumbly::PIN_LED_FRONT, OUTPUT);
  digitalWrite(tumbly::PIN_LED_FRONT, LOW);
  // Keep LED on while awake; turn off immediately before deep sleep.
  digitalWrite(tumbly::PIN_LED_FRONT, HIGH);
  node.beginHardware();
  node.beginI2C();
  logger.begin();
  if (!node.sd().begin() || node.sd().cardType() == CARD_NONE)
  {
    blinkMissingSdCard();
  }
  const esp_sleep_wakeup_cause_t cause = node.wakeupCause();
  // Offer editor only on power-on/reset wake, not deep-sleep wake cycles.
  if (cause == ESP_SLEEP_WAKEUP_UNDEFINED && node.readUsbSense())
  {
    metaEditor.maybeEnterWithFade(node.sd(), true, Serial, 3000,
                                  tumbly::PIN_LED_FRONT, tumbly::PIN_LED_BACK, &node);
  }
  applyLogPolicyDefaults();
  beginHublink();

  Serial.println();
  Serial.println(F("--------- HubWheelHublink Wake ---------"));
  Serial.print(F("Wake cause: "));
  Serial.print(wakeCauseText(cause));
  Serial.print(F(" ("));
  Serial.print(static_cast<int>(cause));
  Serial.println(F(")"));
  Serial.println();
  blinkPowerOnPattern(cause);

  // Match legacy behavior: only log when waking from timer sleep.
  if (node.isTimerWake())
  {
    appendWheelLogRow();
  }
  else
  {
    gLogCount = 0;
  }

  const uint32_t syncForSeconds =
      (cause == ESP_SLEEP_WAKEUP_UNDEFINED) ? kHublinkSyncSecondsOnReset : gSyncForSeconds;
  const bool syncDue = tumbly::shouldRunSyncWindow(gSleepTimeSeconds, gSyncEverySeconds, gLogCount);
  bool userSkippedLongResetSync = false;
  if (cause == ESP_SLEEP_WAKEUP_UNDEFINED && syncDue && !gHublinkDisable &&
      syncForSeconds == kHublinkSyncSecondsOnReset)
  {
    Serial.println(F("HubWheelHublink: hold boot to skip long reset sync…"));
    userSkippedLongResetSync = waitBootHoldToSkipLongSync();
  }

  if (syncDue && gHublinkDisable)
  {
    Serial.println(F("HubWheelHublink: sync window skipped (hublink.disable=true)"));
    gLogCount = 0;
  }
  else if (syncDue && !gHublinkDisable)
  {
    if (userSkippedLongResetSync)
    {
      Serial.println(F("HubWheelHublink: long reset sync skipped (boot during hold)"));
      gLogCount = 0;
    }
    else
    {
      Serial.print(F("HubWheelHublink: sync window "));
      Serial.print(syncForSeconds);
      Serial.println(F("s"));
      runHublinkSyncWindow(syncForSeconds);
      gLogCount = 0;
    }
  }
  else
  {
    Serial.println(F("HubWheelHublink: sync window skipped"));
  }

  Serial.println(F("--------- Entering deep sleep ----------"));
  Serial.println();

  enterSleep();
}

void loop() {}
