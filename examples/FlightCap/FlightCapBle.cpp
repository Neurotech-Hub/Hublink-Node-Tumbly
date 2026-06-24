#include "FlightCapBle.h"
#include "FlightCapLog.h"
#include <cstring>

static constexpr uint32_t kStaleTimeoutMs = 15000;
static constexpr uint32_t kScanDurationMs = 30 * 1000;
static constexpr bool kPairDebugLog = true;

static PairedDeviceState g_devices[kMaxPairedDevices];
static uint8_t g_deviceCount = 0;
static portMUX_TYPE g_remoteMux = portMUX_INITIALIZER_UNLOCKED;

static FlightCapBleMode g_mode = FlightCapBleMode::Off;
static const FlightCapPairList *g_pairList = nullptr;
static bool g_continuousScan = false;
static bool g_nimBleInitialized = false;

struct PendingPairAdd {
  uint8_t device_addr[6];
  TelemetryAdv adv;
};

static PendingPairAdd g_pendingPairs[kMaxPairedDevices];
static volatile uint8_t g_pendingPairCount = 0;

static bool g_pairSessionComplete = false;
static ActiveScannerCap g_activeScannerCap = {};

static void formatMfgHex(const std::string &mfg, char out[40]) {
  out[0] = '\0';
  const size_t n = mfg.size() < sizeof(TelemetryAdv) ? mfg.size() : sizeof(TelemetryAdv);
  char *p = out;
  for (size_t i = 0; i < n && static_cast<size_t>(p - out) < 38; ++i) {
    p += sprintf(p, "%02X", static_cast<uint8_t>(mfg[i]));
  }
}

static void logPairRejectMfg(const std::string &mfg) {
  if (!kPairDebugLog || g_mode != FlightCapBleMode::PairActive) {
    return;
  }
  static uint32_t lastLogMs = 0;
  const uint32_t now = millis();
  if (now - lastLogMs < 2000) {
    return;
  }
  lastLogMs = now;
  char hex[40];
  formatMfgHex(mfg, hex);
  Serial.printf("FlightCap: pair reject mfg len=%u lead=0x%02X%02X hex=%s\n",
                static_cast<unsigned>(mfg.size()),
                mfg.size() > 0 ? static_cast<uint8_t>(mfg[0]) : 0,
                mfg.size() > 1 ? static_cast<uint8_t>(mfg[1]) : 0, hex);
  Serial.flush();
}

static bool parseTelemetryAdv(const std::string &mfg, TelemetryAdv &out) {
  if (mfg.size() < TELEM_ADV_V02_SIZE) {
    return false;
  }
  if (static_cast<uint8_t>(mfg[0]) != 0x48 || static_cast<uint8_t>(mfg[1]) != 0x4E) {
    return false;
  }
  const uint8_t version = static_cast<uint8_t>(mfg[3]);
  if (!telemetryIsSupportedVersion(version)) {
    return false;
  }
  if (version >= TELEM_ADV_VERSION && mfg.size() < sizeof(TelemetryAdv)) {
    return false;
  }

  memset(&out, 0, sizeof(out));
  if (version >= TELEM_ADV_VERSION) {
    memcpy(&out, mfg.data(), sizeof(TelemetryAdv));
  } else {
    memcpy(&out, mfg.data(), TELEM_ADV_V02_SIZE);
    out.vbatt_mv = 0;
  }
  return telemetryIsFlightCapAdv(out) && telemetryDeviceAddrValid(out);
}

void flightCapBleNotePairSessionCommit(const TelemetryAdv &adv) {
  (void)adv;
  g_pairSessionComplete = true;
  if (kPairDebugLog) {
    flightCapLogPairLine("pair session complete (exit and re-enter to add another)");
  }
}

static void resetPairSession() {
  g_pairSessionComplete = false;
}

