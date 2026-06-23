#include "FlightCapPairs.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <cstring>

void addrToId(const uint8_t addr[6], char out[13]) {
  snprintf(out, 13, "%02X%02X%02X%02X%02X%02X", addr[5], addr[4], addr[3], addr[2], addr[1],
           addr[0]);
}

bool idToAddr(const char *id, uint8_t out[6]) {
  if (id == nullptr || strlen(id) != 12) {
    return false;
  }
  for (uint8_t i = 0; i < 6; ++i) {
    char pair[3] = {id[i * 2], id[i * 2 + 1], '\0'};
    char *end = nullptr;
    const unsigned long byte = strtoul(pair, &end, 16);
    if (end == nullptr || *end != '\0' || byte > 0xFF) {
      return false;
    }
    out[5 - i] = static_cast<uint8_t>(byte);
  }
  return true;
}

static bool writePairsJson(tumbly::HublinkNode &node, const FlightCapPairList &list) {
  if (!node.sd().isMounted() && !node.sd().begin()) {
    return false;
  }
  StaticJsonDocument<512> doc;
  JsonArray arr = doc["pairs"].to<JsonArray>();
  for (uint8_t i = 0; i < list.count; ++i) {
    arr.add(list.ids[i]);
  }
  String text;
  serializeJson(doc, text);
  File f = SD.open(kPairsJsonPath, FILE_WRITE);
  if (!f) {
    return false;
  }
  f.print(text);
  f.close();
  return true;
}

bool flightCapPairsLoad(tumbly::HublinkNode &node, FlightCapPairList &out) {
  out.count = 0;
  memset(out.ids, 0, sizeof(out.ids));

  if (!node.readSdDetect()) {
    return false;
  }
  if (!node.sd().isMounted() && !node.sd().begin()) {
    return false;
  }
  if (!node.sd().exists(kPairsJsonPath)) {
    return true;
  }

  String text;
  if (node.sd().readText(kPairsJsonPath, text) != tumbly::ServiceStatus::Ok) {
    return false;
  }

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, text) != DeserializationError::Ok || !doc["pairs"].is<JsonArray>()) {
    return false;
  }

  JsonArrayConst arr = doc["pairs"].as<JsonArrayConst>();
  for (JsonVariantConst item : arr) {
    if (!item.is<const char *>()) {
      continue;
    }
    const char *id = item.as<const char *>();
    if (strlen(id) != 12 || out.count >= kMaxPairedDevices) {
      continue;
    }
    strncpy(out.ids[out.count], id, 12);
    out.ids[out.count][12] = '\0';
    ++out.count;
  }
  return true;
}

bool flightCapPairsSave(tumbly::HublinkNode &node, const FlightCapPairList &list) {
  if (!node.readSdDetect()) {
    return false;
  }
  if (!node.sd().isMounted() && !node.sd().begin()) {
    return false;
  }
  return writePairsJson(node, list);
}

bool flightCapPairsContains(const FlightCapPairList &list, const char *id) {
  for (uint8_t i = 0; i < list.count; ++i) {
    if (strcmp(list.ids[i], id) == 0) {
      return true;
    }
  }
  return false;
}

bool flightCapPairsAdd(tumbly::HublinkNode &node, FlightCapPairList &list, const char *id) {
  if (id == nullptr || strlen(id) != 12 || list.count >= kMaxPairedDevices) {
    return false;
  }
  if (flightCapPairsContains(list, id)) {
    return false;
  }
  strncpy(list.ids[list.count], id, 12);
  list.ids[list.count][12] = '\0';
  ++list.count;
  return flightCapPairsSave(node, list);
}

bool flightCapPairsRemoveAt(tumbly::HublinkNode &node, FlightCapPairList &list, uint8_t index) {
  if (index >= list.count) {
    return false;
  }
  for (uint8_t i = index; i + 1 < list.count; ++i) {
    memcpy(list.ids[i], list.ids[i + 1], 13);
  }
  --list.count;
  memset(list.ids[list.count], 0, 13);
  return flightCapPairsSave(node, list);
}

void flightCapPairsRemoveAll(FlightCapPairList &list) {
  list.count = 0;
  memset(list.ids, 0, sizeof(list.ids));
}

bool flightCapPairsTryAddFromAddr(tumbly::HublinkNode &node, FlightCapPairList &list,
                                  const uint8_t addr[6], char addedId[13]) {
  char id[13];
  addrToId(addr, id);
  if (flightCapPairsContains(list, id)) {
    return false;
  }
  if (!flightCapPairsAdd(node, list, id)) {
    return false;
  }
  if (addedId != nullptr) {
    strncpy(addedId, id, 13);
  }
  return true;
}
