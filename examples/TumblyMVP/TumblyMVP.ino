#include <HublinkNodeTumbly.h>
#include <Wire.h>

tumbly::HublinkNode node;

static constexpr uint32_t kRefreshMs = 1000;

static const __FlashStringHelper *sdTypeName(uint8_t type) {
  switch (type) {
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

static void scanAndPrintI2c(Stream &out) {
  out.println(F("--- I2C scan (7-bit) ---"));
  uint8_t found = 0;
  for (uint8_t addr = 0x01; addr < 0x78; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      out.print(F("  0x"));
      if (addr < 0x10) {
        out.print('0');
      }
      out.println(addr, HEX);
      ++found;
    }
  }
  if (found == 0) {
    out.println(F("  (no devices ACK'd)"));
  } else {
    out.print(F("  total="));
    out.println(found);
  }
}

static void printInitStatus(Stream &out) {
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

static uint8_t readButtonMask() {
  uint8_t mask = 0;
  if (node.buttons().isPressed(0)) {
    mask |= 0x01;
  }
  if (node.buttons().isPressed(1)) {
    mask |= 0x02;
  }
  if (node.buttons().isPressed(2)) {
    mask |= 0x04;
  }
  if (digitalRead(tumbly::PIN_BOOT_BUTTON) == LOW) {
    mask |= 0x08;
  }
  return mask;
}

static void setFrontLed(bool on) {
  digitalWrite(tumbly::PIN_LED_FRONT, on ? HIGH : LOW);
}

/// Toggle the front LED when any button (incl. boot) changes state since last sample.
static void updateFrontLedOnButtonChange(uint8_t currentMask, uint8_t &prevMask, bool &frontLedOn) {
  if (currentMask != prevMask) {
    frontLedOn = !frontLedOn;
    setFrontLed(frontLedOn);
    prevMask = currentMask;
  }
}

static void renderButtonScreen() {
  if (!node.screen().isInitialized()) {
    return;
  }

  char line0[22];
  char line1[22];
  char line2[22];
  char line3[22];

  snprintf(line0, sizeof(line0), "B0:%s B1:%s B2:%s",
           node.buttons().isPressed(0) ? "DN" : "UP",
           node.buttons().isPressed(1) ? "DN" : "UP",
           node.buttons().isPressed(2) ? "DN" : "UP");
  snprintf(line1, sizeof(line1), "BOOT:%s PGOOD:%s",
           digitalRead(tumbly::PIN_BOOT_BUTTON) == LOW ? "DN" : "UP",
           node.readUsbSense() ? "OK" : "--");
  snprintf(line2, sizeof(line2), "SD:%s TOUCH:%u",
           node.readSdDetect() ? "IN" : "OUT",
           node.readTouchRaw());
  snprintf(line3, sizeof(line3), "AUX0:%s AUX1:%s",
           digitalRead(tumbly::PIN_AUX_GPIO0) == HIGH ? "HI" : "LO",
           digitalRead(tumbly::PIN_AUX_GPIO1) == HIGH ? "HI" : "LO");

  node.screen().printLines(line0, line1, line2, line3);
}

static void printButtonState(Stream &out) {
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

void setup() {
  Serial.begin(115200);
  delay(200);

  node.beginHardware();

  // OLED is on the I2C isolator; PIN_I2C_EN must be LOW (active-low enable).
  node.setI2CPowerEnabled(true);
  delay(100);

  node.beginI2C();
  delay(100);

  node.rtc().begin();
  node.powerGauge().begin();
  node.light().begin();
  node.environment().begin();
  node.servo().begin();

  if (node.screen().begin()) {
    node.screen().printLines("Hublink Node", "Tumbly MVP", "Screen OK", "Buttons below...");
  } else {
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
  renderButtonScreen();
}

void loop() {
  static uint8_t prevButtonMask = 0xFF; // force first sample to establish baseline without toggling
  static bool frontLedOn = false;
  static bool seeded = false;

  const uint8_t buttonMask = readButtonMask();
  if (!seeded) {
    prevButtonMask = buttonMask;
    seeded = true;
  } else {
    updateFrontLedOnButtonChange(buttonMask, prevButtonMask, frontLedOn);
  }

  printButtonState(Serial);
  renderButtonScreen();

  delay(kRefreshMs);
}
