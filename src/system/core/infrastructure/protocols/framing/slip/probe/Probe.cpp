/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built on
 *        every platform the lib.manifest declares — under each cross
 *        toolchain on MCU builds, at each declared posix_cpp dialect on
 *        hosted builds — so a regression against the support contract fails
 *        the build that owns the claim.
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
