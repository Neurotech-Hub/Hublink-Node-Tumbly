#include <HublinkNodeTumbly.h>
#include <Wire.h>
#include <esp_sleep.h>

// TumblyMVP — bring-up sketch: sensors, OLED, servo pulse, and multi-button power tests.
//
// Multi-button holds (checked each loop(), in this order):
//   B0 + B2  Deep sleep 5 s. Teardown SD / I2C / 5V / screen / servo / LEDs, then
//            esp_deep_sleep_start(). Wakes with full reset (setup() runs again).
//   B1 + B2  Light sleep 5 s. Same teardown as deep sleep, then esp_light_sleep_start();
//            execution resumes in loop() with I2C, screen, and SD restored.
//   B0 + B1  I2C power-off test. Drops I2C rail (OLED + I2C sensors) until both
//            buttons are released; use to measure screen/off-isolator current.
//
// If B0+B2 and another combo match at once (e.g. all three held), the first check wins.

tumbly::HublinkNode node;

static constexpr uint32_t kRefreshMs = 500;
static constexpr uint32_t kServoRailSettleMs = 10;
static constexpr uint32_t kServoMoveMs = 400;
static constexpr uint32_t kSleepWakeupSeconds = 5;

static const __FlashStringHelper *sdTypeName(uint8_t type)
{
  switch (type)
  {
  case CARD_MMC:
    return F("MMC");
  case CARD_SD:
    return F("SDSC");
  case CARD_SDHC:
    return F("SDHC");
  default:
    return F("none");
  }
}

static void scanAndPrintI2c(Stream &out)
{
  out.println(F("--- I2C scan (7-bit) ---"));
  uint8_t found = 0;
  for (uint8_t addr = 0x01; addr < 0x78; ++addr)
  {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0)
    {
      out.print(F("  0x"));
      if (addr < 0x10)
      {
        out.print('0');
      }
      out.println(addr, HEX);
      ++found;
    }
  }
  if (found == 0)
  {
    out.println(F("  (no devices ACK'd)"));
  }
  else
  {
    out.print(F("  total="));
    out.println(found);
  }
}

static void printInitStatus(Stream &out)
{
  out.println(F("--- TumblyMVP init ---"));
  out.print(F("MCU clock MHz: "));
  out.println(tumbly::HublinkNode::mcuClockMhz());
  out.print(F("I2C power: "));
  out.println(node.isI2CPowerEnabled() ? F("on") : F("off"));
  out.print(F("5V rail: "));
  out.println(node.is5VPowerEnabled() ? F("on") : F("off"));
  out.print(F("PGOOD (usb_sense): "));
  out.println(node.readUsbSense() ? F("good") : F("absent"));
  out.print(F("SD detect: "));
  out.println(node.readSdDetect() ? F("seated") : F("empty"));
  out.print(F("RTC: "));
  out.println(tumbly::statusToString(node.rtc().readSample().status));
  out.print(F("Fuel gauge: "));
  out.println(tumbly::statusToString(node.powerGauge().readSample().status));
  out.print(F("Light: "));
  out.println(tumbly::statusToString(node.light().readSample().status));
  out.print(F("Env: "));
  out.println(tumbly::statusToString(node.environment().readSample().status));
  out.print(F("Screen: "));
  out.println(node.screen().isInitialized() ? F("ok") : F("not initialized"));
  out.print(F("Servo rail: "));
  out.println(node.servo().isPowerEnabled() ? F("on") : F("off"));
  scanAndPrintI2c(out);
}

static uint8_t readButtonMask()
{
  uint8_t mask = 0;
  if (node.buttons().isPressed(0))
  {
    mask |= 0x01;
  }
  if (node.buttons().isPressed(1))
  {
    mask |= 0x02;
  }
  if (node.buttons().isPressed(2))
  {
    mask |= 0x04;
  }
  if (digitalRead(tumbly::PIN_BOOT_BUTTON) == LOW)
  {
    mask |= 0x08;
  }
  return mask;
}

static void setFrontLed(bool on)
{
  digitalWrite(tumbly::PIN_LED_FRONT, on ? HIGH : LOW);
}

static bool deepSleepButtonsHeld()
{
  return node.buttons().isPressed(0) && node.buttons().isPressed(2);
}

static bool i2cPowerTestButtonsHeld()
{
  return node.buttons().isPressed(0) && node.buttons().isPressed(1);
}

static bool lightSleepButtonsHeld()
{
  return node.buttons().isPressed(1) && node.buttons().isPressed(2);
}

/// Drop SD, I2C, 5V, screen, servo, and LEDs before a sleep mode.
static void teardownExternalLoads()
{
  setFrontLed(false);
  node.setStatusLeds(false);
  node.servo().detach();
  node.screen().end();
  node.sd().end();
  node.set5VPowerEnabled(false);
  node.setI2CPowerEnabled(false);
}

