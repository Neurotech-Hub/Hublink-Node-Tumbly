#include "RtcService.h"
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>

namespace tumbly {

namespace {

constexpr char kBuildStampNamespace[] = "tumbly_rtc";
constexpr char kBuildStampKey[] = "build";
constexpr uint8_t kDs3231Addr = 0x68;
constexpr uint8_t kDs3231Control = 0x0E;
constexpr uint8_t kDs3231Status = 0x0F;

bool ds3231WriteControlByte(TwoWire *wire, uint8_t value) {
  if (wire == nullptr) {
    return false;
  }
  wire->beginTransmission(kDs3231Addr);
  wire->write(kDs3231Control);
  wire->write(value);
  return wire->endTransmission() == 0;
}

bool ds3231ReadControlByte(TwoWire *wire, uint8_t &out) {
  if (wire == nullptr) {
    return false;
  }
  wire->beginTransmission(kDs3231Addr);
  wire->write(kDs3231Control);
  if (wire->endTransmission() != 0) {
    return false;
  }
  if (wire->requestFrom(static_cast<int>(kDs3231Addr), 1) != 1) {
    return false;
  }
  out = wire->read();
  return true;
}

bool ds3231WriteStatusByte(TwoWire *wire, uint8_t value) {
  if (wire == nullptr) {
    return false;
  }
  wire->beginTransmission(kDs3231Addr);
  wire->write(kDs3231Status);
  wire->write(value);
  return wire->endTransmission() == 0;
}

bool ds3231ReadStatusByte(TwoWire *wire, uint8_t &out) {
  if (wire == nullptr) {
    return false;
  }
  wire->beginTransmission(kDs3231Addr);
  wire->write(kDs3231Status);
  if (wire->endTransmission() != 0) {
    return false;
  }
  if (wire->requestFrom(static_cast<int>(kDs3231Addr), 1) != 1) {
    return false;
  }
  out = wire->read();
  return true;
}

void syncEspClockFromDateTime(const DateTime &dt) {
  if (!dt.isValid()) {
    return;
  }
  const time_t epoch = static_cast<time_t>(dt.unixtime());
  if (epoch >= 1700000000 && epoch <= 2200000000) {
    struct timeval tv = {};
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
  }
}

String firmwareBuildStamp(const char *buildDate, const char *buildTime) {
  return String(buildDate) + " " + String(buildTime);
}

bool storedBuildStampMatches(const String &stamp) {
  Preferences prefs;
  if (!prefs.begin(kBuildStampNamespace, true)) {
    return false;
  }
  const String applied = prefs.getString(kBuildStampKey, "");
  prefs.end();
  return applied == stamp;
}

void storeBuildStamp(const String &stamp) {
  Preferences prefs;
  if (!prefs.begin(kBuildStampNamespace, false)) {
    return;
  }
  prefs.putString(kBuildStampKey, stamp);
  prefs.end();
}

} // namespace

bool RtcService::begin(TwoWire &wire, bool setOnLostPower) {
  wire_ = &wire;
  setOnLostPower_ = setOnLostPower;

  if (initialized_) {
    return true;
  }
  if (!rtc_.begin(wire_)) {
    return false;
  }

  const DateTime buildTime(F(__DATE__), F(__TIME__));
  const String buildStamp = firmwareBuildStamp(__DATE__, __TIME__);
  const bool lostPower = rtc_.lostPower();
  const bool newFirmwareBuild = !storedBuildStampMatches(buildStamp);
  const bool seedFromBuildTime =
      newFirmwareBuild || (setOnLostPower_ && lostPower);

  if (seedFromBuildTime) {
    rtc_.adjust(buildTime);
    if (newFirmwareBuild) {
      storeBuildStamp(buildStamp);
    }
  }

  // Best-effort: keep ESP system clock aligned to RTC when RTC time is valid.
  syncEspClockFromDateTime(rtc_.now());

  initialized_ = true;
  return true;
}

RtcReading RtcService::readSample() {
  RtcReading out;
  if (!wire_) {
    return out;
  }
  if (!initialized_ && !begin(*wire_, setOnLostPower_)) {
    out.status = ServiceStatus::NotFound;
    return out;
  }

  out.hadLostPower = rtc_.lostPower();
  out.now = rtc_.now();
  if (!out.now.isValid()) {
    out.status = ServiceStatus::InvalidData;
    return out;
  }

  out.temperatureC = rtc_.getTemperature();
  out.status = ServiceStatus::Ok;
  return out;
}

bool RtcService::adjust(const DateTime &dt) {
  if (!initialized_) {
    return false;
  }
  rtc_.adjust(dt);
  syncEspClockFromDateTime(dt);
  return true;
}

bool RtcService::enableAlarmInterrupt() {
  if (!wire_ || !initialized_) {
    return false;
  }
  uint8_t control = 0;
  if (!ds3231ReadControlByte(wire_, control)) {
    return false;
  }
  control = static_cast<uint8_t>((control & 0xE3U) | 0x04U); // INTCN=1
  if (!ds3231WriteControlByte(wire_, control)) {
    return false;
  }
  uint8_t status = 0;
  if (!ds3231ReadStatusByte(wire_, status)) {
    return false;
  }
  status = static_cast<uint8_t>(status & static_cast<uint8_t>(~0x03U));
  return ds3231WriteStatusByte(wire_, status);
}

bool RtcService::armAlarm1AfterSeconds(uint32_t seconds) {
  if (!initialized_) {
    return false;
  }
  const DateTime now = rtc_.now();
  if (!now.isValid()) {
    return false;
  }
  const DateTime alarm = now + TimeSpan(seconds);
  return rtc_.setAlarm1(alarm, DS3231_A1_Date);
}

bool RtcService::alarm1Fired() {
  if (!initialized_) {
    return false;
  }
  return rtc_.alarmFired(1);
}

void RtcService::clearAlarm1() {
  if (!initialized_) {
    return;
  }
  rtc_.clearAlarm(1);
  if (wire_ == nullptr) {
    return;
  }
  uint8_t status = 0;
  if (!ds3231ReadStatusByte(wire_, status)) {
    return;
  }
  status = static_cast<uint8_t>(status & static_cast<uint8_t>(~0x01U));
  (void)ds3231WriteStatusByte(wire_, status);
}

} // namespace tumbly
