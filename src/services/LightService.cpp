#include "LightService.h"

namespace tumbly {

bool LightService::begin(TwoWire &wire) {
  wire_ = &wire;
  if (initialized_) {
    return true;
  }
  if (!sensor_.begin(wire_)) {
    return false;
  }

  // High-sensitivity defaults for low light; power save and IRQ thresholds are
  // off so each readSample() gets a full integration window while enabled.
  sensor_.setGain(VEML7700_GAIN_2);
  sensor_.setIntegrationTime(VEML7700_IT_800MS);
  sensor_.interruptEnable(false);
  sensor_.powerSaveEnable(false);
  sensor_.enable(false);

  initialized_ = true;
  return true;
}

LightReading LightService::readSample() {
  LightReading out;
  if (!wire_) {
    return out;
  }
  if (!initialized_ && !begin(*wire_)) {
    out.status = ServiceStatus::NotFound;
    return out;
  }

  sensor_.enable(true);
  // Auto gain/IT handles both dim and bright scenes; readLux() waits for the
  // active integration window (do not pair readALS() with VEML_LUX_*_NOWAIT).
  out.lux = sensor_.readLux(VEML_LUX_AUTO);
  out.als = sensor_.readALS(false);
  out.white = sensor_.readWhite(false);
  const uint16_t irq = sensor_.interruptStatus();
  sensor_.enable(false);

  out.lowThreshold = (irq & VEML7700_INTERRUPT_LOW) != 0;
  out.highThreshold = (irq & VEML7700_INTERRUPT_HIGH) != 0;
  out.status = ServiceStatus::Ok;
  return out;
}

} // namespace tumbly
