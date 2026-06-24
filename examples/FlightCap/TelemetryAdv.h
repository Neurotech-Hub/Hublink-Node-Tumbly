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
#ifndef TELEM_ADV_VERSION_MIN
#define TELEM_ADV_VERSION_MIN 0x02U
#endif
#ifndef TELEM_ADV_VERSION
#define TELEM_ADV_VERSION 0x03U
#endif

#ifndef FLIGHTCAP_COMPANY_ID
#define FLIGHTCAP_COMPANY_ID TELEM_ADV_COMPANY_ID
#endif
#define TELEMETRY_MAGIC TELEM_ADV_MAGIC
#define TELEMETRY_VERSION TELEM_ADV_VERSION

#define TELEM_ADV_V02_SIZE 17U

#define FLAG_DIST_VALID (1u << 0)
#define FLAG_INTERACT_VALID (1u << 1)
#define FLAG_TOF_ERR (1u << 2)
#define FLAG_PAIR_MODE (1u << 4) // Magnet-toggled pair/advertise-only mode
#define FLAG_VBATT_VALID (1u << 5)

#ifndef FLIGHTCAP_DEVICE_NAME
#define FLIGHTCAP_DEVICE_NAME "FCap"
#endif

// Full BLE manufacturer-specific value (19 bytes v0x03, little-endian multi-byte fields).
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
  uint16_t vbatt_mv;
} TelemetryAdv;
static_assert(sizeof(TelemetryAdv) == 19, "TelemetryAdv wire size must be 19 bytes");
#pragma pack(pop)

static inline bool telemetryIsSupportedVersion(uint8_t version) {
  return version >= TELEM_ADV_VERSION_MIN && version <= TELEM_ADV_VERSION;
}

static inline bool telemetryDeviceAddrValid(const TelemetryAdv &adv) {
  for (uint8_t i = 0; i < 6; ++i) {
    if (adv.device_addr[i] != 0) {
      return true;
    }
  }
  return false;
}

static inline bool telemetryIsFlightCapAdv(const TelemetryAdv &adv) {
  return adv.company_id == FLIGHTCAP_COMPANY_ID && adv.magic == TELEMETRY_MAGIC &&
         telemetryIsSupportedVersion(adv.version) && telemetryDeviceAddrValid(adv);
}

static inline bool telemetryVbattValid(const TelemetryAdv &adv) {
  return (adv.flags & FLAG_VBATT_VALID) != 0;
}

static inline bool telemetryIsPairMode(const TelemetryAdv &adv) {
  return telemetryIsFlightCapAdv(adv) && (adv.flags & FLAG_PAIR_MODE) != 0;
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
