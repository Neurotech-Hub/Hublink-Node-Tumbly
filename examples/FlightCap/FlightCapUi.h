#pragma once

#include "FlightCapApp.h"
#include "FlightCapBle.h"
#include "FlightCapPairs.h"
#include <HublinkNodeTumbly.h>

void flightCapUiRenderSplash(tumbly::HublinkNode &node);
void flightCapUiFillFixedHeader(tumbly::HublinkNode &node, uint8_t pairCount, char line0[22],
                                char line1[22], char line2[22]);
void flightCapUiRenderInsertSd(tumbly::HublinkNode &node, uint8_t pairCount);
void flightCapUiRenderMainMenu(tumbly::HublinkNode &node, uint8_t pairCount);
void flightCapUiRenderManagePairsMenu(tumbly::HublinkNode &node, uint8_t pairCount);
void flightCapUiRenderAdvancedMenu(tumbly::HublinkNode &node);
void flightCapUiRenderActiveScanner(tumbly::HublinkNode &node, const ActiveScannerCap &cap,
                                    uint32_t secondsSinceData, float lux, float tempC,
                                    bool hasLux, bool hasTemp);
void flightCapUiRenderPairActive(tumbly::HublinkNode &node, const char *lastAddedId, uint8_t pairCount);
void flightCapUiRenderRemoveSingle(tumbly::HublinkNode &node, const FlightCapPairList &list,
                                   uint8_t index);
void flightCapUiRenderRemoveAllConfirm(tumbly::HublinkNode &node, uint8_t pairCount);
void flightCapUiRenderMessage(tumbly::HublinkNode &node, uint8_t pairCount, const char *line4,
                              const char *line5 = nullptr, const char *line6 = nullptr,
                              bool showBootBack = true);
void flightCapUiRenderLoggingPeek(tumbly::HublinkNode &node, const FlightCapPairList &list);
