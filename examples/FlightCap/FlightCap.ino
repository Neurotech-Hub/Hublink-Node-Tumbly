// FlightCap — passive NimBLE scanner for compact nRF52 telemetry in manufacturer data.
// No GATT connection. Parses distance, interactions, seq, RSSI; deduplicates by seq.
// Packets with FLAG_PAIR_MODE are ignored for telemetry (pair/assign UI — TBD).
//
// Build: ESP32S3 Dev Module, Tools → Bluetooth → NimBLE, NimBLE-Arduino library.

#include <HublinkNodeTumbly.h>
#include <NimBLEDevice.h>
#include <Wire.h>
#include <cstring>
#include <nvs_flash.h>

#include "TelemetryAdv.h"

tumbly::HublinkNode node;

static constexpr char kFirmwareVersion[] = "1.0";
static constexpr uint32_t kPrintIntervalMs = 1000;
static constexpr uint32_t kStaleTimeoutMs = 15000;
static constexpr uint32_t kGhostPruneDeltaMs = 4000;
static constexpr uint32_t kPairSightTimeoutMs = 5000;
static constexpr uint32_t kScanDurationMs = 30 * 1000;
static constexpr uint8_t kMaxRemotes = 8;

static RemoteTelemetry g_remotes[kMaxRemotes];
static portMUX_TYPE g_remoteMux = portMUX_INITIALIZER_UNLOCKED;

// Latest pair-mode advertisement (not stored in g_remotes); for future assign UI.
typedef struct {
  uint8_t addr[6];
  int8_t rssi;
  uint32_t last_seen_ms;
  bool active;
} PairBeaconSight;

static PairBeaconSight g_pairSight = {};

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

static RemoteTelemetry *findRemoteSlot(const uint8_t addr[6]) {
  for (uint8_t i = 0; i < kMaxRemotes; ++i) {
    if (addrEqual(g_remotes[i].addr, addr)) {
      return &g_remotes[i];
    }
  }
  return nullptr;
}

static bool isEmptySlot(const RemoteTelemetry &slot) {
  for (uint8_t i = 0; i < 6; ++i) {
    if (slot.addr[i] != 0) {
      return false;
    }
  }
  return true;
}

static RemoteTelemetry *allocRemoteSlot(const uint8_t addr[6]) {
  RemoteTelemetry *existing = findRemoteSlot(addr);
  if (existing != nullptr) {
    return existing;
  }

  // Reuse an invalid slot before claiming a never-used one.
  RemoteTelemetry *reuse = nullptr;
  for (uint8_t i = 0; i < kMaxRemotes; ++i) {
    if (!g_remotes[i].valid && !isEmptySlot(g_remotes[i])) {
      if (reuse == nullptr || g_remotes[i].last_seen_ms < reuse->last_seen_ms) {
        reuse = &g_remotes[i];
      }
    }
  }
  if (reuse != nullptr) {
    memset(reuse, 0, sizeof(*reuse));
    memcpy(reuse->addr, addr, 6);
    reuse->last_seq = 0xFFFF;
    return reuse;
  }

  for (uint8_t i = 0; i < kMaxRemotes; ++i) {
    if (isEmptySlot(g_remotes[i])) {
      memset(&g_remotes[i], 0, sizeof(g_remotes[i]));
      memcpy(g_remotes[i].addr, addr, 6);
      g_remotes[i].last_seq = 0xFFFF;
      return &g_remotes[i];
    }
  }
  return nullptr;
}

static void applyTelemetryUpdate(const uint8_t addr[6], const TelemetryAdv &adv, int8_t rssi) {
  portENTER_CRITICAL(&g_remoteMux);
  RemoteTelemetry *slot = allocRemoteSlot(addr);
  if (slot == nullptr) {
    portEXIT_CRITICAL(&g_remoteMux);
    return;
  }

  const uint32_t now = millis();
  slot->rssi = rssi;
  slot->last_seen_ms = now;

  // Same seq repeats every 500 ms when values unchanged; still refresh presence.
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

static void notePairBeacon(const uint8_t addr[6], int8_t rssi) {
  portENTER_CRITICAL(&g_remoteMux);
  memcpy(g_pairSight.addr, addr, 6);
  g_pairSight.rssi = rssi;
  g_pairSight.last_seen_ms = millis();
  g_pairSight.active = true;
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
      notePairBeacon(addr, advertisedDevice->getRSSI());
      return;
    }

    applyTelemetryUpdate(addr, adv, advertisedDevice->getRSSI());
  }

  void onScanEnd(const NimBLEScanResults &results, int reason) override {
    (void)results;
    (void)reason;
    NimBLEDevice::getScan()->start(kScanDurationMs, false, true);
  }
};

