#pragma once

#include "ServiceTypes.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

namespace tumbly {

/// Driver wrapper for the on-board HS96L03W2C03 128x64 monochrome OLED
/// (SSD1306 controller, I2C, default 7-bit address 0x3C). The panel exposes
/// only GND/VCC/SCL/SDA, so there is no reset line and no separate D/C pin.
/// Power is supplied through the I2C isolator and gated by `setI2CPowerEnabled`.
class ScreenService {
public:
  static constexpr uint8_t kDefaultAddress = 0x3C;
  static constexpr int16_t kWidth = 128;
  static constexpr int16_t kHeight = 64;

  /// Allocates the SSD1306 driver (1 KB framebuffer) and runs initialization.
  /// Safe to call repeatedly: returns true if already initialized.
  bool begin(TwoWire &wire = Wire, uint8_t i2cAddress = kDefaultAddress);

  /// Release the framebuffer and mark uninitialized. Useful before deep sleep
  /// or after toggling I2C power so the next `begin()` re-runs SSD1306 init.
  void end();

  bool isInitialized() const { return initialized_; }
  uint8_t address() const { return address_; }

  /// Direct access to the Adafruit GFX surface for text, shapes, bitmaps,
  /// custom fonts, etc. Returns a null reference equivalent only if `begin()`
  /// has not succeeded — callers should check `isInitialized()` first.
  Adafruit_SSD1306 &display();

  /// Convenience wrappers around the most common calls.
  void clear();   ///< Clears the framebuffer and resets the text cursor.
  void show();    ///< Pushes the framebuffer to the panel.

  /// Render up to eight short lines using the default 6x8 GFX font, left-aligned
  /// starting at (0, 0). Pass nullptr to skip a line. Calls `show()` at the end.
  void printLines(const char *line0,
                  const char *line1 = nullptr,
                  const char *line2 = nullptr,
                  const char *line3 = nullptr,
                  const char *line4 = nullptr,
                  const char *line5 = nullptr,
                  const char *line6 = nullptr,
                  const char *line7 = nullptr);

private:
  TwoWire *wire_ = nullptr;
  Adafruit_SSD1306 *display_ = nullptr;
  uint8_t address_ = kDefaultAddress;
  bool initialized_ = false;
};

} // namespace tumbly
