/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
 */

#include "src/system/core/infrastructure/protocols/framing/slip/inc/SlipCodec.hpp"

using namespace apex::protocols::slip;

size_t probe() {
  static const uint8_t MSG[] = {0x01, 0xC0, 0x02, 0xDB, 0x03};

  SlipCodec<32> codec;
  IoResult enc = codec.encode(apex::compat::bytes_span{MSG, sizeof(MSG)});

  IoResult dec = codec.feedDecode(apex::compat::bytes_span{codec.encodeBuf(), enc.bytesProduced});
  apex::compat::bytes_span payload = codec.decodedPayload();

  return enc.bytesProduced + dec.bytesConsumed + payload.size() +
         static_cast<size_t>(enc.status == Status::OK) + static_cast<size_t>(dec.frameCompleted);
}
