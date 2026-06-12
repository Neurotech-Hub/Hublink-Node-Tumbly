#include "ScreenService.h"

namespace tumbly {

bool ScreenService::begin(TwoWire &wire, uint8_t i2cAddress) {
  wire_ = &wire;
  address_ = i2cAddress;
  if (initialized_) {
    return true;
  }
  if (!display_) {
    // -1 reset pin: the 4-pin HS96L03W2C03 has no dedicated reset line.
    display_ = new Adafruit_SSD1306(kWidth, kHeight, wire_, -1);
  }
  // SSD1306_SWITCHCAPVCC enables the internal charge pump that the datasheet
  // describes as the standard power-up path for this 4-pin module.
  if (!display_->begin(SSD1306_SWITCHCAPVCC, address_)) {
    return false;
  }
  display_->clearDisplay();
  display_->setTextColor(SSD1306_WHITE);
  display_->setTextSize(1);
  display_->setCursor(0, 0);
  display_->display();
  initialized_ = true;
  return true;
}

void ScreenService::end() {
  if (display_) {
    delete display_;
    display_ = nullptr;
  }
  initialized_ = false;
}

Adafruit_SSD1306 &ScreenService::display() {
  // Caller is expected to gate on isInitialized(); allocating on demand here
  // would defeat the lazy/explicit lifecycle and hide failure modes.
  return *display_;
}

void ScreenService::clear() {
  if (!initialized_) {
    return;
  }
  display_->clearDisplay();
  display_->setCursor(0, 0);
}

void ScreenService::show() {
  if (!initialized_) {
    return;
  }
  display_->display();
}

void ScreenService::printLines(const char *line0, const char *line1,
                               const char *line2, const char *line3) {
  if (!initialized_) {
    return;
  }
  clear();
  const char *lines[] = {line0, line1, line2, line3};
  for (uint8_t i = 0; i < 4; ++i) {
    if (lines[i] == nullptr) {
      continue;
    }
    display_->setCursor(0, i * 8);
    display_->print(lines[i]);
  }
  show();
}

} // namespace tumbly
