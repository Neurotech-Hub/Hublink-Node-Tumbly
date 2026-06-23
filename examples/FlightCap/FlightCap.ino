// FlightCap — menu app: pair nRF52833 caps, log telemetry to SD in light sleep.
// Build: ESP32S3 Dev Module, Tools → Bluetooth → NimBLE, NimBLE-Arduino library.

#include <HublinkNodeTumbly.h>
#include <Wire.h>
#include <cstring>
#include <nvs_flash.h>

#include "FlightCapApp.h"
#include "FlightCapBle.h"
#include "FlightCapConfig.h"
#include "FlightCapLog.h"
#include "FlightCapLogging.h"
#include "FlightCapPairs.h"
#include "FlightCapUi.h"

tumbly::HublinkNode node;
tumbly::DataLoggerHelper logger(node);

static AppState g_appState = AppState::BootSplash;
static uint32_t g_splashStartMs = 0;
static FlightCapPairList g_pairs;
static FlightCapLoggingContext g_logCtx;
static uint8_t g_removeIndex = 0;
static char g_lastAddedId[13] = "";
static uint32_t g_messageUntilMs = 0;

static void setAppState(AppState state) {
  if (g_appState == state) {
    return;
  }
  g_appState = state;
  flightCapLogState(state);
}

static void initNvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

static bool isBootPressed() {
  return digitalRead(tumbly::PIN_BOOT_BUTTON) == LOW;
}

static bool onPairAdvertAdd(const uint8_t addr[6], char addedId[13]) {
  if (flightCapPairsTryAddFromAddr(node, g_pairs, addr, addedId)) {
    strncpy(g_lastAddedId, addedId, 13);
    g_lastAddedId[12] = '\0';
    Serial.print(F("FlightCap: paired "));
    Serial.println(g_lastAddedId);
    return true;
  }
  return false;
}

static void reloadPairsFromSd() {
  (void)flightCapPairsLoad(node, g_pairs);
  flightCapBleSetPairList(&g_pairs);
}

static void enterPairActiveCaps() {
  reloadPairsFromSd();
  g_lastAddedId[0] = '\0';
  flightCapBleSetPairAddCallback(onPairAdvertAdd);
  flightCapBleSetMode(FlightCapBleMode::PairActive);
  flightCapBleStartContinuousScan();
  setAppState(AppState::PairActiveCaps);
}

static void handleMainMenu() {
  flightCapUiRenderMainMenu(node, g_pairs.count);
  if (node.buttons().wasPressed(0)) {
    setAppState(AppState::LoggingStarting);
  } else if (node.buttons().wasPressed(1)) {
    reloadPairsFromSd();
    setAppState(AppState::ManagePairsMenu);
  } else if (node.buttons().wasPressed(2)) {
    setAppState(AppState::SettingsStub);
  }
}

static void handleManagePairsMenu() {
  flightCapUiRenderManagePairsMenu(node, g_pairs.count);
  if (isBootPressed()) {
    setAppState(AppState::MainMenu);
    return;
  }
  if (node.buttons().wasPressed(0)) {
    enterPairActiveCaps();
  } else if (node.buttons().wasPressed(1)) {
    reloadPairsFromSd();
    g_removeIndex = 0;
    setAppState(AppState::RemoveSingleList);
  } else if (node.buttons().wasPressed(2)) {
    flightCapPairsRemoveAll(g_pairs);
    (void)flightCapPairsSave(node, g_pairs);
    reloadPairsFromSd();
    flightCapUiRenderMessage(node, g_pairs.count, "All pairs removed", nullptr, nullptr, true);
    g_messageUntilMs = millis() + 1500;
    setAppState(AppState::RemoveAllPairs);
  }
}

static void handlePairActiveCaps() {
  flightCapUiRenderPairActive(node, g_lastAddedId[0] ? g_lastAddedId : nullptr, g_pairs.count);
  if (isBootPressed()) {
    flightCapBleSetPairAddCallback(nullptr);
    reloadPairsFromSd();
    flightCapBleSetMode(FlightCapBleMode::IdleMenu);
    setAppState(AppState::ManagePairsMenu);
  }
}