static void restoreAfterLightSleep()
{
  node.setI2CPowerEnabled(true);
  delay(100);
  if (!node.screen().begin())
  {
    Serial.println(F("Screen re-init failed after light sleep"));
  }
  (void)node.sd().begin();
}

/// Cut I2C rail (OLED + sensors) until B0 and B1 are released, then restore.
static void waitForI2cPowerOffTest()
{
  Serial.println(F("TumblyMVP: B0+B1 — I2C off (release to resume)"));
  Serial.flush();

  node.screen().end();
  node.setI2CPowerEnabled(false);

  while (node.buttons().isPressed(0) || node.buttons().isPressed(1))
  {
    delay(10);
  }
  delay(100);

  node.setI2CPowerEnabled(true);
  delay(100);
  if (!node.screen().begin())
  {
    Serial.println(F("Screen re-init failed after I2C restore"));
  }

  Serial.println(F("TumblyMVP: I2C power restored"));
}

/// Drop external loads, then timer-wake deep sleep (does not return).
static void enterDeepSleep()
{
  Serial.println(F("TumblyMVP: B0+B2 held — deep sleep 5s"));
  Serial.flush();

  teardownExternalLoads();

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(kSleepWakeupSeconds) * 1000000ULL);
  esp_deep_sleep_start();
}

/// Same teardown as deep sleep, but light sleep returns to the loop after 5 s.
static void enterLightSleep()
{
  Serial.println(F("TumblyMVP: B1+B2 held — light sleep 5s"));
  Serial.flush();

  teardownExternalLoads();

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(kSleepWakeupSeconds) * 1000000ULL);
  esp_light_sleep_start();

  Serial.println(F("TumblyMVP: light sleep wake"));
  restoreAfterLightSleep();
}

/// Power 5V, attach, write angle, detach, then drop 5V.
/// Returns the last FBK0 ADC sample taken while the rail was still up.
static uint16_t pulseServoDegrees(int degrees)
{
  uint16_t feedback = 0;
  node.set5VPowerEnabled(true);
  delay(kServoRailSettleMs);
  if (node.servo().attach())
  {
    node.servo().writeDegrees(degrees);
    delay(kServoMoveMs);
    feedback = node.servo().readFeedbackRaw();
    node.servo().detach();
  }
  node.set5VPowerEnabled(false);
  return feedback;
}

/// Toggle the front LED when any button (incl. boot) changes state since last sample.
static void updateFrontLedOnButtonChange(uint8_t currentMask, uint8_t &prevMask, bool &frontLedOn)
{
  if (currentMask != prevMask)
  {
    frontLedOn = !frontLedOn;
    setFrontLed(frontLedOn);
    prevMask = currentMask;
  }
}

static void renderButtonScreen(uint16_t servoFeedback)
{
  if (!node.screen().isInitialized())
  {
    return;
  }

  char line0[22];
  char line1[22];
  char line2[22];
  char line3[22];
  char line4[22];
  char line5[22];

  const tumbly::BatteryReading battery = node.powerGauge().readSample();
  const bool hasBattery =
      battery.status == tumbly::ServiceStatus::Ok && battery.hasCellReading;
  const bool hasSoc = hasBattery && !isnan(battery.stateOfChargePct);
  if (hasBattery && hasSoc)
  {
    snprintf(line0, sizeof(line0), "%.2fV %.0f%%", battery.voltageV, battery.stateOfChargePct);
  }
  else if (hasBattery)
  {
    snprintf(line0, sizeof(line0), "%.2fV --%%", battery.voltageV);
  }
  else
  {
    snprintf(line0, sizeof(line0), "--V --%%");
  }

  snprintf(line1, sizeof(line1), "B0:%s B1:%s B2:%s",
           node.buttons().isPressed(0) ? "DN" : "UP",
           node.buttons().isPressed(1) ? "DN" : "UP",
           node.buttons().isPressed(2) ? "DN" : "UP");
  snprintf(line2, sizeof(line2), "BOOT:%s PGOOD:%s SD:%s",
           digitalRead(tumbly::PIN_BOOT_BUTTON) == LOW ? "DN" : "UP",
           node.readUsbSense() ? "OK" : "--",
           node.readSdDetect() ? "IN" : "OUT");

  const tumbly::LightReading light = node.light().readSample();
  const tumbly::EnvReading env = node.environment().readSample();
  const bool hasLux = light.status == tumbly::ServiceStatus::Ok && !isnan(light.lux);
  const bool hasTemp = env.status == tumbly::ServiceStatus::Ok && !isnan(env.temperatureC);
  if (hasLux && hasTemp)
  {
    snprintf(line3, sizeof(line3), "Lux:%.1f T:%.1fC", light.lux, env.temperatureC);
  }
  else if (hasLux)
  {
    snprintf(line3, sizeof(line3), "Lux:%.1f T:--", light.lux);
  }
  else if (hasTemp)
  {
    snprintf(line3, sizeof(line3), "Lux:-- T:%.1fC", env.temperatureC);
  }
  else
  {
    snprintf(line3, sizeof(line3), "Lux:-- T:--");
  }

  snprintf(line4, sizeof(line4), "FBK0:%u", servoFeedback);

  const tumbly::RtcReading rtc = node.rtc().readSample();
  if (rtc.status == tumbly::ServiceStatus::Ok)
  {
    snprintf(line5, sizeof(line5), "%04d-%02d-%02d %02d:%02d:%02d",
             rtc.now.year(), rtc.now.month(), rtc.now.day(),
             rtc.now.hour(), rtc.now.minute(), rtc.now.second());
  }
  else
  {
    snprintf(line5, sizeof(line5), "RTC unavailable");
  }

  node.screen().printLines(line0, line1, line2, line3, line4, line5);
}

