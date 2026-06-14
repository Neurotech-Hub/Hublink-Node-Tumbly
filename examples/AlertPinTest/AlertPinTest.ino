/*
 * AlertPinTest — exercise the separate alert pins on Tumbly
 *
 * Hardware: on Tumbly the two alert sources go to dedicated GPIOs (no AND gate):
 *   - PIN_RTC_INT    (GPIO21): ~RTC_INT from DS3231, open-drain active LOW
 *   - PIN_FUEL_ALERT (GPIO18): ~ALRT from MAX17048,  open-drain active LOW
 * Both pins are configured `INPUT_PULLUP` by `HublinkNode::beginHardware()`.
 *
 * What this sketch does:
 *   1. Configures DS3231 INTCN and arms Alarm1 a few seconds in the future.
 *      Waits for `PIN_RTC_INT` to assert LOW and `rtc.alarmFired(1)` to be true.
 *   2. (Optional) Programs an "absurd" upper voltage alert on the MAX17048 so any
 *      real pack trips VHi. Waits for `PIN_FUEL_ALERT` to assert LOW with `VHi` set.
 *   3. Disables the I2C rail (`setI2CPowerEnabled(false)`) and confirms neither
 *      alert pin spuriously changes when the DS3231 is unpowered (pull-ups should
 *      hold both lines HIGH). The MAX17048 is also gated by the I2C isolator.
 *
 * Notes:
 *   - MAX17048: Adafruit `cellVoltage()` returns `NaN` if `isDeviceReady()` is false.
 *     Never display that as 0.000 V. With a present pack, a true ~0.000 V read
 *     usually means a VCELL sense / bring-up issue; we print chip version/ID to help.
 *     After `wake()`, allow ~200 ms before read.
 *   - RTClib::setAlarm1() requires INTCN=1 in DS3231 control 0x0E bit 2; we set
 *     that before setAlarm1().
 *   - The front red LED mirrors EITHER alert asserted (OR semantics).
 */

#include <Adafruit_MAX1704X.h>
#include <HublinkNodeTumbly.h>
#include <RTClib.h>
#include <esp_log.h>
#include <Wire.h>

// DS3231 (same as RTClib; local helpers for a tiny control tweak)
static constexpr uint8_t kDs3231Addr = 0x68;
static constexpr uint8_t kDs3231Control = 0x0E;
static constexpr uint8_t kDs3231Status = 0x0F;

static bool ds3231SetIntcn() {
  Wire.beginTransmission(kDs3231Addr);
  Wire.write(kDs3231Control);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(kDs3231Addr), 1) != 1) {
    return false;
  }
  uint8_t c = Wire.read();
  c = (c & 0xE3) | 0x04; // INTCN=1, RS0/RS1=0
  Wire.beginTransmission(kDs3231Addr);
  Wire.write(kDs3231Control);
  Wire.write(c);
  return Wire.endTransmission() == 0;
}

static void ds3231ClearIntFlags() {
  Wire.beginTransmission(kDs3231Addr);
  Wire.write(kDs3231Status);
  if (Wire.endTransmission() != 0) {
    return;
  }
  if (Wire.requestFrom(static_cast<int>(kDs3231Addr), 1) != 1) {
    return;
  }
  uint8_t s = Wire.read();
  s = static_cast<uint8_t>(s & static_cast<uint8_t>(~0x03U)); // A1F, A2F
  Wire.beginTransmission(kDs3231Addr);
  Wire.write(kDs3231Status);
  Wire.write(s);
  Wire.endTransmission();
}

tumbly::HublinkNode node;
RTC_DS3231 rtc;
Adafruit_MAX17048 fuel;

/** Print VCELL; does not treat NaN as 0.00. */
static void printMax17048Status(const __FlashStringHelper *label) {
  Serial.print(label);
  Serial.print(F("  ready="));
  Serial.print(fuel.isDeviceReady() ? F("y") : F("n"));
  Serial.print(F("  chip=0x"));
  Serial.print(fuel.getChipID(), HEX);
  Serial.print(F("  icVer=0x"));
  Serial.print(fuel.getICversion(), HEX);
  Serial.print(F("  VCELL="));
  const float v = fuel.cellVoltage();
  if (isnan(v)) {
    Serial.print(F("NaN (not valid per driver — not the same as 0.000V)"));
  } else {
    Serial.print(v, 3);
    Serial.print(F(" V"));
    if (v < 0.01f) {
      Serial.print(
          F("  [if pack is present, check VBAT/VCELL sense to MAX17048; may need "
            "settling time]"));
    }
  }
  Serial.println();
}

