// FlightCap — menu app: pair nRF52833 caps, log telemetry to SD in light sleep.
// Build: ESP32S3 Dev Module, Tools → Bluetooth → NimBLE, NimBLE-Arduino library.

#include <HublinkNodeTumbly.h>
#include <Wire.h>
#include <cstring>
#include <nvs_flash.h>

#include "FlightCapApp.h"
#include "FlightCapBle.h"
#include "FlightCapConfig.h"
#include "FlightCapDiag.h"
#include "FlightCapLog.h"
#include "FlightCapLogging.h"
#include "FlightCapPairs.h"
#include "FlightCapSd.h"
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
static bool g_sdWasReady = true;
static bool g_bootArmed = true;
static uint32_t g_activeScannerHubSampleMs = 0;
static float g_activeScannerLux = 0.0f;
static float g_activeScannerTempC = 0.0f;
static bool g_activeScannerHasLux = false;
static bool g_activeScannerHasTemp = false;

static void flushButtonInput() {
  node.buttons().flushPending();
  g_bootArmed = (digitalRead(tumbly::PIN_BOOT_BUTTON) != LOW);
}

static void setAppState(AppState state) {
  if (g_appState == state) {
    return;
  }
  g_appState = state;
  flushButtonInput();
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

static bool wasBootPressed() {
  const bool held = digitalRead(tumbly::PIN_BOOT_BUTTON) == LOW;
  if (!g_bootArmed && !held) {
    g_bootArmed = true;
  }
  if (g_bootArmed && held) {
    g_bootArmed = false;
    return true;
  }
  return false;
}

static void reloadPairsFromSd() {
  (void)flightCapPairsLoad(node, g_pairs);
  flightCapBleSetPairList(&g_pairs);
}

static bool isMenuSubState(AppState state) {
  switch (state) {
  case AppState::MainMenu:
  case AppState::BootSplash:
    return false;
  default:
    return true;
  }
}

static void exitSubMenuOnSdLoss() {
  if (g_appState == AppState::PairActiveCaps) {
    flightCapBleClearPendingPairAdds();
    flightCapBleSetMode(FlightCapBleMode::IdleMenu);
  }
  if (g_appState == AppState::ActiveScanner) {
    flightCapBleEndActiveScanner();
  }
  if (isMenuSubState(g_appState)) {
    setAppState(AppState::MainMenu);
  }
}

static bool handleSdGate() {
  const bool sdReady = flightCapSdReady(node);
  if (!sdReady) {
    exitSubMenuOnSdLoss();
    flightCapUiRenderInsertSd(node, g_pairs.count);
    g_sdWasReady = false;
    return true;
  }
  if (!g_sdWasReady) {
    reloadPairsFromSd();
  }
  g_sdWasReady = true;
  return false;
}

static void processPendingPairAdds() {
  uint8_t deviceAddr[6];
  TelemetryAdv adv{};
  while (flightCapBleTakePendingPairAdd(deviceAddr, &adv)) {
    if (!telemetryIsPairMode(adv)) {
      Serial.println(F("FlightCap: pair skip (not pair mode at commit)"));
      flightCapLogTelemetryAdv(" ", adv);
      continue;
    }
    char addedId[13];
    if (flightCapPairsTryAddDeviceAddr(node, g_pairs, adv.device_addr, addedId)) {
      strncpy(g_lastAddedId, addedId, 13);
      g_lastAddedId[12] = '\0';
      flightCapBleSetPairList(&g_pairs);
      flightCapBleNotePairSessionCommit(adv);
      Serial.print(F("FlightCap: paired "));
      Serial.println(g_lastAddedId);
      flightCapLogDeviceAddr(" dev ", adv.device_addr);
      flightCapLogTelemetryAdv(" ", adv);
    } else {
      deviceAddrToId(adv.device_addr, addedId);
      Serial.print(F("FlightCap: pair skip duplicate id "));
      Serial.println(addedId);
      flightCapLogDeviceAddr(" dev ", adv.device_addr);
      flightCapLogTelemetryAdv(" ", adv);
    }
  }
}

static void enterPairActiveCaps() {
  reloadPairsFromSd();
  g_lastAddedId[0] = '\0';
  flightCapBleClearPendingPairAdds();
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
    setAppState(AppState::AdvancedMenu);
  }
}

static void handleManagePairsMenu() {
  flightCapUiRenderManagePairsMenu(node, g_pairs.count);
  if (wasBootPressed()) {
    setAppState(AppState::MainMenu);
    return;
  }
  if (node.buttons().wasPressed(0)) {
    enterPairActiveCaps();
  } else if (node.buttons().wasPressed(1)) {
    reloadPairsFromSd();
    g_removeIndex = 0;
    setAppState(AppState::RemoveSingleList);
  } else if (node.buttons().wasPressed(2) && g_pairs.count > 0) {
    setAppState(AppState::RemoveAllConfirm);
  }
}