static void seedRtcFromBuildTime()
{
  if (!node.rtc().begin(Wire, false))
  {
    Serial.println(F("RTC: not found"));
    return;
  }

  const DateTime buildTime(F(__DATE__), F(__TIME__));
  if (!node.rtc().adjust(buildTime))
  {
    Serial.println(F("RTC: adjust failed"));
    return;
  }

  Serial.print(F("RTC set to build time: "));
  Serial.println(buildTime.timestamp(DateTime::TIMESTAMP_FULL));
}

static void printButtonState(Stream &out)
{
  out.println(F("--- TumblyMVP loop ---"));
  out.print(F("BNT_0 (GPIO"));
  out.print(tumbly::PIN_BNT_0);
  out.print(F("): "));
  out.println(node.buttons().isPressed(0) ? F("pressed") : F("released"));
  out.print(F("BNT_1 (GPIO"));
  out.print(tumbly::PIN_BNT_1);
  out.print(F("): "));
  out.println(node.buttons().isPressed(1) ? F("pressed") : F("released"));
  out.print(F("BNT_2 (GPIO"));
  out.print(tumbly::PIN_BNT_2);
  out.print(F("): "));
  out.println(node.buttons().isPressed(2) ? F("pressed") : F("released"));
  out.print(F("BOOT (GPIO"));
  out.print(tumbly::PIN_BOOT_BUTTON);
  out.print(F("): "));
  out.println(digitalRead(tumbly::PIN_BOOT_BUTTON) == LOW ? F("held") : F("released"));
  out.print(F("Edge since last poll: B0="));
  out.print(node.buttons().wasPressed(0) ? F("yes") : F("no"));
  out.print(F(" B1="));
  out.print(node.buttons().wasPressed(1) ? F("yes") : F("no"));
  out.print(F(" B2="));
  out.println(node.buttons().wasPressed(2) ? F("yes") : F("no"));
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  node.beginHardware();

  if (node.isTimerWake())
  {
    Serial.println(F("TumblyMVP: timer wake"));
  }

  // OLED is on the I2C isolator; PIN_I2C_EN must be LOW (active-low enable).
  node.setI2CPowerEnabled(true);
  delay(100);

  node.beginI2C();
  delay(100);

  seedRtcFromBuildTime();
  node.powerGauge().begin();
  node.light().begin();
  node.environment().begin();
  node.servo().begin();

  if (node.screen().begin())
  {
    node.screen().printLines("Tumbly v1.0 MVP", "Screen OK", "Buttons below...");
  }
  else
  {
    Serial.println(F("Screen init failed (check I2C_EN)"));
  }

  const bool sdMounted = node.sd().begin();
  const uint8_t sdType = node.sd().cardType();

  printInitStatus(Serial);
  Serial.print(F("SD mount: "));
  Serial.print(sdMounted ? F("ok") : F("fail"));
  Serial.print(F("  type="));
  Serial.println(sdTypeName(sdType));

  setFrontLed(false);
  delay(kRefreshMs);
  renderButtonScreen(0);
}

void loop()
{
  static uint8_t prevButtonMask = 0xFF; // force first sample to establish baseline without toggling
  static bool frontLedOn = false;
  static bool seeded = false;

  const uint8_t buttonMask = readButtonMask();

  // Multi-button power tests — see file header for B0+B2 / B1+B2 / B0+B1 behavior.
  if (deepSleepButtonsHeld())
  {
    enterDeepSleep();
  }
  if (lightSleepButtonsHeld())
  {
    enterLightSleep();
  }
  if (i2cPowerTestButtonsHeld())
  {
    waitForI2cPowerOffTest();
  }

  if (!seeded)
  {
    prevButtonMask = buttonMask;
    seeded = true;
  }
  else
  {
    updateFrontLedOnButtonChange(buttonMask, prevButtonMask, frontLedOn);
  }

  printButtonState(Serial);

  static int servoAngle = 0;
  const uint16_t servoFeedback = pulseServoDegrees(servoAngle);
  servoAngle = (servoAngle == 0) ? 180 : 0;

  renderButtonScreen(servoFeedback);

  delay(kRefreshMs);
}
