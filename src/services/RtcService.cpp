#include "RtcService.h"
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>

namespace tumbly {

namespace {

constexpr char kBuildStampNamespace[] = "tumbly_rtc";
constexpr char kBuildStampKey[] = "build";

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

} // namespace tumbly
