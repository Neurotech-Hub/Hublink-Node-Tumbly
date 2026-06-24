#pragma once

#include "../hardware/TumblyPins.h"
#include "ServiceTypes.h"
#include <Arduino.h>

namespace tumbly {

class ButtonService {
public:
  using Callback = void (*)(uint8_t index, void *ctx);

  /// Default debounce window between two accepted edges, in milliseconds.
  static constexpr uint32_t kDefaultDebounceMs = 100;

  /// Configure pull-ups and attach FALLING-edge interrupts for all buttons.
  bool begin(uint32_t debounceMs = kDefaultDebounceMs);

  /// Detach button interrupts (e.g. before light sleep using GPIO wake instead).
  void end();

  /// Active-LOW: returns true when the button is currently held.
  bool isPressed(uint8_t index) const;

  /// True once per press-and-release cycle: debounced falling edge while armed,
  /// then re-arms only after the button reads released. Auto-clears the edge flag.
  bool wasPressed(uint8_t index);

  /// Discard pending edges and re-arm only for buttons that are currently released.
  void flushPending();

  /// Register a callback fired from the ISR after the debounce window has elapsed.
  /// Callbacks run in interrupt context; keep them short.
  void attachCallback(uint8_t index, Callback cb, void *ctx = nullptr);

  static constexpr uint8_t count() { return kButtonCount; }

private:
  struct State {
    volatile uint32_t lastEdgeMs = 0;
    volatile bool edgeFlag = false;
    volatile bool armed = true;
    Callback callback = nullptr;
    void *callbackCtx = nullptr;
  };

  static void IRAM_ATTR isrTrampoline(void *arg);

  bool initialized_ = false;
  uint32_t debounceMs_ = kDefaultDebounceMs;
  State states_[kButtonCount];
};

} // namespace tumbly
