#include "FlightCapUi.h"
#include <math.h>

void flightCapUiRenderSplash(tumbly::HublinkNode &node) {
  if (!node.screen().isInitialized()) {
    return;
  }
  auto &d = node.screen().display();
  d.clearDisplay();
  d.setTextSize(2);
  d.setCursor(10, 8);
  d.print(F("FlightCap"));
  d.setTextSize(1);
  d.setCursor(46, 32);
  d.print(F("v"));
  d.print(kFlightCapFirmwareVersion);
  d.display();
}

void flightCapUiFillFixedHeader(tumbly::HublinkNode &node, uint8_t pairCount, char line0[22],
                                char line1[22], char line2[22]) {
  const tumbly::RtcReading rtc = node.rtc().readSample();
  if (rtc.status == tumbly::ServiceStatus::Ok) {
    snprintf(line0, 22, "%04d-%02d-%02d %02d:%02d:%02d", rtc.now.year(), rtc.now.month(),
             rtc.now.day(), rtc.now.hour(), rtc.now.minute(), rtc.now.second());
  } else {
    snprintf(line0, 22, "RTC unavailable");
  }

  const tumbly::BatteryReading battery = node.powerGauge().readSample();
  const bool hasBattery =
      battery.status == tumbly::ServiceStatus::Ok && battery.hasCellReading;
  const bool hasSoc = hasBattery && !isnan(battery.stateOfChargePct);
  if (hasBattery && hasSoc) {
    snprintf(line1, 22, "%.2fV, %.0f%% Charged", battery.voltageV, battery.stateOfChargePct);
  } else if (hasBattery) {
    snprintf(line1, 22, "%.2fV, --%% Charged", battery.voltageV);
  } else if (node.readUsbSense()) {
    snprintf(line1, 22, "USB powered");
  } else {
    snprintf(line1, 22, "--V, --%% Charged");
  }

  if (!node.readSdDetect()) {
    snprintf(line2, 22, "SD: Missing Pairs:%u", pairCount);
  } else if (node.sd().isMounted() || node.sd().begin()) {
    snprintf(line2, 22, "SD: Ready Pairs:%u", pairCount);
  } else {
    snprintf(line2, 22, "SD: Error Pairs:%u", pairCount);
  }
}

static void renderWithFixed(tumbly::HublinkNode &node, uint8_t pairCount, const char *line4,
                            const char *line5 = nullptr, const char *line6 = nullptr,
                            const char *line7 = nullptr) {
  char line0[22];
  char line1[22];
  char line2[22];
  flightCapUiFillFixedHeader(node, pairCount, line0, line1, line2);
  node.screen().printLines(line0, line1, line2, nullptr, line4, line5, line6, line7);
}

void flightCapUiRenderMainMenu(tumbly::HublinkNode &node, uint8_t pairCount) {
  if (pairCount == 0) {
    // 128px wide OLED fits ~21 chars at text size 1; pair count is on the SD row.
    renderWithFixed(node, pairCount, "BTN0: Start (0 caps)", "BTN1: Add Pairs", "BTN2: Settings");
  } else {
    renderWithFixed(node, pairCount, "BTN0: Start Logging", "BTN1: Manage Pairs", "BTN2: Settings");
  }
}

void flightCapUiRenderManagePairsMenu(tumbly::HublinkNode &node, uint8_t pairCount) {
  if (pairCount == 0) {
    renderWithFixed(node, pairCount, "BTN0: Pair caps", "BTN1: Remove (none)",
                    "BTN2: Remove all --", "BOOT: Back");
  } else {
    renderWithFixed(node, pairCount, "BTN0: Pair caps", "BTN1: Remove Single",
                    "BTN2: Remove all", "BOOT: Back");
  }
}

void flightCapUiRenderSettingsStub(tumbly::HublinkNode &node, uint8_t pairCount) {
  renderWithFixed(node, pairCount, "Coming soon", nullptr, nullptr, "BOOT: Back");
}

void flightCapUiRenderPairActive(tumbly::HublinkNode &node, const char *lastAddedId,
                                 uint8_t pairCount) {
  char line4[22];
  char line5[22];
  char line6[22];
  if (lastAddedId != nullptr && lastAddedId[0] != '\0') {
    snprintf(line4, sizeof(line4), "Added %s", lastAddedId);
  } else {
    snprintf(line4, sizeof(line4), "Scanning caps...");
  }
  snprintf(line5, sizeof(line5), "Pairs: %u", pairCount);
  snprintf(line6, sizeof(line6), "BOOT: Back");
  renderWithFixed(node, pairCount, line4, line5, line6);
}

void flightCapUiRenderRemoveSingle(tumbly::HublinkNode &node, const FlightCapPairList &list,
                                   uint8_t index) {
  char line4[22];
  char line5[22];
  char line6[22];
  char line7[22];
  if (list.count == 0) {
    snprintf(line4, sizeof(line4), "No pairs saved");
    snprintf(line5, sizeof(line5), "Pair caps first");
    renderWithFixed(node, list.count, line4, line5, nullptr, "BOOT: Back");
    return;
  }
  snprintf(line4, sizeof(line4), "BTN0/2 Scroll");
  snprintf(line5, sizeof(line5), "BTN1 Remove");
  snprintf(line6, sizeof(line6), ">%s", list.ids[index]);
  snprintf(line7, sizeof(line7), "%u/%u BOOT:Back", static_cast<unsigned>(index + 1),
           static_cast<unsigned>(list.count));
  renderWithFixed(node, list.count, line4, line5, line6, line7);
}

void flightCapUiRenderMessage(tumbly::HublinkNode &node, uint8_t pairCount, const char *line4,
                              const char *line5, const char *line6, bool showBootBack) {
  renderWithFixed(node, pairCount, line4, line5, line6, showBootBack ? "BOOT: Back" : nullptr);
}

void flightCapUiRenderLoggingPeek(tumbly::HublinkNode &node, const FlightCapPairList &list) {
  char line4[22];
  char line5[22];
  char line6[22];
  char line7[22];
  snprintf(line4, sizeof(line4), "Logging: %u pairs", list.count);

  PairedDeviceState *devices = flightCapBleDeviceStates();
  uint8_t shown = 0;
  line5[0] = '\0';
  line6[0] = '\0';
  line7[0] = '\0';

  for (uint8_t i = 0; i < flightCapBleDeviceCount() && shown < 3; ++i) {
    const PairedDeviceState &d = devices[i];
    char *target = (shown == 0) ? line5 : (shown == 1) ? line6 : line7;
    if (d.valid) {
      const int dist = (d.flags & FLAG_DIST_VALID) ? static_cast<int>(d.distance_mm) : -1;
      snprintf(target, 22, "%.6s d=%d i=%u", d.id, dist, d.interactions);
    } else {
      snprintf(target, 22, "%.6s (no data)", d.id);
    }
    ++shown;
  }

  renderWithFixed(node, list.count, line4, line5[0] ? line5 : nullptr, line6[0] ? line6 : nullptr,
                  line7[0] ? line7 : nullptr);
}