static FlightCapScanCallbacks g_scanCallbacks;

static void initNvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

static void startBleScan() {
  NimBLEDevice::init("");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&g_scanCallbacks, true);
  scan->setActiveScan(false);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->setMaxResults(0);
  scan->start(kScanDurationMs, false, true);
  Serial.println(F("FlightCap: passive BLE scan started"));
}

static void applyStaleTimeout() {
  const uint32_t now = millis();
  portENTER_CRITICAL(&g_remoteMux);

  uint32_t newestSeenMs = 0;
  for (uint8_t i = 0; i < kMaxRemotes; ++i) {
    if (g_remotes[i].valid && g_remotes[i].last_seen_ms > newestSeenMs) {
      newestSeenMs = g_remotes[i].last_seen_ms;
    }
  }

  for (uint8_t i = 0; i < kMaxRemotes; ++i) {
    if (!g_remotes[i].valid) {
      continue;
    }
    if ((now - g_remotes[i].last_seen_ms) > kStaleTimeoutMs) {
      g_remotes[i].valid = false;
      continue;
    }
    // Drop stale MAC entries when a fresher remote is active (e.g. nRF reboot/new address).
    if (newestSeenMs > g_remotes[i].last_seen_ms &&
        (newestSeenMs - g_remotes[i].last_seen_ms) > kGhostPruneDeltaMs) {
      g_remotes[i].valid = false;
    }
  }

  if (g_pairSight.active && (now - g_pairSight.last_seen_ms) > kPairSightTimeoutMs) {
    g_pairSight.active = false;
  }
  portEXIT_CRITICAL(&g_remoteMux);
}

static void printRemotes(Stream &out) {
  out.println(F("--- FlightCap remotes ---"));
  const uint32_t now = millis();

  portENTER_CRITICAL(&g_remoteMux);
  uint8_t count = 0;
  for (uint8_t i = 0; i < kMaxRemotes; ++i) {
    const RemoteTelemetry &r = g_remotes[i];
    if (!r.valid) {
      continue;
    }
    ++count;
    out.printf("  %02X:%02X:%02X:%02X:%02X:%02X seq=%u dist=%d mm int=%u fl=0x%02X rssi=%d age=%lu ms\n",
               r.addr[5], r.addr[4], r.addr[3], r.addr[2], r.addr[1], r.addr[0], r.last_seq,
               (r.flags & FLAG_DIST_VALID) ? static_cast<int>(r.distance_mm) : -1, r.interactions,
               r.flags, r.rssi, static_cast<unsigned long>(now - r.last_seen_ms));
  }
  portEXIT_CRITICAL(&g_remoteMux);

  if (count == 0) {
    out.println(F("  (none)"));
  } else {
    out.print(F("  total="));
    out.println(count);
  }

  portENTER_CRITICAL(&g_remoteMux);
  const bool pairActive = g_pairSight.active;
  PairBeaconSight pair = g_pairSight;
  portEXIT_CRITICAL(&g_remoteMux);

  if (pairActive) {
    out.print(F("  pair (ignored): "));
    out.printf("%02X:%02X:%02X:%02X:%02X:%02X rssi=%d age=%lu ms\n", pair.addr[5], pair.addr[4],
               pair.addr[3], pair.addr[2], pair.addr[1], pair.addr[0], pair.rssi,
               static_cast<unsigned long>(now - pair.last_seen_ms));
  }
}

static void formatBleAddress(char *out, size_t outLen, const uint8_t addr[6]) {
  snprintf(out, outLen, "%02X:%02X:%02X:%02X:%02X:%02X", addr[5], addr[4], addr[3], addr[2],
           addr[1], addr[0]);
}

