#pragma once

#include <stdint.h>

// flightcap-prod nRF52833 manufacturer data (see flightcap-prod README).
#ifndef FLIGHTCAP_COMPANY_ID
#define FLIGHTCAP_COMPANY_ID 0x4E48U // Neurotech Hub provisional ('N' | 'H'<<8)
#endif

#define TELEMETRY_MAGIC 0xA5
#define TELEMETRY_VERSION 0x01

#define FLAG_DIST_VALID (1u << 0)
#define FLAG_INTERACT_VALID (1u << 1)
#define FLAG_TOF_ERR (1u << 2)
#define FLAG_PAIR_MODE (1u << 4) // Magnet-toggled pair menu; not run telemetry

// Full BLE manufacturer-specific value: company_id + telemetry (11 bytes, little-endian).
#pragma pack(push, 1)
typedef struct {
  uint16_t company_id;
  uint8_t magic;
  uint8_t version;
  uint16_t seq;
  int16_t distance_mm;
  uint16_t interactions;
  uint8_t flags;
} TelemetryAdv;
static_assert(sizeof(TelemetryAdv) == 11, "TelemetryAdv wire size must be 11 bytes");
#pragma pack(pop)

static inline bool telemetryIsPairMode(const TelemetryAdv &adv) {
  return (adv.flags & FLAG_PAIR_MODE) != 0;
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
