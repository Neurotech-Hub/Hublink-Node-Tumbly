#include "HublinkNode.h"
#include <esp32-hal-cpu.h>
#include <esp_log.h>

namespace tumbly
{

  bool HublinkNode::setMcuClockMhz(uint32_t mhz) { return setCpuFrequencyMhz(mhz); }

  uint32_t HublinkNode::mcuClockMhz() { return getCpuFrequencyMhz(); }

  bool HublinkNode::beginHardware(uint32_t mcuClockMhz)
  {
    (void)setMcuClockMhz(mcuClockMhz);

    pinMode(PIN_AUX_GPIO0, INPUT);
    pinMode(PIN_AUX_GPIO1, INPUT);
    pinMode(PIN_TOUCH, INPUT);

    pinMode(PIN_5V_EN, OUTPUT);
    set5VPowerEnabled(false);

    pinMode(PIN_I2C_EN, OUTPUT);
    setI2CPowerEnabled(true);

    pinMode(PIN_SD_DET, INPUT_PULLUP);

    pinMode(PIN_SRV_EN, OUTPUT);
    digitalWrite(PIN_SRV_EN, HIGH); // servo rail off (active LOW gate)
    pinMode(PIN_FBK0, INPUT);

    pinMode(PIN_FUEL_ALERT, INPUT_PULLUP);
    pinMode(PIN_RTC_INT, INPUT_PULLUP);
    pinMode(PIN_USB_SENSE, INPUT_PULLUP);

    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

    pinMode(PIN_LED_FRONT, OUTPUT);
    pinMode(PIN_LED_BACK, OUTPUT);
    setStatusLeds(false);

    pinMode(PIN_SPI_MOSI, INPUT);
    pinMode(PIN_SPI_SCK, INPUT);
    pinMode(PIN_SPI_MISO, INPUT);

    pinMode(PIN_UART_RX, INPUT);
    pinMode(PIN_UART_TX, OUTPUT);
    digitalWrite(PIN_UART_TX, LOW);

    pinMode(PIN_TDXO, OUTPUT);
    digitalWrite(PIN_TDXO, LOW);

    pinMode(PIN_SD_EN, OUTPUT);
    digitalWrite(PIN_SD_EN, HIGH);
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);

    buttons_.begin();
    servo_.begin();

    hardwareInitialized_ = true;
    return true;
  }

  bool HublinkNode::beginI2C(uint32_t clockHz)
  {
    if (!hardwareInitialized_)
    {
      beginHardware();
    }
    // Optional peripherals may NACK when not populated; keep logs quiet by default.
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(clockHz);
    return true;
  }

  void HublinkNode::setI2CPowerEnabled(bool enabled)
  {
    // Active-low enable (~ON).
    digitalWrite(PIN_I2C_EN, enabled ? LOW : HIGH);
  }

  bool HublinkNode::isI2CPowerEnabled() const
  {
    return digitalRead(PIN_I2C_EN) == LOW;
  }

  void HublinkNode::set5VPowerEnabled(bool enabled)
  {
    digitalWrite(PIN_5V_EN, enabled ? HIGH : LOW);
  }

  bool HublinkNode::is5VPowerEnabled() const
  {
    return digitalRead(PIN_5V_EN) == HIGH;
  }

  // TODO: verify polarity on hardware. Carried over from Raven (== HIGH means
  // "magnet present"), but the APS11753K MDA L X-3PL1 datasheet (suffix "-3PL1",
  // "L" = output Low when B > BOP) says the sensor's output is LOW when a magnet
  // is present. Either Raven's logic was wrong or Tumbly uses a different
  // physical variant; confirm on the board before relying on this value.
  bool HublinkNode::readMagnet() const { return digitalRead(PIN_AUX_GPIO0) == HIGH; }

  bool HublinkNode::readUsbSense() const
  {
    return digitalRead(PIN_USB_SENSE) == LOW;
  }

  bool HublinkNode::readSdDetect() const
  {
    return digitalRead(PIN_SD_DET) == LOW;
  }

  bool HublinkNode::readFuelAlert() const
  {
    return digitalRead(PIN_FUEL_ALERT) == LOW;
  }

  bool HublinkNode::readRtcInt() const
  {
    return digitalRead(PIN_RTC_INT) == LOW;
  }

  uint16_t HublinkNode::readTouchRaw() const
  {
    return static_cast<uint16_t>(analogRead(PIN_TOUCH));
  }

  void HublinkNode::setStatusLeds(bool on)
  {
    const int level = on ? HIGH : LOW;
    digitalWrite(PIN_LED_FRONT, level);
    digitalWrite(PIN_LED_BACK, level);
  }

  esp_sleep_wakeup_cause_t HublinkNode::wakeupCause() const
  {
    return esp_sleep_get_wakeup_cause();
  }

  bool HublinkNode::isTimerWake() const
  {
    return wakeupCause() == ESP_SLEEP_WAKEUP_TIMER;
  }

} // namespace tumbly
