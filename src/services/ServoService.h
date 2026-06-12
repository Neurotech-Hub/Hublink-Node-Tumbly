#pragma once

#include "../hardware/TumblyPins.h"
#include "ServiceTypes.h"
#include <Arduino.h>
#include <ESP32Servo.h>

namespace tumbly {

class ServoService {
public:
  /// Standard hobby-servo PWM endpoints (µs). Override in `attach()` if needed.
  static constexpr uint16_t kDefaultMinUs = 500;
  static constexpr uint16_t kDefaultMaxUs = 2500;

  /// Configures `PIN_SRV_EN` as OUTPUT and leaves the servo rail disabled.
  bool begin();

  /// Drives `PIN_SRV_EN` (active LOW) to enable/disable the SN74AHCT1G125 buffer
  /// that supplies the servo rail. Does not attach the PWM channel.
  void setPowerEnabled(bool enabled);
  bool isPowerEnabled() const { return powerEnabled_; }

  /// Enables servo power and attaches the PWM channel on `PIN_SRVO`.
  bool attach(uint16_t minUs = kDefaultMinUs, uint16_t maxUs = kDefaultMaxUs);

  /// Releases the PWM channel and disables servo power.
  void detach();
  bool isAttached() const { return attached_; }

  void writeMicroseconds(uint16_t us);
  void writeDegrees(int degrees);

  /// 12-bit ADC sample from `PIN_FBK0` (0..4095 by default).
  uint16_t readFeedbackRaw();

private:
  Servo servo_;
  bool initialized_ = false;
  bool attached_ = false;
  bool powerEnabled_ = false;
};

} // namespace tumbly