static void queuePairAdd(const uint8_t deviceAddr[6], const TelemetryAdv &adv) {
  if (!telemetryIsPairMode(adv) || g_mode != FlightCapBleMode::PairActive) {
    return;
  }
  if (g_pairSessionComplete) {
    if (kPairDebugLog) {
      flightCapLogPairLine("pair queue skip (session already paired one cap)");
    }
    return;
  }
  if (g_pairList != nullptr && flightCapPairsContainsDeviceAddr(*g_pairList, deviceAddr)) {
    if (kPairDebugLog) {
      flightCapLogPairLine("pair queue skip (device already in list)");
    }
    return;
  }

  portENTER_CRITICAL(&g_remoteMux);
  if (g_pendingPairCount >= kMaxPairedDevices) {
    portEXIT_CRITICAL(&g_remoteMux);
    if (kPairDebugLog) {
      flightCapLogPairLine("pair queue full");
    }
    return;
  }
  for (uint8_t i = 0; i < g_pendingPairCount; ++i) {
    if (telemetryDeviceAddrEqual(g_pendingPairs[i].device_addr, deviceAddr)) {
      portEXIT_CRITICAL(&g_remoteMux);
      return;
    }
  }
  PendingPairAdd &slot = g_pendingPairs[g_pendingPairCount++];
  memcpy(slot.device_addr, deviceAddr, sizeof(slot.device_addr));
  slot.adv = adv;
  portEXIT_CRITICAL(&g_remoteMux);

  if (kPairDebugLog) {
    flightCapLogPairLine("pair queued for main loop");
    flightCapLogTelemetryAdv(" ", adv);
  }
}

static void notePairModeAdvert(const TelemetryAdv &adv, int8_t rssi, const std::string &mfgRaw) {
  (void)rssi;
  (void)mfgRaw;
  if (!telemetryIsPairMode(adv) || g_mode != FlightCapBleMode::PairActive) {
    return;
  }
  if (g_pairSessionComplete) {
    return;
  }
  queuePairAdd(adv.device_addr, adv);
}

static void applyActiveScannerUpdate(const TelemetryAdv &adv, int8_t rssi) {
  const uint32_t now = millis();
  portENTER_CRITICAL(&g_remoteMux);
  if (!g_activeScannerCap.locked) {
    g_activeScannerCap.locked = true;
    memcpy(g_activeScannerCap.device_addr, adv.device_addr, sizeof(g_activeScannerCap.device_addr));
    g_activeScannerCap.seq = adv.seq;
    g_activeScannerCap.distance_mm = adv.distance_mm;
    g_activeScannerCap.interactions = adv.interactions;
    g_activeScannerCap.flags = adv.flags;
    g_activeScannerCap.vbatt_mv = adv.vbatt_mv;
    g_activeScannerCap.rssi = rssi;
    g_activeScannerCap.last_data_ms = now;
    g_activeScannerCap.last_seen_ms = now;
    portEXIT_CRITICAL(&g_remoteMux);
    return;
  }

  if (!telemetryDeviceAddrEqual(g_activeScannerCap.device_addr, adv.device_addr)) {
    portEXIT_CRITICAL(&g_remoteMux);
    return;
  }

  g_activeScannerCap.rssi = rssi;
  g_activeScannerCap.last_seen_ms = now;
  if (adv.seq != g_activeScannerCap.seq) {
    g_activeScannerCap.seq = adv.seq;
    g_activeScannerCap.distance_mm = adv.distance_mm;
    g_activeScannerCap.interactions = adv.interactions;
    g_activeScannerCap.flags = adv.flags;
    g_activeScannerCap.vbatt_mv = adv.vbatt_mv;
    g_activeScannerCap.last_data_ms = now;
  }
  portEXIT_CRITICAL(&g_remoteMux);
}

static bool isPairedDevice(const TelemetryAdv &adv) {
  if (g_pairList == nullptr) {
    return false;
  }
  return flightCapPairsContainsDeviceAddr(*g_pairList, adv.device_addr);
}

static PairedDeviceState *findDeviceByDeviceAddr(const uint8_t deviceAddr[6]) {
  for (uint8_t i = 0; i < g_deviceCount; ++i) {
    if (telemetryDeviceAddrEqual(g_devices[i].device_addr, deviceAddr)) {
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
    (void)idToDeviceAddr(g_devices[i].id, g_devices[i].device_addr);
  }
}

static void applyTelemetryUpdate(const TelemetryAdv &adv, int8_t rssi) {
  portENTER_CRITICAL(&g_remoteMux);
  PairedDeviceState *slot = findDeviceByDeviceAddr(adv.device_addr);
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
  slot->vbatt_mv = adv.vbatt_mv;
  slot->valid = true;
  portEXIT_CRITICAL(&g_remoteMux);
}

class FlightCapScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    if (!advertisedDevice->haveManufacturerData()) {
      return;
    }

    const std::string mfgData = advertisedDevice->getManufacturerData();
    TelemetryAdv adv{};
    if (!parseTelemetryAdv(mfgData, adv)) {
      if (g_mode == FlightCapBleMode::PairActive) {
        logPairRejectMfg(mfgData);
      }
      return;
    }

    const int8_t rssi = advertisedDevice->getRSSI();

    if (telemetryIsPairMode(adv)) {
      notePairModeAdvert(adv, rssi, mfgData);
      return;
    }

    if (g_mode == FlightCapBleMode::ActiveScanner) {
      applyActiveScannerUpdate(adv, rssi);
      return;
    }

    if (g_mode != FlightCapBleMode::LoggingWindow && g_mode != FlightCapBleMode::IdleMenu) {
      return;
    }
    if (!isPairedDevice(adv)) {
      return;
    }
    applyTelemetryUpdate(adv, rssi);
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
  flightCapBleClearPendingPairAdds();
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
  if (mode == FlightCapBleMode::PairActive && kPairDebugLog) {
    flightCapLogPairLine("pair scan active (passive, device_addr ID)");
  }
  if (mode != FlightCapBleMode::PairActive) {
    flightCapBleClearPendingPairAdds();
  }
}

