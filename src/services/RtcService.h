#pragma once

#include "RTClib.h"
#include "ServiceTypes.h"
#include <Wire.h>

namespace tumbly {

struct RtcReading {
  ServiceStatus status = ServiceStatus::NotInitialized;
  DateTime now;
  float temperatureC = NAN;
  bool hadLostPower = false;
};

class RtcService {
public:
  /// Initializes the DS3231. When `setOnLostPower` is true, seeds from firmware
  /// build time (`__DATE__` / `__TIME__`) only if the chip reports lost power or
  /// this firmware build has not been applied yet (tracked in NVS). Always
  /// best-effort syncs the ESP32 system clock from the external RTC when valid.
  bool begin(TwoWire &wire = Wire, bool setOnLostPower = true);
  RtcReading readSample();
  bool adjust(const DateTime &dt);
  bool isInitialized() const { return initialized_; }

  /// Route DS3231 alarms to the open-drain /INT pin (INTCN=1). Clears A1F/A2F.
  bool enableAlarmInterrupt();
  /// Arm Alarm1 for an exact one-shot match at `now + seconds`.
  bool armAlarm1AfterSeconds(uint32_t seconds);
  bool alarm1Fired();
  void clearAlarm1();

private:
  TwoWire *wire_ = nullptr;
  RTC_DS3231 rtc_;
  bool initialized_ = false;
  bool setOnLostPower_ = true;
};

} // namespace tumbly
