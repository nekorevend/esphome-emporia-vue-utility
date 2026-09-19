#pragma once

// Dependency-free decoder for the V7+ meter reading payload.
//
// The V7+ (MGM firmware >= 7) payload is a standard Zigbee Cluster Library (ZCL)
// "Read Attributes Response" frame for the Simple Metering cluster (0x0702).  See
// docs/protocol-meter-reading.md for the full wire format.
//
// This header intentionally has NO ESPHome (or any other) dependencies beyond the
// C++ standard integer types, so it can be compiled and unit-tested on the host.
// Do not add ESPHome includes here.

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace emporia_vue_utility {

// ---- ZCL frame constants -------------------------------------------------

static constexpr uint8_t ZCL_CMD_READ_ATTR_RESPONSE = 0x01;
static constexpr uint8_t ZCL_STATUS_SUCCESS = 0x00;

// ZCL data type tags.  Only the widths matter to us; signedness is handled per
// attribute (only InstantaneousDemand is signed).
static constexpr uint8_t ZCL_TYPE_UINT8 = 0x20;
static constexpr uint8_t ZCL_TYPE_UINT16 = 0x21;
static constexpr uint8_t ZCL_TYPE_UINT24 = 0x22;
static constexpr uint8_t ZCL_TYPE_UINT32 = 0x23;
static constexpr uint8_t ZCL_TYPE_UINT40 = 0x24;
static constexpr uint8_t ZCL_TYPE_UINT48 = 0x25;
static constexpr uint8_t ZCL_TYPE_INT8 = 0x28;
static constexpr uint8_t ZCL_TYPE_INT16 = 0x29;
static constexpr uint8_t ZCL_TYPE_INT24 = 0x2A;
static constexpr uint8_t ZCL_TYPE_INT32 = 0x2B;

// Simple Metering cluster (0x0702) attribute identifiers.
static constexpr uint16_t ATTR_SUMMATION_DELIVERED = 0x0000;  // ImportWh
static constexpr uint16_t ATTR_SUMMATION_RECEIVED = 0x0001;   // ExportWh
static constexpr uint16_t ATTR_MULTIPLIER = 0x0301;           // ZCL Multiplier
static constexpr uint16_t ATTR_DIVISOR = 0x0302;              // ZCL Divisor
static constexpr uint16_t ATTR_INSTANTANEOUS_DEMAND = 0x0400;  // PowerVal / watts

// Returns the value width in bytes for a ZCL data type tag, or 0 if the type is
// unknown/unsupported (in which case we cannot know the record length and must
// stop parsing).
inline size_t zcl_type_len(uint8_t type) {
  switch (type) {
    case ZCL_TYPE_UINT8:
    case ZCL_TYPE_INT8:
      return 1;
    case ZCL_TYPE_UINT16:
    case ZCL_TYPE_INT16:
      return 2;
    case ZCL_TYPE_UINT24:
    case ZCL_TYPE_INT24:
      return 3;
    case ZCL_TYPE_UINT32:
    case ZCL_TYPE_INT32:
      return 4;
    case ZCL_TYPE_UINT40:
      return 5;
    case ZCL_TYPE_UINT48:
      return 6;
    default:
      return 0;
  }
}

// Sign-extend an n-byte little-endian value already assembled into `v`.
inline int64_t zcl_sign_extend(uint64_t v, size_t nbytes) {
  const size_t bits = nbytes * 8;
  const uint64_t sign_bit = uint64_t(1) << (bits - 1);
  return int64_t((v ^ sign_bit) - sign_bit);
}

// Scale a raw metering value into watts / watt-hours using the ZCL Metering
// Multiplier/Divisor.  Values are kept in watts (not kilowatts): the project
// treats the base unit as watts, so the usual `raw * Multiplier / Divisor`
// (which yields kW when Divisor == 1000) is turned into watts by dividing the
// Divisor by 1000 first.  `double` is used because summation counters routinely
// exceed the ~16.7M exact-integer range of float32.
inline double zcl_apply_adjustment(int64_t raw, uint32_t multiplier,
                                   uint32_t divisor) {
  return double(raw) * double(multiplier) / (double(divisor) / 1000.0);
}

// Result of decoding one V7+ payload.  Each value has a `*_present` flag because
// any attribute may be reported with a non-SUCCESS status (e.g. a meter with no
// solar/export returns 0x86 UNSUPPORTED_ATTRIBUTE for CurrentSummationReceived),
// in which case that record carries no value and the payload is shorter.
struct ParsedV7Reading {
  bool ok = true;  // false => malformed frame / unknown type / not a read response

  bool import_present = false;
  double import_wh = 0;  // watt-hours

  bool export_present = false;
  double export_wh = 0;  // watt-hours

  bool watts_present = false;
  double watts = 0;  // watts

  bool multiplier_present = false;
  uint32_t multiplier = 0;  // raw ZCL Multiplier (uint24)

  bool divisor_present = false;
  uint32_t divisor = 0;  // raw ZCL Divisor (uint24)

  // Net energy = import - export.  When export is absent it counts as zero, so
  // net collapses to import.
  double net_wh() const {
    return import_wh - (export_present ? export_wh : 0.0);
  }

  // True when the meter reports a zero Multiplier or Divisor.  Some meters send
  // such a reading (with every other value also 0) ahead of the real one.  It
  // cannot be scaled, so it carries no usable data and should be ignored.
  bool is_unscaled() const {
    return multiplier_present && divisor_present &&
           (multiplier == 0 || divisor == 0);
  }
};

// Decode a ZCL Read Attributes Response payload (starting at the ZCL Frame
// Control byte) into a ParsedV7Reading.
//
// The Multiplier and Divisor attributes are required to scale the readings.
// Real meters send them in every reading, so their absence is treated as an
// error by the caller (see multiplier_present / divisor_present); no value is
// assumed on their behalf.
inline ParsedV7Reading parse_v7_zcl(const uint8_t *payload, size_t len) {
  ParsedV7Reading r;

  if (len < 3) {
    r.ok = false;
    return r;
  }
  if (payload[2] != ZCL_CMD_READ_ATTR_RESPONSE) {
    r.ok = false;
    return r;
  }

  bool import_present = false;
  uint64_t import_raw = 0;
  bool export_present = false;
  uint64_t export_raw = 0;
  bool demand_present = false;
  int64_t demand_raw = 0;

  // Walk read-attribute-status records.
  size_t p = 3;
  while (p + 3 <= len) {  // need at least attr id (2) + status (1)
    uint16_t attr_id = uint16_t(payload[p]) | (uint16_t(payload[p + 1]) << 8);
    uint8_t status = payload[p + 2];

    if (status != ZCL_STATUS_SUCCESS) {
      // Unsupported/failed attribute: no type or value follows.  Normal, not an
      // error (e.g. 0x86 for export on meters without solar).
      p += 3;
      continue;
    }

    // A SUCCESS record must carry a type byte and its value.
    if (p + 4 > len) {  // no room for the type byte
      r.ok = false;
      return r;
    }
    uint8_t type = payload[p + 3];
    size_t vlen = zcl_type_len(type);
    if (vlen == 0) {  // unknown type => cannot know the record length
      r.ok = false;
      return r;
    }
    if (p + 4 + vlen > len) {  // value would overrun the payload
      r.ok = false;
      return r;
    }

    const uint8_t *val = &payload[p + 4];
    uint64_t uval = 0;
    for (size_t i = 0; i < vlen; i++) {
      uval |= uint64_t(val[i]) << (8 * i);  // little-endian
    }

    switch (attr_id) {
      case ATTR_SUMMATION_DELIVERED:
        import_present = true;
        import_raw = uval;
        break;
      case ATTR_SUMMATION_RECEIVED:
        export_present = true;
        export_raw = uval;
        break;
      case ATTR_MULTIPLIER:
        r.multiplier_present = true;
        r.multiplier = uint32_t(uval);
        break;
      case ATTR_DIVISOR:
        r.divisor_present = true;
        r.divisor = uint32_t(uval);
        break;
      case ATTR_INSTANTANEOUS_DEMAND:
        demand_present = true;
        demand_raw = zcl_sign_extend(uval, vlen);
        break;
      default:
        break;  // ignore unknown attributes (forward-compatible)
    }

    p += 4 + vlen;
  }

  r.import_present = import_present;
  r.export_present = export_present;
  r.watts_present = demand_present;

  // Scale the raw values into watts / watt-hours using this frame's
  // Multiplier/Divisor.  Both are required; when either is missing (or the
  // divisor is zero) the caller treats the reading as an error, so the values
  // are simply left unscaled at 0 here rather than guessing.
  if (r.multiplier_present && r.divisor_present && r.divisor != 0) {
    if (import_present) {
      r.import_wh =
          zcl_apply_adjustment(int64_t(import_raw), r.multiplier, r.divisor);
    }
    if (export_present) {
      r.export_wh =
          zcl_apply_adjustment(int64_t(export_raw), r.multiplier, r.divisor);
    }
    if (demand_present) {
      r.watts = zcl_apply_adjustment(demand_raw, r.multiplier, r.divisor);
    }
  }

  return r;
}

}  // namespace emporia_vue_utility
}  // namespace esphome
