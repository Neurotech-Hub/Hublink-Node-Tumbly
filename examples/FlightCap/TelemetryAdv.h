#pragma once

#include <stdint.h>
#include <string.h>

// flightcap-prod nRF52833 manufacturer data (see flightcap-prod README).
#ifndef TELEM_ADV_COMPANY_ID
#define TELEM_ADV_COMPANY_ID 0x4E48U // NH
#endif
#ifndef TELEM_ADV_MAGIC
#define TELEM_ADV_MAGIC 0xA5U
#endif
#ifndef TELEM_ADV_VERSION
#define TELEM_ADV_VERSION 0x02U
#endif

#ifndef FLIGHTCAP_COMPANY_ID
#define FLIGHTCAP_COMPANY_ID TELEM_ADV_COMPANY_ID
#endif
#define TELEMETRY_MAGIC TELEM_ADV_MAGIC
#define TELEMETRY_VERSION TELEM_ADV_VERSION

#define FLAG_DIST_VALID (1u << 0)
#define FLAG_INTERACT_VALID (1u << 1)
#define FLAG_TOF_ERR (1u << 2)
#define FLAG_PAIR_MODE (1u << 4) // Magnet-toggled pair/advertise-only mode

#ifndef FLIGHTCAP_DEVICE_NAME
#define FLIGHTCAP_DEVICE_NAME "FCap"
#endif

// Full BLE manufacturer-specific value (17 bytes, little-endian multi-byte fields).
#pragma pack(push, 1)
typedef struct {
  uint16_t company_id;
  uint8_t magic;
  uint8_t version;
  uint8_t device_addr[6];
  uint16_t seq;
  int16_t distance_mm;
  uint16_t interactions;
  uint8_t flags;
} TelemetryAdv;
static_assert(sizeof(TelemetryAdv) == 17, "TelemetryAdv wire size must be 17 bytes");
#pragma pack(pop)

static inline bool telemetryIsFlightCapAdv(const TelemetryAdv &adv) {
  return adv.company_id == FLIGHTCAP_COMPANY_ID && adv.magic == TELEMETRY_MAGIC &&
         adv.version == TELEMETRY_VERSION;
}

static inline bool telemetryDeviceAddrValid(const TelemetryAdv &adv) {
  for (uint8_t i = 0; i < 6; ++i) {
    if (adv.device_addr[i] != 0) {
      return true;
    }
  }
  return false;
}

static inline bool telemetryIsPairMode(const TelemetryAdv &adv) {
  return telemetryIsFlightCapAdv(adv) && telemetryDeviceAddrValid(adv) &&
         (adv.flags & FLAG_PAIR_MODE) != 0;
}

static inline bool telemetryDeviceAddrEqual(const uint8_t a[6], const uint8_t b[6]) {
  return memcmp(a, b, 6) == 0;
}

typedef struct {
  uint8_t addr[6];
  uint16_t last_seq;
  int16_t distance_mm;
  uint16_t interactions;
  uint8_t flags;
  int8_t rssi;
  uint32_t last_seen_ms;
  bool valid;
} RemoteTelemetry;
