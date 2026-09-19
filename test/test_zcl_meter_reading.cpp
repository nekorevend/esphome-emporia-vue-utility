// Unit tests for the dependency-free V7+ ZCL meter reading decoder.
//
// Build & run (from the repo root):
//   g++ -std=c++17 -I esphome/components/emporia_vue_utility -I test
//       test/test_zcl_meter_reading.cpp -o /tmp/zcltest && /tmp/zcltest
//
// (or `make -C test`)

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "zcl_meter_reading.h"

#include <vector>

using esphome::emporia_vue_utility::parse_v7_zcl;
using esphome::emporia_vue_utility::ParsedV7Reading;
using esphome::emporia_vue_utility::zcl_type_len;

namespace {

// Helper: decode a byte vector.
ParsedV7Reading decode(const std::vector<uint8_t> &bytes) {
  return parse_v7_zcl(bytes.data(), bytes.size());
}

}  // namespace

TEST_CASE("full 44-byte frame: import, export, and demand all present") {
  const std::vector<uint8_t> frame = {
      0x18, 0x34, 0x01, 0x00, 0x00, 0x00, 0x25, 0x7A, 0x91, 0x55, 0x01,
      0x00, 0x00, 0x01, 0x00, 0x00, 0x25, 0x33, 0x68, 0x63, 0x01, 0x00,
      0x00, 0x01, 0x03, 0x00, 0x22, 0x01, 0x00, 0x00, 0x02, 0x03, 0x00,
      0x22, 0xE8, 0x03, 0x00, 0x00, 0x04, 0x00, 0x2A, 0xA7, 0x04, 0x00};

  ParsedV7Reading r = decode(frame);

  CHECK(r.ok);
  CHECK(r.import_present);
  CHECK(r.export_present);
  CHECK(r.watts_present);
  CHECK(r.multiplier_present);
  CHECK(r.divisor_present);

  CHECK(r.watts == doctest::Approx(1191));
  CHECK(r.import_wh == doctest::Approx(22385018));
  CHECK(r.export_wh == doctest::Approx(23291956));
  CHECK(r.net_wh() == doctest::Approx(-906938));
  CHECK(r.multiplier == 1);
  CHECK(r.divisor == 1000);
  CHECK_FALSE(r.is_unscaled());
}

TEST_CASE("37-byte frame: export unsupported (status 0x86), shorter payload") {
  const std::vector<uint8_t> frame = {
      0x18, 0xB7, 0x01, 0x00, 0x00, 0x00, 0x25, 0xFA, 0xDB, 0x04, 0x00, 0x00,
      0x00, 0x01, 0x00, 0x86, 0x01, 0x03, 0x00, 0x22, 0x01, 0x00, 0x00, 0x02,
      0x03, 0x00, 0x22, 0xE8, 0x03, 0x00, 0x00, 0x04, 0x00, 0x2A, 0xB8, 0x01,
      0x00};

  ParsedV7Reading r = decode(frame);

  CHECK(r.ok);
  CHECK(r.import_present);
  CHECK_FALSE(r.export_present);
  CHECK(r.watts_present);

  CHECK(r.watts == doctest::Approx(440));
  CHECK(r.import_wh == doctest::Approx(318458));
  CHECK(r.net_wh() == doctest::Approx(318458));  // export treated as 0
  CHECK(r.multiplier == 1);
  CHECK(r.divisor == 1000);
  CHECK_FALSE(r.is_unscaled());
}

TEST_CASE("all-zero frame: every attribute present but 0 => unscaled") {
  // Some meters send this ahead of the real reading. Structurally valid, but
  // the zero Multiplier/Divisor means it carries no usable data.
  const std::vector<uint8_t> frame = {
      0x18, 0x1A, 0x01,                                            // ZCL header
      0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // import
      0x01, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // export
      0x01, 0x03, 0x00, 0x22, 0x00, 0x00, 0x00,                    // multiplier
      0x02, 0x03, 0x00, 0x22, 0x00, 0x00, 0x00,                    // divisor
      0x00, 0x04, 0x00, 0x2A, 0x00, 0x00, 0x00};                   // demand

  ParsedV7Reading r = decode(frame);

  CHECK(r.ok);
  CHECK(r.import_present);
  CHECK(r.export_present);
  CHECK(r.watts_present);
  CHECK(r.multiplier_present);
  CHECK(r.divisor_present);
  CHECK(r.is_unscaled());
}