static void renderOledSummary() {
  if (!node.screen().isInitialized()) {
    return;
  }

  char line0[22];
  char line1[22];
  char line2[22];
  char line3[22];
  char line4[22];
  char line5[22];
  char line6[22];
  char line7[22];

  uint8_t validCount = 0;
  RemoteTelemetry latest{};
  bool haveLatest = false;
  PairBeaconSight pair = {};
  bool pairActive = false;

  portENTER_CRITICAL(&g_remoteMux);
  for (uint8_t i = 0; i < kMaxRemotes; ++i) {
    if (!g_remotes[i].valid) {
      continue;
    }
    ++validCount;
    if (!haveLatest || g_remotes[i].last_seen_ms >= latest.last_seen_ms) {
      latest = g_remotes[i];
      haveLatest = true;
    }
  }
  pairActive = g_pairSight.active;
  if (pairActive) {
    pair = g_pairSight;
  }
  portEXIT_CRITICAL(&g_remoteMux);

  const uint32_t now = millis();
  snprintf(line0, sizeof(line0), "FlightCap v%s", kFirmwareVersion);

  if (!haveLatest) {
    if (pairActive) {
      formatBleAddress(line1, sizeof(line1), pair.addr);
      snprintf(line2, sizeof(line2), "PAIR mode (ignored)");
      snprintf(line3, sizeof(line3), "rssi=%d assign TBD", pair.rssi);
      snprintf(line4, sizeof(line4), "remotes: 0");
      node.screen().printLines(line0, line1, line2, line3, line4);
    } else {
      snprintf(line1, sizeof(line1), "awaiting run adv...");
      snprintf(line2, sizeof(line2), "remotes: 0");
      node.screen().printLines(line0, line1, line2);
    }
    return;
  }

  const int dist =
      (latest.flags & FLAG_DIST_VALID) ? static_cast<int>(latest.distance_mm) : -1;
  const unsigned long ageMs = now - latest.last_seen_ms;

  formatBleAddress(line1, sizeof(line1), latest.addr);
  snprintf(line2, sizeof(line2), "seq=%u dist=%dmm", latest.last_seq, dist);
  snprintf(line3, sizeof(line3), "int=%u rssi=%d", latest.interactions, latest.rssi);
  snprintf(line4, sizeof(line4), "fl=0x%02X age=%lums", latest.flags,
           static_cast<unsigned long>(ageMs));
  snprintf(line5, sizeof(line5), "remotes: %u", validCount);
  if (pairActive) {
    snprintf(line6, sizeof(line6), "PAIR ignored");
  } else if (validCount > 1) {
    snprintf(line6, sizeof(line6), "(latest shown)");
  } else {
    snprintf(line6, sizeof(line6), "");
  }
  snprintf(line7, sizeof(line7), "");

  node.screen().printLines(line0, line1, line2, line3, line4, line5, line6, line7);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  memset(g_remotes, 0, sizeof(g_remotes));
  for (uint8_t i = 0; i < kMaxRemotes; ++i) {
    g_remotes[i].last_seq = 0xFFFF;
  }

  node.beginHardware();
  node.setI2CPowerEnabled(true);
  delay(100);
  node.beginI2C();
  delay(100);

  if (!node.rtc().begin()) {
    Serial.println(F("RTC: not found"));
  }

  if (node.screen().begin()) {
    char fwLine[22];
    snprintf(fwLine, sizeof(fwLine), "Firmware v%s", kFirmwareVersion);
    node.screen().printLines("FlightCap", fwLine, "BLE scan passive", "");
  } else {
    Serial.println(F("Screen init failed (check I2C_EN)"));
  }

  initNvs();
  startBleScan();
}

void loop() {
  static uint32_t lastPrintMs = 0;
  const uint32_t now = millis();

  if (lastPrintMs == 0 || (now - lastPrintMs) >= kPrintIntervalMs) {
    lastPrintMs = now;
    applyStaleTimeout();
    printRemotes(Serial);
    renderOledSummary();
  }

  delay(10);
}
