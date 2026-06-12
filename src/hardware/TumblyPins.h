#pragma once

#include <Arduino.h>

namespace tumbly
{

    constexpr uint8_t PIN_BOOT_BUTTON = 0;
    constexpr uint8_t PIN_AUX_GPIO0 = 1;
    constexpr uint8_t PIN_AUX_GPIO1 = 2;
    constexpr uint8_t PIN_I2C_SDA = 3;
    constexpr uint8_t PIN_I2C_SCL = 4;
    constexpr uint8_t PIN_TOUCH = 5;
    constexpr uint8_t PIN_5V_EN = 6;  // active HIGH; 470k pulldown when off
    constexpr uint8_t PIN_I2C_EN = 7; // active LOW (~ON); HW pull-up
    constexpr uint8_t PIN_SD_DET = 8; // active LOW; needs internal pullup
    constexpr uint8_t PIN_SRVO = 9;
    constexpr uint8_t PIN_SRV_EN = 10; // active LOW (~OE on SN74AHCT1G125)
    constexpr uint8_t PIN_FBK0 = 11;   // analog servo feedback
    // GPIO12: not connected
    constexpr uint8_t PIN_BNT_0 = 13; // active LOW; needs internal pullup
    constexpr uint8_t PIN_BNT_1 = 14; // active LOW; needs internal pullup
    constexpr uint8_t PIN_BNT_2 = 17; // active LOW; needs internal pullup
    constexpr uint8_t PIN_FUEL_ALERT = 18; // ~FUEL_ALERT from MAX17048; open-drain active LOW
    constexpr uint8_t PIN_RTC_INT = 21;    // ~RTC_INT from DS3231; open-drain active LOW
    // PIN_USB_SENSE: ~PGOOD from BQ24075 — active LOW when input power is good (USB or charger).
    // Kept this name for CSV/API compatibility with the Raven library.
    constexpr uint8_t PIN_USB_SENSE = 34;
    constexpr uint8_t PIN_SPI_MOSI = 35;
    constexpr uint8_t PIN_SPI_SCK = 36;
    constexpr uint8_t PIN_SPI_MISO = 37;
    constexpr uint8_t PIN_UART_RX = 38; // test point only
    constexpr uint8_t PIN_UART_TX = 39; // test point only
    constexpr uint8_t PIN_LED_FRONT = 40;
    constexpr uint8_t PIN_TDXO = 43; // test point only
    constexpr uint8_t PIN_SD_EN = 45; // active LOW (~ON); HW pull-up
    constexpr uint8_t PIN_SD_CS = 46;
    constexpr uint8_t PIN_LED_BACK = 47;

    constexpr uint8_t kButtonCount = 3;
    constexpr uint8_t kButtonPins[kButtonCount] = {PIN_BNT_0, PIN_BNT_1, PIN_BNT_2};

    constexpr uint32_t DEFAULT_I2C_CLOCK_HZ = 100000;
    constexpr uint32_t DEFAULT_SD_SPI_CLOCK_HZ = 1000000; // matches hublink

} // namespace tumbly
