#include "FlightCapUi.h"
#include <math.h>

namespace {

enum class ArrowKind : uint8_t { Up, Right, Down };

static void drawArrowUp(Adafruit_SSD1306 &d, int16_t x, int16_t y) {
  d.fillTriangle(x + 3, y, x, y + 6, x + 6, y + 6, SSD1306_WHITE);
}

static void drawArrowRight(Adafruit_SSD1306 &d, int16_t x, int16_t y) {
  d.fillTriangle(x + 6, y + 3, x, y, x, y + 6, SSD1306_WHITE);
}

static void drawArrowDown(Adafruit_SSD1306 &d, int16_t x, int16_t y) {
  d.fillTriangle(x + 3, y + 6, x, y, x + 6, y, SSD1306_WHITE);
}

static void drawArrow(Adafruit_SSD1306 &d, int16_t x, int16_t y, ArrowKind kind) {
  switch (kind) {
  case ArrowKind::Up:
    drawArrowUp(d, x, y);
    break;
  case ArrowKind::Right:
    drawArrowRight(d, x, y);
    break;
  case ArrowKind::Down:
    drawArrowDown(d, x, y);
    break;
  }
}

static void printTextLine(Adafruit_SSD1306 &d, uint8_t line, const char *text, int16_t textX = 0) {
  if (text == nullptr) {
    return;
  }
  d.setTextSize(1);
  d.setCursor(textX, line * 8);
  d.print(text);
}

static void printActionLine(Adafruit_SSD1306 &d, uint8_t line, ArrowKind arrow,
                            const char *label) {
  if (label == nullptr) {
    return;
  }
  drawArrow(d, 0, line * 8, arrow);
  printTextLine(d, line, label, 10);
}

static void beginScreen(tumbly::HublinkNode &node) {
  if (!node.screen().isInitialized()) {
    return;
  }
  node.screen().clear();
}

static void endScreen(tumbly::HublinkNode &node) {
  if (!node.screen().isInitialized()) {
    return;
  }
  node.screen().show();
}

static void renderTextLines(tumbly::HublinkNode &node, const char *line0, const char *line1 = nullptr,
                            const char *line2 = nullptr, const char *line3 = nullptr,
                            const char *line4 = nullptr, const char *line5 = nullptr,
                            const char *line6 = nullptr, const char *line7 = nullptr) {
  if (!node.screen().isInitialized()) {
    return;
  }
  beginScreen(node);
  auto &d = node.screen().display();
  const char *lines[] = {line0, line1, line2, line3, line4, line5, line6, line7};
  for (uint8_t i = 0; i < 8; ++i) {
    printTextLine(d, i, lines[i]);
  }
  endScreen(node);
}

static void renderWithHeaderAndActions(tumbly::HublinkNode &node, const char *header0,
                                       const char *header1, const char *header2,
                                       const char *actionUp, const char *actionMid,
                                       const char *actionDown) {
  if (!node.screen().isInitialized()) {
    return;
  }
  beginScreen(node);
  auto &d = node.screen().display();
  printTextLine(d, 0, header0);
  printTextLine(d, 1, header1);
  printTextLine(d, 2, header2);
  printActionLine(d, 4, ArrowKind::Up, actionUp);
  printActionLine(d, 5, ArrowKind::Right, actionMid);
  printActionLine(d, 6, ArrowKind::Down, actionDown);
  endScreen(node);
}

static void renderMenuWithActions(tumbly::HublinkNode &node, const char *title,
                                  const char *subtitle, const char *actionUp,
                                  const char *actionMid, const char *actionDown,
                                  const char *footer = nullptr) {
  if (!node.screen().isInitialized()) {
    return;
  }
  beginScreen(node);
  auto &d = node.screen().display();
  printTextLine(d, 0, title);
  printTextLine(d, 1, subtitle);
  printActionLine(d, 3, ArrowKind::Up, actionUp);
  printActionLine(d, 4, ArrowKind::Right, actionMid);
  printActionLine(d, 5, ArrowKind::Down, actionDown);
  printTextLine(d, 7, footer);
  endScreen(node);
}

} // namespace

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

void flightCapUiRenderInsertSd(tumbly::HublinkNode &node, uint8_t pairCount) {
  char line0[22];
  char line1[22];
  char line2[22];
  flightCapUiFillFixedHeader(node, pairCount, line0, line1, line2);
  renderTextLines(node, line0, line1, line2, "INSERT SD", nullptr, nullptr, nullptr);
}

void flightCapUiRenderMainMenu(tumbly::HublinkNode &node, uint8_t pairCount) {
  char line0[22];
  char line1[22];
  char line2[22];
  flightCapUiFillFixedHeader(node, pairCount, line0, line1, line2);

  if (pairCount == 0) {
    renderWithHeaderAndActions(node, line0, line1, line2, "Start (0 caps)", "Add Pairs",
                               "Advanced");
  } else {
    renderWithHeaderAndActions(node, line0, line1, line2, "Start Logging", "Manage Pairs",
                               "Advanced");
  }
}

void flightCapUiRenderManagePairsMenu(tumbly::HublinkNode &node, uint8_t pairCount) {
  char subtitle[22];
  snprintf(subtitle, sizeof(subtitle), "Pairs: %u", pairCount);
  if (pairCount == 0) {
    renderMenuWithActions(node, "Manage Pairs", subtitle, "Pair caps", "Remove (none)",
                          "Remove all --", "Back");
  } else {
    renderMenuWithActions(node, "Manage Pairs", subtitle, "Pair caps", "Remove Single",
                          "Remove all", "Back");
  }
}