bool gMaxOk = false;
bool gDs3231IntOk = false;
bool gMax17048IntOk = false;
bool gFuelTestSkipped = false;
bool gI2cOffAlertsUnchanged = false;
static uint8_t gFuelAmbiguousPrints = 0;
enum class Phase : uint8_t {
  WaitRtc,
  RtcCleared,
  ArmFuel,
  WaitFuel,
  I2cOffVerify,
  Done,
};

Phase gPhase = Phase::WaitRtc;
uint32_t gPhaseTime = 0;
bool gPrevRtcLow = false;
bool gPrevFuelLow = false;

static bool readRtcAlert() { return digitalRead(tumbly::PIN_RTC_INT) == LOW; }
static bool readFuelAlert() { return digitalRead(tumbly::PIN_FUEL_ALERT) == LOW; }

static void printFinalResult() {
  Serial.println();
  Serial.println(F("========== ALERT TEST RESULT =========="));
  Serial.print(F("DS3231  (PIN_RTC_INT, via A1F):     "));
  Serial.println(gDs3231IntOk ? F("OK") : F("NOT CONFIRMED"));
  if (!gMaxOk) {
    Serial.println(F("MAX17048:                            SKIPPED (not ready at boot)"));
  } else if (gFuelTestSkipped) {
    Serial.print(F("MAX17048  (PIN_FUEL_ALERT):          "));
    Serial.println(F("SKIPPED (VCELL invalid/low)"));
  } else {
    Serial.print(F("MAX17048  (PIN_FUEL_ALERT, VHi):     "));
    Serial.println(gMax17048IntOk ? F("OK") : F("NOT CONFIRMED"));
  }
  Serial.print(F("I2C off  (no spurious alert change): "));
  Serial.println(gI2cOffAlertsUnchanged ? F("OK (both pins stable)")
                                         : F("NOT OK (a pin changed)"));
  Serial.print(F("COMBINED:                            "));
  if (gI2cOffAlertsUnchanged && gDs3231IntOk && gMax17048IntOk) {
    Serial.println(F("SUCCESS — both alert sources + I2C-off stability confirmed."));
  } else if (gI2cOffAlertsUnchanged && gDs3231IntOk && (gFuelTestSkipped || !gMaxOk)) {
    Serial.println(
        F("PARTIAL — DS3231 + I2C-off OK; MAX17048 path not fully tested. Re-run with "
          "valid pack and VCELL."));
  } else if (gI2cOffAlertsUnchanged && gMax17048IntOk && !gDs3231IntOk) {
    Serial.println(F("PARTIAL — MAX17048 + I2C-off OK; DS3231 not confirmed (unexpected)."));
  } else if (!gI2cOffAlertsUnchanged) {
    Serial.println(
        F("PARTIAL or FAIL — disabling I2C_EN caused an alert pin to change; other rows "
          "above may still be OK."));
  } else {
    Serial.println(F("FAIL or INCOMPLETE — see messages above."));
  }
  Serial.println(F("======================================"));
}

/** I2C_EN HIGH = rail off; DS3231 + MAX17048 lose power. Expect neither alert
 *  pin to change state because both have internal pull-ups. */
