#include "FlightCapBle.h"
#include <cstring>

static constexpr uint32_t kStaleTimeoutMs = 15000;
static constexpr uint32_t kGhostPruneDeltaMs = 4000;
static constexpr uint32_t kScanDurationMs = 30 * 1000;

static PairedDeviceState g_devices[kMaxPairedDevices];
static uint8_t g_deviceCount = 0;
static portMUX_TYPE g_remoteMux = portMUX_INITIALIZER_UNLOCKED;

static FlightCapBleMode g_mode = FlightCapBleMode::Off;
static const FlightCapPairList *g_pairList = nullptr;
static bool (*g_pairAddCb)(const uint8_t addr[6], char addedId[13]) = nullptr;
static bool g_continuousScan = false;
static bool g_nimBleInitialized = false;

static bool parseTelemetryAdv(const std::string &mfg, TelemetryAdv &out) {
  if (mfg.size() < sizeof(TelemetryAdv)) {
    return false;
  }
  memcpy(&out, mfg.data(), sizeof(TelemetryAdv));
  if (out.company_id != FLIGHTCAP_COMPANY_ID) {
    return false;
  }
  if (out.magic != TELEMETRY_MAGIC || out.version != TELEMETRY_VERSION) {
    return false;
  }
  return true;
}

static bool addrEqual(const uint8_t *a, const uint8_t *b) {
  return memcmp(a, b, 6) == 0;
}

static bool isPairedAddr(const uint8_t addr[6]) {
  if (g_pairList == nullptr) {
    return false;
  }
  char id[13];
  addrToId(addr, id);
  return flightCapPairsContains(*g_pairList, id);
}

static PairedDeviceState *findDeviceByAddr(const uint8_t addr[6]) {
  for (uint8_t i = 0; i < g_deviceCount; ++i) {
    if (addrEqual(g_devices[i].addr, addr)) {
      return &g_devices[i];
    }
  }
  return nullptr;
}

static void syncDevicesFromPairList() {
  if (g_pairList == nullptr) {
    g_deviceCount = 0;
    memset(g_devices, 0, sizeof(g_devices));
    return;
  }
  g_deviceCount = g_pairList->count;
  for (uint8_t i = 0; i < g_deviceCount; ++i) {
    memset(&g_devices[i], 0, sizeof(g_devices[i]));
    strncpy(g_devices[i].id, g_pairList->ids[i], 12);
    g_devices[i].id[12] = '\0';
    g_devices[i].last_seq = 0xFFFF;
    (void)idToAddr(g_devices[i].id, g_devices[i].addr);
  }
}

static void applyTelemetryUpdate(const uint8_t addr[6], const TelemetryAdv &adv, int8_t rssi) {
  portENTER_CRITICAL(&g_remoteMux);
  PairedDeviceState *slot = findDeviceByAddr(addr);
  if (slot == nullptr) {
    portEXIT_CRITICAL(&g_remoteMux);
    return;
  }

  const uint32_t now = millis();
  slot->rssi = rssi;
  slot->last_seen_ms = now;
  slot->seenThisInterval = true;

  if (adv.seq == slot->last_seq) {
    slot->valid = true;
    portEXIT_CRITICAL(&g_remoteMux);
    return;
  }

  slot->last_seq = adv.seq;
  slot->distance_mm = adv.distance_mm;
  slot->interactions = adv.interactions;
  slot->flags = adv.flags;
  slot->valid = true;
  portEXIT_CRITICAL(&g_remoteMux);
}

class FlightCapScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    if (!advertisedDevice->haveManufacturerData()) {
      return;
    }

    TelemetryAdv adv{};
    if (!parseTelemetryAdv(advertisedDevice->getManufacturerData(), adv)) {
      return;
    }

    uint8_t addr[6];
    memcpy(addr, advertisedDevice->getAddress().getVal(), sizeof(addr));

    if (telemetryIsPairMode(adv)) {
      if (g_mode == FlightCapBleMode::PairActive && g_pairAddCb != nullptr) {
        char addedId[13];
        (void)g_pairAddCb(addr, addedId);
      }
      return;
    }

    if (g_mode == FlightCapBleMode::LoggingWindow || g_mode == FlightCapBleMode::IdleMenu) {
      if (g_mode == FlightCapBleMode::LoggingWindow && !isPairedAddr(addr)) {
        return;
      }
      applyTelemetryUpdate(addr, adv, advertisedDevice->getRSSI());
    }
  }

  void onScanEnd(const NimBLEScanResults &results, int reason) override {
    (void)results;
    (void)reason;
    if (g_continuousScan) {
      NimBLEDevice::getScan()->start(kScanDurationMs, false, true);
    }
  }
};

static FlightCapScanCallbacks g_scanCallbacks;

static void configureNimBleScan() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&g_scanCallbacks, true);
  scan->setActiveScan(false);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->setMaxResults(0);
}

void flightCapBleInit() {
  memset(g_devices, 0, sizeof(g_devices));
  flightCapBleEnsureInit();
}

void flightCapBleEnsureInit() {
  if (g_nimBleInitialized) {
    return;
  }
  NimBLEDevice::init("");
  configureNimBleScan();
  g_nimBleInitialized = true;
}

void flightCapBleStopForSleep() {
  g_continuousScan = false;
  if (g_nimBleInitialized) {
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan != nullptr) {
      scan->stop();
    }
    delay(100);
  }
  flightCapBleSetMode(FlightCapBleMode::Off);
}

void flightCapBleSetMode(FlightCapBleMode mode) {
  g_mode = mode;
}

FlightCapBleMode flightCapBleMode() {
  return g_mode;
}

void flightCapBleSetPairList(const FlightCapPairList *list) {
  g_pairList = list;
  syncDevicesFromPairList();
}

void flightCapBleSetPairAddCallback(bool (*cb)(const uint8_t addr[6], char addedId[13])) {
  g_pairAddCb = cb;
}

void flightCapBleStartContinuousScan() {
  flightCapBleEnsureInit();
  g_continuousScan = true;
  if (g_mode == FlightCapBleMode::Off || g_mode == FlightCapBleMode::LoggingWindow) {
    g_mode = FlightCapBleMode::IdleMenu;
  }
  NimBLEDevice::getScan()->start(kScanDurationMs, false, true);
}

void flightCapBleStopScan() {
  g_continuousScan = false;
  if (g_nimBleInitialized && NimBLEDevice::getScan() != nullptr) {
    NimBLEDevice::getScan()->stop();
  }
  flightCapBleSetMode(FlightCapBleMode::Off);
}

bool flightCapBleRunScanWindow(uint32_t durationMs) {
  flightCapBleEnsureInit();
  flightCapBleSetMode(FlightCapBleMode::LoggingWindow);
  g_continuousScan = false;
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->stop();
  scan->start(durationMs, false, true);
  const uint32_t start = millis();
  while (millis() - start < durationMs + 100) {
    delay(10);
  }
  scan->stop();
  return true;
}

void flightCapBleBeginLogInterval() {
  portENTER_CRITICAL(&g_remoteMux);
  for (uint8_t i = 0; i < g_deviceCount; ++i) {
    g_devices[i].seenThisInterval = false;
  }
  portEXIT_CRITICAL(&g_remoteMux);
}

void flightCapBleEndLogInterval() {
}

PairedDeviceState *flightCapBleDeviceStates() {
  return g_devices;
}

uint8_t flightCapBleDeviceCount() {
  return g_deviceCount;
}

void flightCapBleApplyStaleTimeout() {
  const uint32_t now = millis();
  portENTER_CRITICAL(&g_remoteMux);
  for (uint8_t i = 0; i < g_deviceCount; ++i) {
    if (!g_devices[i].valid) {
      continue;
    }
    if ((now - g_devices[i].last_seen_ms) > kStaleTimeoutMs) {
      g_devices[i].valid = false;
    }
  }
  portEXIT_CRITICAL(&g_remoteMux);
}
