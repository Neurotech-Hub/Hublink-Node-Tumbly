#pragma once

#include "../hardware/TumblyPins.h"
#include "ServiceTypes.h"
#include <Arduino.h>

namespace tumbly {

class ButtonService {
public:
  using Callback = void (*)(uint8_t index, void *ctx);

  /// Default debounce window between two accepted edges, in milliseconds.
  static constexpr uint32_t kDefaultDebounceMs = 50;

  /// Configure pull-ups and attach FALLING-edge interrupts for all buttons.
  bool begin(uint32_t debounceMs = kDefaultDebounceMs);

  /// Active-LOW: returns true when the button is currently held.
  bool isPressed(uint8_t index) const;

  /// True if a debounced press edge has fired since the last call; auto-clears.
  bool wasPressed(uint8_t index);

  /// Register a callback fired from the ISR after the debounce window has elapsed.
  /// Callbacks run in interrupt context; keep them short.
  void attachCallback(uint8_t index, Callback cb, void *ctx = nullptr);

  static constexpr uint8_t count() { return kButtonCount; }

private:
  struct State {
    volatile uint32_t lastEdgeMs = 0;
    volatile bool edgeFlag = false;
    Callback callback = nullptr;
    void *callbackCtx = nullptr;
  };

  static void IRAM_ATTR isrTrampoline(void *arg);

  bool initialized_ = false;
  uint32_t debounceMs_ = kDefaultDebounceMs;
  State states_[kButtonCount];
};

} // namespace tumbly
