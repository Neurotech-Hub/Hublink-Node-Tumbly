#include <HublinkNodeTumbly.h>
#include <esp_sleep.h>

// HubWheelMinimal:
// - Core wheel logging + deep-sleep loop (same flow as HubWheelHublink without the Hublink library).
// - Optional USB Serial MetaConfigEditor on power-on (same hold as HubWheelHublink); no gateway sync.
// - Hardcoded sleep interval, log file policy, and default CSV columns match HubWheelHublink when
//   meta.json is absent; see examples/HubWheelHublink/HubWheelHublink.ino for wheel.* / logger.*
//   keys and the sample meta.json comment block.

tumbly::HublinkNode node;
tumbly::DataLoggerHelper logger(node);
tumbly::MetaConfigEditor metaEditor;

// Core wheel timing policy.
constexpr uint32_t kSleepTimeSeconds = 10;
// File naming policy for logger helper.
constexpr char kLogBaseName[] = "HUBWHEEL";
constexpr tumbly::FileNameMode kLogFileMode = tumbly::FileNameMode::Daily;
// Default CSV columns: deep-sleep wheel + gauge (same mask order as HubWheelHublink kCsvFieldMask).
static constexpr tumbly::CsvFieldMask kCsvFieldMask = tumbly::csvFields({
    tumbly::CsvField::RtcUnix,
    tumbly::CsvField::UlpEdges,
    tumbly::CsvField::MagnetPasses,
    tumbly::CsvField::PassesPerMin,
    tumbly::CsvField::BattV,
    tumbly::CsvField::BattPer,
});

struct LogContext
{
  tumbly::LogFilePolicy filePolicy;
  tumbly::CsvFieldMask fieldMask = 0;
};

LogContext gLogContext = {
    {kLogBaseName, kLogFileMode, 0, false},
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
  Serial.println(F("HubWheel: SD card not present. Halting."));
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

static void appendWheelLogRow()
{
  tumbly::SampleFields sample;
  sample = logger.captureSample();
  sample.passesPerMin =
      tumbly::computePassesPerMinute(sample.magnetPassCount, kSleepTimeSeconds);

  String logPath;
  const tumbly::ServiceStatus pathStatus =
      tumbly::resolveLogFilePath(node.sd(), gLogContext.filePolicy, sample.rtc, logPath);
  if (pathStatus != tumbly::ServiceStatus::Ok)
  {
    Serial.print(F("HubWheel: log write failed ("));
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
      Serial.print(F("HubWheel: log write failed ("));
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
    Serial.print(F("HubWheel: log write failed ("));
    Serial.print(tumbly::statusToString(rowStatus));
    Serial.println(F(")"));
    return;
  }

  Serial.print(F("HubWheel: wheel count this sleep = "));
  Serial.print(sample.magnetPassCount);
  Serial.print(F(" (edges="));
  Serial.print(sample.ulpEdgeCount);
  Serial.println(F(")"));

  Serial.println(F("HubWheel: log write OK"));
}

static void enterSleep()
{
  digitalWrite(tumbly::PIN_LED_FRONT, LOW);
  node.magnetCounter().clearCount();
  node.magnetCounter().begin();
  node.magnetCounter().start();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(kSleepTimeSeconds) * 1000000ULL);
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
  // Offer editor only on power-on/reset wake, not deep-sleep wake cycles (same as HubWheelHublink).
  if (cause == ESP_SLEEP_WAKEUP_UNDEFINED && node.readUsbSense())
  {
    metaEditor.maybeEnterWithFade(node.sd(), true, Serial, 3000,
                                  tumbly::PIN_LED_FRONT, tumbly::PIN_LED_BACK, &node);
  }

  Serial.println();
  Serial.println(F("--------- HubWheelMinimal Wake ---------"));
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

  Serial.println(F("--------- Entering deep sleep ----------"));
  Serial.println();

  enterSleep();
}

void loop() {}
