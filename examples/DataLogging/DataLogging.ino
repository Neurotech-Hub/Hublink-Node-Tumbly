#include <HublinkNodeTumbly.h>

tumbly::HublinkNode node;
tumbly::DataLoggerHelper logger(node);

constexpr char kLogBaseName[] = "LOGGER";
constexpr tumbly::FileNameMode kLogFileMode = tumbly::FileNameMode::Disabled;

tumbly::LogFilePolicy gLogFilePolicy = {
    kLogBaseName,
    kLogFileMode,
    0,
    false,
};

// This sketch stays awake (no deep sleep / ULP counting), so omit ulp_edges, magnet_passes,
// and passes_min; live GPIO magnet state is enough.
static constexpr tumbly::CsvFieldMask kCsvFieldMask = tumbly::csvFields({
    tumbly::CsvField::Millis,
    tumbly::CsvField::RtcUnix,
    tumbly::CsvField::DateTime,
    tumbly::CsvField::RtcTempC,
    tumbly::CsvField::BattV,
    tumbly::CsvField::BattPer,
    tumbly::CsvField::Lux,
    tumbly::CsvField::Als,
    tumbly::CsvField::White,
    tumbly::CsvField::TempC,
    tumbly::CsvField::PressureHpa,
    tumbly::CsvField::HumidityPct,
    tumbly::CsvField::GasKOhm,
    tumbly::CsvField::AltM,
    tumbly::CsvField::Magnet,
    tumbly::CsvField::UsbSense,
});

static void blinkMissingSdCard()
{
  Serial.println(F("DataLogging: SD card not present. Halting."));
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

void setup()
{
  Serial.begin(115200);
  pinMode(tumbly::PIN_LED_FRONT, OUTPUT);
  // Keep LED on while the sketch is active/awake for bring-up visibility.
  digitalWrite(tumbly::PIN_LED_FRONT, HIGH);

  if (!node.beginHardware())
  {
    Serial.println(F("Init: beginHardware failed."));
  }
  if (!node.beginI2C())
  {
    Serial.println(F("Init: beginI2C failed."));
  }
  if (!node.rtc().begin())
  {
    Serial.println(F("Init: DS3231 not found."));
  }

  if (!logger.begin())
  {
    Serial.println(F("Init: logger begin failed."));
  }
  if (!node.sd().begin() || node.sd().cardType() == CARD_NONE)
  {
    blinkMissingSdCard();
  }

  Serial.println();
  Serial.println(F("--------- DataLogging Active -----------"));
  Serial.print(F("CSV Header: "));
  Serial.println(tumbly::DataLoggerHelper::csvHeader(kCsvFieldMask));
  Serial.println(F("----------------------------------------"));
}

void loop()
{
  Serial.println();
  Serial.println(F("--------- DataLogging Cycle ------------"));
  tumbly::SampleFields sample;
  String logPath;
  tumbly::ServiceStatus logStatus = tumbly::captureAndAppendManagedCsv(
      logger, node, gLogFilePolicy, kCsvFieldMask, sample, &logPath);
  if (logStatus != tumbly::ServiceStatus::Ok)
  {
    Serial.println(F("Log write failed"));
    delay(5000);
    return;
  }

  String csvLine = tumbly::DataLoggerHelper::toCsv(sample, kCsvFieldMask);
  Serial.print(F("Log write: "));
  Serial.println(tumbly::statusToString(logStatus));
  Serial.print(F("Log path: "));
  Serial.println(logPath);
  Serial.print(F("Logged CSV: "));
  Serial.println(csvLine);
  Serial.println(F("----------------------------------------"));

  delay(5000);
}
