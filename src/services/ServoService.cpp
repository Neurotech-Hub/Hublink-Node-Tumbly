#include "ServoService.h"

namespace tumbly {

bool ServoService::begin() {
  if (initialized_) {
    return true;
  }
  pinMode(PIN_SRV_EN, OUTPUT);
  setPowerEnabled(false);
  pinMode(PIN_FBK0, INPUT);
  initialized_ = true;
  return true;
}

void ServoService::setPowerEnabled(bool enabled) {
  // Active LOW: pulling SRV_EN low asserts ~OE on the SN74AHCT1G125 buffer.
  digitalWrite(PIN_SRV_EN, enabled ? LOW : HIGH);
  powerEnabled_ = enabled;
}

bool ServoService::attach(uint16_t minUs, uint16_t maxUs) {
  if (!initialized_ && !begin()) {
    return false;
  }
  if (attached_) {
    return true;
  }
  setPowerEnabled(true);
  // Brief settle time so the rail is up before the first pulse.
  delay(2);
  const int channel = servo_.attach(PIN_SRVO, minUs, maxUs);
  attached_ = channel >= 0;
  if (!attached_) {
    setPowerEnabled(false);
  }
  return attached_;
}

void ServoService::detach() {
  if (attached_) {
    servo_.detach();
    attached_ = false;
  }
  setPowerEnabled(false);
}

void ServoService::writeMicroseconds(uint16_t us) {
  if (!attached_) {
    return;
  }
  servo_.writeMicroseconds(us);
}

void ServoService::writeDegrees(int degrees) {
  if (!attached_) {
    return;
  }
  servo_.write(degrees);
}

uint16_t ServoService::readFeedbackRaw() {
  return static_cast<uint16_t>(analogRead(PIN_FBK0));
}

} // namespace tumbly