static void handleRemoveSingleList() {
  if (g_pairs.count == 0) {
    reloadPairsFromSd();
  }
  if (g_removeIndex >= g_pairs.count && g_pairs.count > 0) {
    g_removeIndex = static_cast<uint8_t>(g_pairs.count - 1);
  }

  flightCapUiRenderRemoveSingle(node, g_pairs, g_removeIndex);

  if (isBootPressed()) {
    setAppState(AppState::ManagePairsMenu);
    return;
  }
  if (node.buttons().wasPressed(0) && g_removeIndex > 0) {
    --g_removeIndex;
  } else if (node.buttons().wasPressed(2) && g_removeIndex + 1 < g_pairs.count) {
    ++g_removeIndex;
  } else if (node.buttons().wasPressed(1) && g_pairs.count > 0) {
    (void)flightCapPairsRemoveAt(node, g_pairs, g_removeIndex);
    reloadPairsFromSd();
    if (g_removeIndex >= g_pairs.count && g_pairs.count > 0) {
      g_removeIndex = static_cast<uint8_t>(g_pairs.count - 1);
    }
  }
}

static void handleRemoveAllPairs() {
  if (millis() >= g_messageUntilMs) {
    setAppState(AppState::ManagePairsMenu);
  } else if (isBootPressed()) {
    setAppState(AppState::ManagePairsMenu);
  }
}

static void handleSettingsStub() {
  flightCapUiRenderSettingsStub(node, g_pairs.count);
  if (isBootPressed()) {
    setAppState(AppState::MainMenu);
  }
}

static void handleLoggingStarting() {
  reloadPairsFromSd();
  flightCapUiRenderMessage(node, g_pairs.count, "Starting logging...", nullptr, nullptr, false);
  (void)flightCapLoggingPrepare(node, logger, g_logCtx);
  delay(1000);
  g_appState = flightCapLoggingEnterLoop(node, logger, g_logCtx);
  flightCapLogState(g_appState);
}

void setup() {
  Serial.begin(115200);
  const uint32_t serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000) {
    delay(10);
  }

  node.beginHardware();

  node.setI2CPowerEnabled(true);
  delay(100);
  node.beginI2C();
  delay(100);

  (void)node.rtc().begin();
  (void)node.powerGauge().begin();
  (void)logger.begin();

  flightCapLog(F("FlightCap: setup"));

  if (node.screen().begin()) {
    flightCapUiRenderSplash(node);
    g_splashStartMs = millis();
  } else {
    flightCapLog(F("FlightCap: screen init failed"));
    g_appState = AppState::MainMenu;
  }

  initNvs();
  flightCapBleInit();
  flightCapBleStartContinuousScan();
  reloadPairsFromSd();
  Serial.print(F("FlightCap: pairs loaded="));
  Serial.println(g_pairs.count);
}

void loop() {
  if (g_appState == AppState::BootSplash) {
    if (millis() - g_splashStartMs >= kBootSplashMs) {
      setAppState(AppState::MainMenu);
    }
    return;
  }

  switch (g_appState) {
  case AppState::MainMenu:
    handleMainMenu();
    break;
  case AppState::ManagePairsMenu:
    handleManagePairsMenu();
    break;
  case AppState::PairActiveCaps:
    handlePairActiveCaps();
    break;
  case AppState::RemoveSingleList:
    handleRemoveSingleList();
    break;
  case AppState::RemoveAllPairs:
    handleRemoveAllPairs();
    break;
  case AppState::SettingsStub:
    handleSettingsStub();
    break;
  case AppState::LoggingStarting:
    handleLoggingStarting();
    break;
  default:
    setAppState(AppState::MainMenu);
    break;
  }

  delay(50);
}
