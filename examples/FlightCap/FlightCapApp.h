#pragma once

#include <stdint.h>

enum class AppState : uint8_t {
  BootSplash,
  MainMenu,
  LoggingStarting,
  ManagePairsMenu,
  PairActiveCaps,
  RemoveSingleList,
  RemoveAllConfirm,
  RemoveAllPairs,
  AdvancedMenu,
  ActiveScanner,
};

constexpr char kFlightCapFirmwareVersion[] = "1.0";
constexpr uint8_t kMaxPairedDevices = 8;
constexpr uint32_t kDefaultLogIntervalSec = 60;
constexpr uint32_t kDefaultPairIntervalSec = 10;
constexpr uint32_t kBootSplashMs = 1000;
constexpr uint32_t kPairScanWindowMs = 10000;