TEST_CASE("defensive: truncated frame (value overruns) => not ok") {
  // Full frame with the trailing demand value byte chopped off, so the last
  // record's declared 3-byte int24 value overruns the payload end.
  std::vector<uint8_t> frame = {
      0x18, 0x34, 0x01, 0x00, 0x00, 0x00, 0x25, 0x7A, 0x91, 0x55, 0x01,
      0x00, 0x00, 0x01, 0x00, 0x00, 0x25, 0x33, 0x68, 0x63, 0x01, 0x00,
      0x00, 0x01, 0x03, 0x00, 0x22, 0x01, 0x00, 0x00, 0x02, 0x03, 0x00,
      0x22, 0xE8, 0x03, 0x00, 0x00, 0x04, 0x00, 0x2A, 0xA7, 0x04, 0x00};
  frame.pop_back();  // drop one byte of the demand value

  ParsedV7Reading r = decode(frame);
  CHECK_FALSE(r.ok);
}

TEST_CASE("defensive: unknown data type tag => not ok") {
  // Same as the 44-byte frame but the import attribute's type tag (offset 6) is
  // corrupted to an unrecognized value, so the record length is unknowable.
  std::vector<uint8_t> frame = {
      0x18, 0x34, 0x01, 0x00, 0x00, 0x00, 0x25, 0x7A, 0x91, 0x55, 0x01,
      0x00, 0x00, 0x01, 0x00, 0x00, 0x25, 0x33, 0x68, 0x63, 0x01, 0x00,
      0x00, 0x01, 0x03, 0x00, 0x22, 0x01, 0x00, 0x00, 0x02, 0x03, 0x00,
      0x22, 0xE8, 0x03, 0x00, 0x00, 0x04, 0x00, 0x2A, 0xA7, 0x04, 0x00};
  frame[6] = 0xFF;  // unknown ZCL type tag

  ParsedV7Reading r = decode(frame);
  CHECK_FALSE(r.ok);
}

TEST_CASE("defensive: wrong command id => not ok") {
  std::vector<uint8_t> frame = {0x18, 0x34, 0x02 /* not a read attr resp */};
  ParsedV7Reading r = decode(frame);
  CHECK_FALSE(r.ok);
}

TEST_CASE("missing Multiplier/Divisor: parses, but values are left unscaled") {
  // Only the import summation and instantaneous demand are present; there are no
  // Multiplier (0x0301) or Divisor (0x0302) records. The frame is structurally
  // valid (ok), but without a scale factor the raw values cannot be converted,
  // so they are left at 0 and the caller (component) reports the error.
  const std::vector<uint8_t> frame = {
      0x18, 0x34, 0x01,                                      // ZCL header
      0x00, 0x00, 0x00, 0x25, 0x7A, 0x91, 0x55, 0x01, 0x00, 0x00,  // import
      0x00, 0x04, 0x00, 0x2A, 0xA7, 0x04, 0x00};                   // demand

  ParsedV7Reading r = decode(frame);

  CHECK(r.ok);
  CHECK_FALSE(r.multiplier_present);
  CHECK_FALSE(r.divisor_present);
  CHECK(r.import_present);
  CHECK(r.watts_present);
  CHECK(r.import_wh == doctest::Approx(0));  // unscaled: no Multiplier/Divisor
  CHECK(r.watts == doctest::Approx(0));
  // Missing attributes are an error, not an ignorable unscaled reading.
  CHECK_FALSE(r.is_unscaled());
}

TEST_CASE("zcl_type_len covers the metering types and rejects unknowns") {
  CHECK(zcl_type_len(0x22) == 3);  // uint24
  CHECK(zcl_type_len(0x25) == 6);  // uint48
  CHECK(zcl_type_len(0x2A) == 3);  // int24
  CHECK(zcl_type_len(0x00) == 0);  // unknown
  CHECK(zcl_type_len(0xFF) == 0);  // unknown
}
