#pragma once

#include "FlightCapApp.h"
#include "TelemetryAdv.h"
#include <Arduino.h>
#include <esp_sleep.h>

static inline const char *appStateName(AppState state) {
  switch (state) {
  case AppState::BootSplash:
    return "BootSplash";
  case AppState::MainMenu:
    return "MainMenu";
  case AppState::LoggingStarting:
    return "LoggingStarting";
  case AppState::LoggingSleepLoop:
    return "LoggingSleepLoop";
  case AppState::LoggingPeek:
    return "LoggingPeek";
  case AppState::ManagePairsMenu:
    return "ManagePairsMenu";
  case AppState::PairActiveCaps:
    return "PairActiveCaps";
  case AppState::RemoveSingleList:
    return "RemoveSingleList";
  case AppState::RemoveAllConfirm:
    return "RemoveAllConfirm";
  case AppState::RemoveAllPairs:
    return "RemoveAllPairs";
  case AppState::AdvancedMenu:
    return "AdvancedMenu";
  case AppState::ActiveScanner:
    return "ActiveScanner";
  default:
    return "Unknown";
  }
}

static inline void flightCapLog(const __FlashStringHelper *msg) {
  Serial.println(msg);
}

static inline void flightCapLogState(AppState state) {
  Serial.print(F("FlightCap: state -> "));
  Serial.println(appStateName(state));
}

static inline void flightCapLogWake(esp_sleep_wakeup_cause_t cause) {
  Serial.print(F("FlightCap: wake cause="));
  Serial.println(static_cast<int>(cause));
}

static inline void flightCapLogPairLine(const char *msg) {
  Serial.print(F("FlightCap: "));
  Serial.println(msg);
  Serial.flush();
}

static inline void flightCapLogDeviceAddr(const char *label, const uint8_t deviceAddr[6]) {
  Serial.printf("FlightCap:%s%02X:%02X:%02X:%02X:%02X:%02X\n", label, deviceAddr[0],
                deviceAddr[1], deviceAddr[2], deviceAddr[3], deviceAddr[4], deviceAddr[5]);
  Serial.flush();
}

static inline void flightCapLogTelemetryAdv(const char *label, const TelemetryAdv &adv) {
  if (telemetryVbattValid(adv)) {
    Serial.printf(
        "FlightCap:%s dev=%02X:%02X:%02X:%02X:%02X:%02X seq=%u int=%u flags=0x%02X dist=%d vbatt=%u mV\n",
        label, adv.device_addr[0], adv.device_addr[1], adv.device_addr[2], adv.device_addr[3],
        adv.device_addr[4], adv.device_addr[5], adv.seq, adv.interactions, adv.flags,
        adv.distance_mm, adv.vbatt_mv);
  } else {
    Serial.printf(
        "FlightCap:%s dev=%02X:%02X:%02X:%02X:%02X:%02X seq=%u int=%u flags=0x%02X dist=%d\n",
        label, adv.device_addr[0], adv.device_addr[1], adv.device_addr[2], adv.device_addr[3],
        adv.device_addr[4], adv.device_addr[5], adv.seq, adv.interactions, adv.flags,
        adv.distance_mm);
  }
  Serial.flush();
}