static void runI2cOffAlertStabilityTest() {
  Serial.println();
  Serial.println(F("========== I2C rail off (last check) =========="));
  Serial.println(
      F("PIN_I2C_EN -> HIGH: I2C / DS3231 / MAX17048 unpowered. With pull-ups, neither "
        "alert pin should spuriously change."));

  const bool rtcLowBefore = readRtcAlert();
  const bool fuelLowBefore = readFuelAlert();
  Serial.print(F("RTC_INT    before: "));
  Serial.println(rtcLowBefore ? F("LOW (asserted)") : F("HIGH (idle)"));
  Serial.print(F("FUEL_ALERT before: "));
  Serial.println(fuelLowBefore ? F("LOW (asserted)") : F("HIGH (idle)"));

  node.setI2CPowerEnabled(false); // active-low en: pin HIGH = rail off
  delay(200);

  const bool rtcLowAfter = readRtcAlert();
  const bool fuelLowAfter = readFuelAlert();
  Serial.print(F("RTC_INT    after:  "));
  Serial.println(rtcLowAfter ? F("LOW (asserted)") : F("HIGH (idle)"));
  Serial.print(F("FUEL_ALERT after:  "));
  Serial.println(fuelLowAfter ? F("LOW (asserted)") : F("HIGH (idle)"));

  gI2cOffAlertsUnchanged = (rtcLowBefore == rtcLowAfter) && (fuelLowBefore == fuelLowAfter);
  if (gI2cOffAlertsUnchanged) {
    Serial.println(F("PASS: alert pins stable when I2C rail was disabled."));
  } else {
    Serial.println(F("FAIL: an alert pin changed when I2C rail was disabled — check "
                     "pull-ups and routing."));
  }

  node.setI2CPowerEnabled(true);
  delay(30);
  node.beginI2C();
  Serial.println(F("I2C rail re-enabled; Wire clock restored for next run."));
  Serial.println(F("============================================="));
  gPhase = Phase::Done;
}

// Optional: set true to also wait for a MAX17048 window after the RTC part.
constexpr bool kRunFuelTest = true;
// Time until the DS3231 alarm (minute + second match, single shot within the hour)
constexpr int kRtcFirstFireSeconds = 12;
// Wait for RTC: alarm time + margin (ms)
static constexpr uint32_t kWaitRtcTimeoutMs = 40000;

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) {
    delay(10);
  }

  esp_log_level_set("i2c.master", ESP_LOG_NONE);
  node.beginHardware();
  node.setI2CPowerEnabled(true);
  node.beginI2C();

  // PIN_RTC_INT, PIN_FUEL_ALERT, and PIN_LED_FRONT are configured by beginHardware().
  digitalWrite(tumbly::PIN_LED_FRONT, LOW);

  if (!rtc.begin(&Wire)) {
    Serial.println(F("DS3231: not found. Check I2C_EN, wiring, and power."));
  } else {
    if (!ds3231SetIntcn()) {
      Serial.println(F("DS3231: could not set INTCN (alarm on /INT)"));
    }
    ds3231ClearIntFlags();

    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    const DateTime now = rtc.now();
    const DateTime alarm = now + TimeSpan(0, 0, 0, kRtcFirstFireSeconds);
    if (rtc.setAlarm1(alarm, DS3231_A1_Minute)) {
      Serial.print(F("DS3231: alarm1 armed for "));
      Serial.print(alarm.timestamp(DateTime::TIMESTAMP_TIME));
      Serial.print(F(" (minute+second, ~"));
      Serial.print(kRtcFirstFireSeconds);
      Serial.println(F(" s)"));
    } else {
      Serial.println(F("DS3231: setAlarm1 failed (INTCN? valid time?)"));
    }
  }

  gMaxOk = fuel.begin(&Wire) && fuel.isDeviceReady();
  if (!gMaxOk) {
    Serial.println(F("MAX17048: not ready (unplugged / weak pack is common)."));
  } else {
    fuel.wake();
    delay(200);
    printMax17048Status(F("MAX17048 (startup):"));
  }

  gPhase = Phase::WaitRtc;
  gPhaseTime = millis();
  gPrevRtcLow = readRtcAlert();
  gPrevFuelLow = readFuelAlert();
  Serial.println(F("Watch PIN_RTC_INT (GPIO21) and PIN_FUEL_ALERT (GPIO18); active LOW."));
  Serial.println(F("Front red LED mirrors either alert asserted."));
  Serial.println(F("---"));
}