void flightCapUiRenderAdvancedMenu(tumbly::HublinkNode &node) {
  renderMenuWithActions(node, "Advanced", nullptr, "Active Scanner", nullptr, nullptr, "Back");
}

void flightCapUiRenderActiveScanner(tumbly::HublinkNode &node, const ActiveScannerCap &cap,
                                    uint32_t secondsSinceData, float lux, float tempC,
                                    bool hasLux, bool hasTemp) {
  char line1[22];
  char line2[22];
  char line3[22];
  char line4[22];
  char line5[22];

  if (cap.locked) {
    deviceAddrToId(cap.device_addr, line1);
    const int dist =
        (cap.flags & FLAG_DIST_VALID) ? static_cast<int>(cap.distance_mm) : -1;
    snprintf(line2, sizeof(line2), "d=%d i=%u", dist, cap.interactions);
    snprintf(line3, sizeof(line3), "seq=%u rssi=%d", cap.seq, cap.rssi);
    snprintf(line4, sizeof(line4), "Last Data: %lus", static_cast<unsigned long>(secondsSinceData));
  } else {
    strncpy(line1, "Scanning...", sizeof(line1));
    line1[sizeof(line1) - 1] = '\0';
    line2[0] = '\0';
    line3[0] = '\0';
    snprintf(line4, sizeof(line4), "Last Data: --");
  }

  if (hasLux && hasTemp) {
    snprintf(line5, sizeof(line5), "Lux: %.0f T: %.1fC", lux, tempC);
  } else if (hasLux) {
    snprintf(line5, sizeof(line5), "Lux: %.0f T: --", lux);
  } else if (hasTemp) {
    snprintf(line5, sizeof(line5), "Lux: -- T: %.1fC", tempC);
  } else {
    strncpy(line5, "Lux: -- T: --", sizeof(line5));
    line5[sizeof(line5) - 1] = '\0';
  }

  renderTextLines(node, "Active Scanner", line1, line2[0] ? line2 : nullptr,
                  line3[0] ? line3 : nullptr, line4, line5, nullptr, "Back");
}

void flightCapUiRenderPairActive(tumbly::HublinkNode &node, const char *lastAddedId,
                                 uint8_t pairCount) {
  char line2[22];
  char line3[22];
  if (lastAddedId != nullptr && lastAddedId[0] != '\0') {
    snprintf(line2, sizeof(line2), "Added %s", lastAddedId);
  } else {
    snprintf(line2, sizeof(line2), "Scanning caps...");
  }
  snprintf(line3, sizeof(line3), "Pairs: %u", pairCount);
  renderTextLines(node, "Pair caps", "Put cap in pair mode", line2, line3, nullptr, nullptr,
                  "Back");
}

void flightCapUiRenderRemoveSingle(tumbly::HublinkNode &node, const FlightCapPairList &list,
                                   uint8_t index) {
  if (list.count == 0) {
    renderTextLines(node, "Remove pair", "No pairs saved", "Pair caps first", nullptr, nullptr,
                    nullptr, "Back");
    return;
  }

  if (!node.screen().isInitialized()) {
    return;
  }

  char line4[22];
  char line5[22];
  snprintf(line4, sizeof(line4), ">%s", list.ids[index]);
  snprintf(line5, sizeof(line5), "%u/%u", static_cast<unsigned>(index + 1),
           static_cast<unsigned>(list.count));

  beginScreen(node);
  auto &d = node.screen().display();
  printTextLine(d, 0, "Remove pair");
  drawArrow(d, 0, 2 * 8, ArrowKind::Up);
  drawArrow(d, 8, 2 * 8, ArrowKind::Down);
  printTextLine(d, 2, "Scroll", 18);
  printActionLine(d, 3, ArrowKind::Right, "Remove");
  printTextLine(d, 4, line4);
  printTextLine(d, 5, line5);
  printTextLine(d, 7, "Back");
  endScreen(node);
}

void flightCapUiRenderRemoveAllConfirm(tumbly::HublinkNode &node, uint8_t pairCount) {
  char subtitle[22];
  snprintf(subtitle, sizeof(subtitle), "Remove %u pair(s)?", pairCount);
  renderMenuWithActions(node, "Remove all", subtitle, nullptr, "Confirm", nullptr, "Back");
}

void flightCapUiRenderMessage(tumbly::HublinkNode &node, uint8_t pairCount, const char *line4,
                              const char *line5, const char *line6, bool showBootBack) {
  (void)pairCount;
  renderTextLines(node, line4, line5, line6, nullptr, nullptr, nullptr, nullptr,
                  showBootBack ? "Back" : nullptr);
}

void flightCapUiRenderLoggingPeek(tumbly::HublinkNode &node, const FlightCapPairList &list) {
  char line0[22];
  char line1[22];
  char line2[22];
  char line4[22];
  char line5[22];
  char line6[22];
  char line7[22];
  flightCapUiFillFixedHeader(node, list.count, line0, line1, line2);
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

  if (!node.screen().isInitialized()) {
    return;
  }
  beginScreen(node);
  auto &d = node.screen().display();
  printTextLine(d, 0, line0);
  printTextLine(d, 1, line1);
  printTextLine(d, 2, line2);
  printTextLine(d, 4, line4);
  printTextLine(d, 5, line5[0] ? line5 : nullptr);
  printTextLine(d, 6, line6[0] ? line6 : nullptr);
  printTextLine(d, 7, line7[0] ? line7 : nullptr);
  endScreen(node);
}
