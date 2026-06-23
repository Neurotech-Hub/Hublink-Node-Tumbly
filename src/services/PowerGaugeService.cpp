#include "PowerGaugeService.h"
#include <cmath>

namespace tumbly {

namespace {

constexpr float kMinPlausibleCellV = 2.0f;
constexpr float kMaxPlausibleCellV = 5.5f;
constexpr float kMaxStableReadDeltaV = 0.08f;
constexpr uint8_t kMaxPlausibleChipId = 0x1F;

bool chipIdPlausible(uint8_t id) { return id <= kMaxPlausibleChipId; }

} // namespace

bool PowerGaugeService::begin(TwoWire &wire) {
  wire_ = &wire;
  if (initialized_) {
    return true;
  }
  if (!sensor_.begin(wire_)) {
    return false;
  }
  delay(200);
  if (!sensor_.isDeviceReady()) {
    return false;
  }
  const uint8_t id = sensor_.getChipID();
  if (!chipIdPlausible(id)) {
    return false;
  }
  chipId_ = id;
  // Adafruit::begin() leaves enableSleep(false). Allow CONFIG sleep (~1 µA) between reads; I2C
  // transactions use sleep(false) first. Hibernate reduces internal gauge activity when idle.
  sensor_.enableSleep(true);
  sensor_.hibernate();
  sensor_.sleep(true);
  initialized_ = true;
  return true;
}

void PowerGaugeService::setAlertVoltages(float minCellV, float maxCellV) {
  if (!initialized_) {
    return;
  }
  sensor_.setAlertVoltages(minCellV, maxCellV);
}

BatteryReading PowerGaugeService::readSample() {
  BatteryReading out;
  if (!wire_) {
    return out;
  }
  if (!initialized_ && !begin(*wire_)) {
    out.status = ServiceStatus::NotFound;
    return out;
  }

  sensor_.sleep(false);
  sensor_.wake();
  delay(3);

  if (!sensor_.isDeviceReady()) {
    initialized_ = false;
    sensor_.hibernate();
    sensor_.sleep(true);
    out.status = ServiceStatus::NotFound;
    return out;
  }

  const uint8_t id = sensor_.getChipID();
  if (!chipIdPlausible(id) || (initialized_ && id != chipId_)) {
    initialized_ = false;
    sensor_.hibernate();
    sensor_.sleep(true);
    out.status = ServiceStatus::InvalidData;
    return out;
  }

  const float v1 = sensor_.cellVoltage();
  delay(10);
  const float v2 = sensor_.cellVoltage();
  const float soc = sensor_.cellPercent();
  sensor_.hibernate();
  sensor_.sleep(true);

  if (isnan(v1) || isnan(v2)) {
    initialized_ = false;
    out.status = ServiceStatus::ReadFailed;
    return out;
  }

  if (fabsf(v1 - v2) > kMaxStableReadDeltaV) {
    initialized_ = false;
    out.status = ServiceStatus::InvalidData;
    return out;
  }

  const float voltage = (v1 + v2) * 0.5f;

  // Guard against floating VCELL pins (common when no battery is attached).
  if (voltage < kMinPlausibleCellV || voltage > kMaxPlausibleCellV) {
    out.status = ServiceStatus::InvalidData;
    return out;
  }

  out.status = ServiceStatus::Ok;
  out.voltageV = voltage;
  if (!isnan(soc) && soc >= 0.0f) {
    out.stateOfChargePct = min(soc, 100.0f);
  }
  out.hasCellReading = true;
  return out;
}

} // namespace tumbly