void tryArmFuel() {
  if (!gMaxOk || !kRunFuelTest) {
    gFuelTestSkipped = true;
    gPhase = Phase::I2cOffVerify;
    return;
  }
  fuel.wake();
  delay(200);
  float v = fuel.cellVoltage();
  for (uint8_t n = 0; n < 5 && (isnan(v) || v < 0.1f); n++) {
    delay(100);
    v = fuel.cellVoltage();
  }
  printMax17048Status(F("MAX17048 (arm fuel test):"));
  if (isnan(v) || v < 0.25f) {
    Serial.println(
        F("MAX17048: skip fuel ALRT test — NaN or V < 0.25V (if battery is on-board, "
          "see note above)"));
    gFuelTestSkipped = true;
    gPhase = Phase::I2cOffVerify;
    fuel.hibernate();
    return;
  }
  // "Absurd" upper window: any real cell is above 0.5 V → V Hi alert.
  const float minV = 0.0f;
  const float maxV = 0.5f;
  for (uint8_t b = 0; b < 7; b++) {
    (void)fuel.clearAlertFlag(static_cast<uint8_t>(1U << b));
  }
  fuel.setAlertVoltages(minV, maxV);
  (void)fuel.getAlertStatus();
  Serial.print(F("MAX17048: alert if V < "));
  Serial.print(minV, 2);
  Serial.print(F(" V  OR  V > "));
  Serial.print(maxV, 2);
  Serial.print(F(" V  (intentional: any normal cell >> "));
  Serial.print(maxV, 2);
  Serial.println(F(" V should trip 'high' / ALRT)"));
  gFuelAmbiguousPrints = 0;
  gPhase = Phase::WaitFuel;
  gPhaseTime = millis();
  // Re-sync edge state so a subsequent fall is attributed correctly.
  gPrevFuelLow = readFuelAlert();
}

void loop() {
  const bool rtcLow = readRtcAlert();
  const bool fuelLow = readFuelAlert();
  digitalWrite(tumbly::PIN_LED_FRONT, (rtcLow || fuelLow) ? HIGH : LOW);

  const bool rtcFalling = !gPrevRtcLow && rtcLow;
  const bool fuelFalling = !gPrevFuelLow && fuelLow;
  gPrevRtcLow = rtcLow;
  gPrevFuelLow = fuelLow;

  switch (gPhase) {
  case Phase::WaitRtc: {
    if (rtcFalling) {
      Serial.println(F("PIN_RTC_INT: assert (LOW)"));
    }
    if (rtc.alarmFired(1)) {
      Serial.println(F("DS3231: alarm1 fired (A1F) — clearing /INT"));
      gDs3231IntOk = true;
      rtc.clearAlarm(1);
      ds3231ClearIntFlags();
      gPhase = Phase::RtcCleared;
      gPhaseTime = millis();
    } else if (millis() - gPhaseTime > kWaitRtcTimeoutMs) {
      Serial.println(F("Timeout: no DS3231 alarm. Check INTCN, time, and alarm."));
      gPhase = gMaxOk && kRunFuelTest ? Phase::ArmFuel : Phase::I2cOffVerify;
      gPhaseTime = millis();
    }
    break;
  }
  case Phase::RtcCleared:
    if (millis() - gPhaseTime > 300) {
      gPhase = Phase::ArmFuel;
    }
    break;
  case Phase::ArmFuel:
    tryArmFuel();
    break;
  case Phase::WaitFuel: {
    const uint8_t st = fuel.getAlertStatus();
    const bool vHi = (st & MAX1704X_ALERTFLAG_VOLTAGE_HIGH) != 0;
    if ((fuelFalling || (fuelLow && vHi)) && vHi) {
      Serial.println(F("PIN_FUEL_ALERT: assert (LOW) [VHi set on MAX17048]"));
      Serial.print(F("MAX17048: status=0x"));
      Serial.print(st, HEX);
      Serial.print(F("  (VOLTAGE_HIGH=0x"));
      Serial.print(MAX1704X_ALERTFLAG_VOLTAGE_HIGH, HEX);
      Serial.println(F(")"));
      gMax17048IntOk = true;
      fuel.hibernate();
      gPhase = Phase::I2cOffVerify;
    } else if (fuelFalling && gFuelAmbiguousPrints < 2) {
      gFuelAmbiguousPrints++;
      Serial.println(F("PIN_FUEL_ALERT: assert (LOW) [no VHi set — ambiguous]"));
      Serial.print(F("  status=0x"));
      Serial.println(st, HEX);
    } else if (millis() - gPhaseTime > 20000) {
      Serial.println(
          F("No unambiguous fuel assert in window (normal if no pack or wrong thresholds)."));
      fuel.hibernate();
      gPhase = Phase::I2cOffVerify;
    }
    break;
  }
  case Phase::I2cOffVerify:
    runI2cOffAlertStabilityTest();
    break;
  case Phase::Done:
  default: {
    static bool printed = false;
    if (!printed) {
      printed = true;
      printFinalResult();
      Serial.println(F("Test finished. Re-flash or reset to run again."));
    }
    break;
  }
  }

  delay(20);
}