FlightCapBleMode flightCapBleMode() {
  return g_mode;
}

void flightCapBleSetPairList(const FlightCapPairList *list) {
  g_pairList = list;
  syncDevicesFromPairList();
}

void flightCapBleClearPendingPairAdds() {
  portENTER_CRITICAL(&g_remoteMux);
  g_pendingPairCount = 0;
  portEXIT_CRITICAL(&g_remoteMux);
  resetPairSession();
}

bool flightCapBleTakePendingPairAdd(uint8_t deviceAddr[6], TelemetryAdv *advOut) {
  portENTER_CRITICAL(&g_remoteMux);
  if (g_pendingPairCount == 0) {
    portEXIT_CRITICAL(&g_remoteMux);
    return false;
  }

  const PendingPairAdd pending = g_pendingPairs[0];
  for (uint8_t i = 1; i < g_pendingPairCount; ++i) {
    g_pendingPairs[i - 1] = g_pendingPairs[i];
  }
  --g_pendingPairCount;
  portEXIT_CRITICAL(&g_remoteMux);

  memcpy(deviceAddr, pending.device_addr, 6);
  if (advOut != nullptr) {
    *advOut = pending.adv;
  }
  return true;
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
    if (flightCapBleAllPairsSeenThisInterval()) {
      flightCapLogPairLine("scan early stop, all pairs heard");
      scan->stop();
      return true;
    }
    delay(10);
  }
  scan->stop();
  return true;
}

bool flightCapBleAllPairsSeenThisInterval() {
  if (g_deviceCount == 0) {
    return true;
  }
  portENTER_CRITICAL(&g_remoteMux);
  bool allSeen = true;
  for (uint8_t i = 0; i < g_deviceCount; ++i) {
    if (!g_devices[i].seenThisInterval) {
      allSeen = false;
      break;
    }
  }
  portEXIT_CRITICAL(&g_remoteMux);
  return allSeen;
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

void flightCapBleBeginActiveScanner() {
  portENTER_CRITICAL(&g_remoteMux);
  memset(&g_activeScannerCap, 0, sizeof(g_activeScannerCap));
  portEXIT_CRITICAL(&g_remoteMux);
  flightCapBleSetMode(FlightCapBleMode::ActiveScanner);
  flightCapBleStartContinuousScan();
}

void flightCapBleEndActiveScanner() {
  portENTER_CRITICAL(&g_remoteMux);
  memset(&g_activeScannerCap, 0, sizeof(g_activeScannerCap));
  portEXIT_CRITICAL(&g_remoteMux);
  if (g_mode == FlightCapBleMode::ActiveScanner) {
    flightCapBleSetMode(FlightCapBleMode::IdleMenu);
  }
}

void flightCapBleGetActiveScannerCap(ActiveScannerCap *out) {
  if (out == nullptr) {
    return;
  }
  portENTER_CRITICAL(&g_remoteMux);
  *out = g_activeScannerCap;
  portEXIT_CRITICAL(&g_remoteMux);
}

uint32_t flightCapBleActiveScannerSecondsSinceData() {
  portENTER_CRITICAL(&g_remoteMux);
  if (!g_activeScannerCap.locked || g_activeScannerCap.last_data_ms == 0) {
    portEXIT_CRITICAL(&g_remoteMux);
    return 0;
  }
  const uint32_t elapsedMs = millis() - g_activeScannerCap.last_data_ms;
  portEXIT_CRITICAL(&g_remoteMux);
  return elapsedMs / 1000U;
}
