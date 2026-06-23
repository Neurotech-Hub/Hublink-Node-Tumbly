#pragma once

#include "FlightCapApp.h"
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
  case AppState::RemoveAllPairs:
    return "RemoveAllPairs";
  case AppState::SettingsStub:
    return "SettingsStub";
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
