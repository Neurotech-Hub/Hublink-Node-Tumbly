#include "ButtonService.h"

namespace tumbly {
namespace {

struct IsrArg {
  ButtonService *service;
  uint8_t index;
};

IsrArg gIsrArgs[kButtonCount];

} // namespace

void IRAM_ATTR ButtonService::isrTrampoline(void *arg) {
  IsrArg *ia = static_cast<IsrArg *>(arg);
  ButtonService *self = ia->service;
  const uint8_t index = ia->index;
  State &s = self->states_[index];

  if (!s.armed) {
    return;
  }

  const uint32_t now = millis();
  if (s.lastEdgeMs != 0 && (now - s.lastEdgeMs) < self->debounceMs_) {
    return;
  }
  s.lastEdgeMs = now;
  s.edgeFlag = true;
  if (s.callback) {
    s.callback(index, s.callbackCtx);
  }
}

bool ButtonService::begin(uint32_t debounceMs) {
  if (initialized_) {
    return true;
  }
  debounceMs_ = debounceMs;
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    pinMode(kButtonPins[i], INPUT_PULLUP);
    states_[i].lastEdgeMs = 0;
    states_[i].edgeFlag = false;
    states_[i].armed = (digitalRead(kButtonPins[i]) != LOW);
    gIsrArgs[i].service = this;
    gIsrArgs[i].index = i;
    attachInterruptArg(digitalPinToInterrupt(kButtonPins[i]), isrTrampoline,
                       &gIsrArgs[i], FALLING);
  }
  initialized_ = true;
  return true;
}

void ButtonService::end() {
  if (!initialized_) {
    return;
  }
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    detachInterrupt(digitalPinToInterrupt(kButtonPins[i]));
  }
  initialized_ = false;
}

bool ButtonService::isPressed(uint8_t index) const {
  if (index >= kButtonCount) {
    return false;
  }
  return digitalRead(kButtonPins[index]) == LOW;
}

bool ButtonService::wasPressed(uint8_t index) {
  if (index >= kButtonCount) {
    return false;
  }

  State &s = states_[index];
  if (!s.armed && !isPressed(index)) {
    noInterrupts();
    s.armed = true;
    interrupts();
  }

  noInterrupts();
  const bool pressed = s.edgeFlag && s.armed;
  s.edgeFlag = false;
  if (pressed) {
    s.armed = false;
  }
  interrupts();
  return pressed;
}

void ButtonService::flushPending() {
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    noInterrupts();
    states_[i].edgeFlag = false;
    states_[i].armed = (digitalRead(kButtonPins[i]) != LOW);
    interrupts();
  }
}

void ButtonService::attachCallback(uint8_t index, Callback cb, void *ctx) {
  if (index >= kButtonCount) {
    return;
  }
  noInterrupts();
  states_[index].callback = cb;
  states_[index].callbackCtx = ctx;
  interrupts();
}

} // namespace tumbly