static void handleRemoveAllConfirm() {
  flightCapUiRenderRemoveAllConfirm(node, g_pairs.count);
  if (wasBootPressed()) {
    setAppState(AppState::ManagePairsMenu);
    return;
  }
  if (node.buttons().wasPressed(1)) {
    flightCapPairsRemoveAll(g_pairs);
    (void)flightCapPairsSave(node, g_pairs);
    reloadPairsFromSd();
    flightCapUiRenderMessage(node, g_pairs.count, "All pairs removed", nullptr, nullptr, true);
    g_messageUntilMs = millis() + 1500;
    setAppState(AppState::RemoveAllPairs);
  }
}

static void handlePairActiveCaps() {
  processPendingPairAdds();
  flightCapUiRenderPairActive(node, g_lastAddedId[0] ? g_lastAddedId : nullptr, g_pairs.count);
  if (wasBootPressed()) {
    flightCapBleClearPendingPairAdds();
    reloadPairsFromSd();
    flightCapBleSetMode(FlightCapBleMode::IdleMenu);
    setAppState(AppState::ManagePairsMenu);
  }
}

static void handleRemoveSingleList() {
  if (g_removeIndex >= g_pairs.count && g_pairs.count > 0) {
    g_removeIndex = static_cast<uint8_t>(g_pairs.count - 1);
  }

  flightCapUiRenderRemoveSingle(node, g_pairs, g_removeIndex);

  if (wasBootPressed()) {
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
  } else if (wasBootPressed()) {
    setAppState(AppState::ManagePairsMenu);
  }
}

static void handleAdvancedMenu() {
  flightCapUiRenderAdvancedMenu(node);
  if (wasBootPressed()) {
    setAppState(AppState::MainMenu);
    return;
  }
  if (node.buttons().wasPressed(0)) {
    g_activeScannerHubSampleMs = 0;
    g_activeScannerHasLux = false;
    g_activeScannerHasTemp = false;
    flightCapBleBeginActiveScanner();
    setAppState(AppState::ActiveScanner);
  }
}

static void sampleActiveScannerHubSensors() {
  const tumbly::CompositeSample sample = logger.captureSample();
  g_activeScannerHasLux = sample.light.status == tumbly::ServiceStatus::Ok;
  g_activeScannerHasTemp = sample.environment.status == tumbly::ServiceStatus::Ok;
  if (g_activeScannerHasLux) {
    g_activeScannerLux = sample.light.lux;
  }
  if (g_activeScannerHasTemp) {
    g_activeScannerTempC = sample.environment.temperatureC;
  }
}

static void handleActiveScanner() {
  if (wasBootPressed()) {
    flightCapBleEndActiveScanner();
    setAppState(AppState::AdvancedMenu);
    return;
  }

  const uint32_t now = millis();
  if (g_activeScannerHubSampleMs == 0 || (now - g_activeScannerHubSampleMs) >= 1000) {
    sampleActiveScannerHubSensors();
    g_activeScannerHubSampleMs = millis();
  }

  ActiveScannerCap cap{};
  flightCapBleGetActiveScannerCap(&cap);
  flightCapUiRenderActiveScanner(node, cap, flightCapBleActiveScannerSecondsSinceData(),
                                 g_activeScannerLux, g_activeScannerTempC, g_activeScannerHasLux,
                                 g_activeScannerHasTemp);
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
  digitalWrite(tumbly::PIN_LED_FRONT, HIGH);

  node.setI2CPowerEnabled(true);
  delay(100);
  node.beginI2C();
  delay(100);

  (void)node.rtc().begin();
  (void)node.powerGauge().begin();
  (void)logger.begin();

  flightCapLog(F("FlightCap: setup"));

  if (node.screen().begin()) {
    digitalWrite(tumbly::PIN_LED_FRONT, LOW);
    flightCapUiRenderSplash(node);
    g_splashStartMs = millis();
  } else {
    digitalWrite(tumbly::PIN_LED_FRONT, LOW);
    flightCapLog(F("FlightCap: screen init failed"));
    g_appState = AppState::MainMenu;
  }

  initNvs();
  flightCapBleInit();
  flightCapBleStartContinuousScan();
  reloadPairsFromSd();
  flushButtonInput();
  flightCapDiagLogBoot(node);
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

  if (handleSdGate()) {
    delay(50);
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
  case AppState::RemoveAllConfirm:
    handleRemoveAllConfirm();
    break;
  case AppState::RemoveAllPairs:
    handleRemoveAllPairs();
    break;
  case AppState::AdvancedMenu:
    handleAdvancedMenu();
    break;
  case AppState::ActiveScanner:
    handleActiveScanner();
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
