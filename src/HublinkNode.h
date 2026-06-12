#pragma once

#include "hardware/TumblyPins.h"
#include "services/ButtonService.h"
#include "services/EnvService.h"
#include "services/LightService.h"
#include "services/MagnetCounterService.h"
#include "services/PowerGaugeService.h"
#include "services/RtcService.h"
#include "services/ScreenService.h"
#include "services/SdService.h"
#include "services/ServoService.h"
#include <esp_sleep.h>
#include <Wire.h>

namespace tumbly {

class HublinkNode {
public:
  /// Default CPU clock before pin setup. 80 MHz is a common choice for reliable Wi‑Fi / Bluetooth on ESP32-S3.
  static constexpr uint32_t kDefaultMcuClockMhz = 80;

  /// Set the ESP32 CPU frequency (Arduino-ESP32 `setCpuFrequencyMhz`). Typical values: 80, 160, 240 MHz.
  static bool setMcuClockMhz(uint32_t mhz);
  /// Current CPU frequency in MHz (from `getCpuFrequencyMhz()`).
  static uint32_t mcuClockMhz();

  /// @param mcuClockMhz Applied first via `setMcuClockMhz`; default is `kDefaultMcuClockMhz`.
  bool beginHardware(uint32_t mcuClockMhz = kDefaultMcuClockMhz);
  bool beginI2C(uint32_t clockHz = DEFAULT_I2C_CLOCK_HZ);

  void setI2CPowerEnabled(bool enabled);
  bool isI2CPowerEnabled() const;

  /// Active HIGH 5V rail enable on `PIN_5V_EN`. 470k pulldown holds the rail off when this is LOW.
  void set5VPowerEnabled(bool enabled);
  bool is5VPowerEnabled() const;

  bool readMagnet() const;
  /// Backed by `~PGOOD` from BQ24075 on `PIN_USB_SENSE` (GPIO34). True when an input
  /// power source (USB or charger) is good. Name retained for CSV/API compatibility.
  bool readUsbSense() const;
  /// True when an SD card is physically seated (`PIN_SD_DET` LOW).
  bool readSdDetect() const;
  /// True when `~FUEL_ALERT` from the MAX17048 is asserted (`PIN_FUEL_ALERT` LOW).
  bool readFuelAlert() const;
  /// True when `~RTC_INT` from the DS3231 is asserted (`PIN_RTC_INT` LOW).
  bool readRtcInt() const;
  /// Raw ADC sample from the external touch pin (`PIN_TOUCH`).
  uint16_t readTouchRaw() const;

  /// Drives both red LEDs (front + back) together.
  void setStatusLeds(bool on);
  esp_sleep_wakeup_cause_t wakeupCause() const;
  bool isTimerWake() const;

  SdService &sd() { return sd_; }
  RtcService &rtc() { return rtc_; }
  PowerGaugeService &powerGauge() { return powerGauge_; }
  LightService &light() { return light_; }
  EnvService &environment() { return environment_; }
  MagnetCounterService &magnetCounter() { return magnetCounter_; }
  ButtonService &buttons() { return buttons_; }
  ServoService &servo() { return servo_; }
  ScreenService &screen() { return screen_; }

private:
  bool hardwareInitialized_ = false;

  SdService sd_;
  RtcService rtc_;
  PowerGaugeService powerGauge_;
  LightService light_;
  EnvService environment_;
  MagnetCounterService magnetCounter_;
  ButtonService buttons_;
  ServoService servo_;
  ScreenService screen_;
};

} // namespace tumbly
