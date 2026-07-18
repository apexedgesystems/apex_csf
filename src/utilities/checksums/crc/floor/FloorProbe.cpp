/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
 */

#include "src/utilities/checksums/crc/inc/Crc.hpp"

using namespace apex::checksums::crc;

uint32_t probe() {
  static const uint8_t DATA[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

  Crc16XmodemTable table;
  uint16_t crcTable = 0;
  Status s1 = table.calculate(DATA, sizeof(DATA), crcTable);

  Crc16XmodemBitwise bitwise;
  uint16_t crcBitwise = 0;
  Status s2 = bitwise.update(DATA, sizeof(DATA));
  Status s3 = bitwise.finalize(crcBitwise);

  Crc8SmbusNibble nibble;
  uint8_t crc8 = 0;
  Status s4 = nibble.calculate(apex::compat::bytes_span{DATA, sizeof(DATA)}, crc8);

  Crc32IsoHdlcTable crc32;
  uint32_t crc32Out = 0;
  Status s5 = crc32.calculate(DATA, sizeof(DATA), crc32Out);

  return static_cast<uint32_t>(crcTable) + crcBitwise + crc8 + crc32Out +
         static_cast<uint32_t>(s1) + static_cast<uint32_t>(s2) + static_cast<uint32_t>(s3) +
         static_cast<uint32_t>(s4) + static_cast<uint32_t>(s5);
}
