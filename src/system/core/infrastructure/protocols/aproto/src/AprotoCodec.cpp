/**
 * @file AprotoCodec.cpp
 * @brief Implementation of APROTO encode/decode functions.
 */

#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoCodec.hpp"

#include "src/utilities/checksums/crc/inc/Crc.hpp"

#include <cstring>

namespace system_core {
namespace protocols {
namespace aproto {

/* ----------------------------- Decode API ------------------------------- */

Status decodeHeader(apex::compat::rospan<std::uint8_t> buf, AprotoHeader& out) noexcept {
  // Check minimum size
  if (buf.size() < APROTO_HEADER_SIZE) {
    return Status::ERROR_INCOMPLETE;
  }

  // Copy header bytes (14 bytes, no alignment issues due to packed struct)
  std::memcpy(&out, buf.data(), APROTO_HEADER_SIZE);

  // Validate magic
  if (out.magic != APROTO_MAGIC) {
    return Status::ERROR_INVALID_MAGIC;
  }

  // Validate version
  if (out.version != APROTO_VERSION) {
    return Status::ERROR_INVALID_VERSION;
  }

  return Status::SUCCESS;
}

Status validatePacket(apex::compat::rospan<std::uint8_t> packet) noexcept {
  // Decode and validate header
  AprotoHeader hdr{};
  Status st = decodeHeader(packet, hdr);
  if (!isSuccess(st)) {
    return st;
  }

  // Check we have full payload (including crypto metadata if encrypted)
  const std::size_t expectedSize = packetSize(hdr);
  if (packet.size() < expectedSize) {
    return Status::ERROR_PAYLOAD_TRUNCATED;
  }

  // If CRC present, validate it
  // CRC covers everything before the CRC trailer: header + [cryptoMeta] + payload
  if (hdr.flags.crcPresent) {
    std::size_t dataLen = APROTO_HEADER_SIZE;
    if (hdr.flags.encryptedPresent) {
      dataLen += APROTO_CRYPTO_META_SIZE;
    }
    dataLen += hdr.payloadLength;

    const std::uint32_t computed = computeCrc(packet.subspan(0, dataLen));

    // Read stored CRC (little-endian)
    std::uint32_t stored = 0;
    const std::uint8_t* crcPtr = packet.data() + dataLen;
    stored |= static_cast<std::uint32_t>(crcPtr[0]);
    stored |= static_cast<std::uint32_t>(crcPtr[1]) << 8;
    stored |= static_cast<std::uint32_t>(crcPtr[2]) << 16;
    stored |= static_cast<std::uint32_t>(crcPtr[3]) << 24;

    if (computed != stored) {
      return Status::ERROR_CRC_MISMATCH;
    }
  }

  return Status::SUCCESS;
}

apex::compat::rospan<std::uint8_t> getPayload(apex::compat::rospan<std::uint8_t> packet) noexcept {
  if (packet.size() <= APROTO_HEADER_SIZE) {
    return {};
  }

  // Decode header to get payload length and flags
  AprotoHeader hdr{};
  if (!isSuccess(decodeHeader(packet, hdr))) {
    return {};
  }

  if (hdr.payloadLength == 0) {
    return {};
  }

  // Payload starts after header, and after crypto metadata if encrypted
  std::size_t payloadOffset = APROTO_HEADER_SIZE;
  if (hdr.flags.encryptedPresent) {
    payloadOffset += APROTO_CRYPTO_META_SIZE;
  }

  if (packet.size() <= payloadOffset) {
    return {};
  }

  const std::size_t availPayload = packet.size() - payloadOffset;
  const std::size_t payloadLen =
      (hdr.payloadLength <= availPayload) ? hdr.payloadLength : availPayload;

  return packet.subspan(payloadOffset, payloadLen);
}

Status getCryptoMeta(apex::compat::rospan<std::uint8_t> packet, CryptoMeta& meta) noexcept {
  // Decode header
  AprotoHeader hdr{};
  if (!isSuccess(decodeHeader(packet, hdr))) {
    return Status::ERROR_INCOMPLETE;
  }

  // Check if encrypted
  if (!hdr.flags.encryptedPresent) {
    return Status::ERROR_MISSING_CRYPTO;
  }

  // Check buffer has crypto metadata
  if (packet.size() < APROTO_HEADER_SIZE + APROTO_CRYPTO_META_SIZE) {
    return Status::ERROR_INCOMPLETE;
  }

  // Copy crypto metadata
  std::memcpy(&meta, packet.data() + APROTO_HEADER_SIZE, APROTO_CRYPTO_META_SIZE);

  return Status::SUCCESS;
}

/* ----------------------------- Encode API ------------------------------- */

AprotoHeader buildHeader(std::uint32_t fullUid, std::uint16_t opcode, std::uint16_t sequence,
                         std::uint16_t payloadLen, bool isResponse, bool ackRequested,
                         bool includeCrc) noexcept {
  AprotoHeader hdr{};
  hdr.magic = APROTO_MAGIC;
  hdr.version = APROTO_VERSION;
  hdr.flags = makeFlags(isResponse, ackRequested, includeCrc);
  hdr.fullUid = fullUid;
  hdr.opcode = opcode;
  hdr.sequence = sequence;
  hdr.payloadLength = payloadLen;
  return hdr;
}

Status encodeHeader(const AprotoHeader& hdr, apex::compat::mutable_bytes_span outBuf) noexcept {
  if (outBuf.size() < APROTO_HEADER_SIZE) {
    return Status::ERROR_BUFFER_TOO_SMALL;
  }

  std::memcpy(outBuf.data(), &hdr, APROTO_HEADER_SIZE);
  return Status::SUCCESS;
}

Status encodePacket(const AprotoHeader& hdr, apex::compat::rospan<std::uint8_t> payload,
                    apex::compat::mutable_bytes_span outBuf, std::size_t& bytesWritten) noexcept {
  bytesWritten = 0;

  // Validate payload size matches header
  if (payload.size() != hdr.payloadLength) {
    return Status::ERROR_PAYLOAD_TOO_LARGE;
  }

  // Calculate total size
  const std::size_t totalSize = packetSize(hdr);
  if (outBuf.size() < totalSize) {
    return Status::ERROR_BUFFER_TOO_SMALL;
  }

  // Write header
  std::memcpy(outBuf.data(), &hdr, APROTO_HEADER_SIZE);
  std::size_t offset = APROTO_HEADER_SIZE;

  // Write payload
  if (!payload.empty()) {
    std::memcpy(outBuf.data() + offset, payload.data(), payload.size());
    offset += payload.size();
  }

  // Append CRC if requested
  if (hdr.flags.crcPresent) {
    const std::uint32_t crc = computeCrc(outBuf.subspan(0, offset));

    // Write little-endian
    outBuf[offset + 0] = static_cast<std::uint8_t>(crc & 0xFF);
    outBuf[offset + 1] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
    outBuf[offset + 2] = static_cast<std::uint8_t>((crc >> 16) & 0xFF);
    outBuf[offset + 3] = static_cast<std::uint8_t>((crc >> 24) & 0xFF);
    offset += APROTO_CRC_SIZE;
  }

  bytesWritten = offset;
  return Status::SUCCESS;
}

Status encodeAckNak(const AprotoHeader& cmdHeader, std::uint8_t statusCode,
                    apex::compat::mutable_bytes_span outBuf, std::size_t& bytesWritten,
                    bool includeCrc) noexcept {
  bytesWritten = 0;

  // Build ACK/NAK payload
  AckPayload ackPayload{};
  ackPayload.cmdOpcode = cmdHeader.opcode;
  ackPayload.cmdSequence = cmdHeader.sequence;
  ackPayload.status = statusCode;
  ackPayload.reserved[0] = 0;
  ackPayload.reserved[1] = 0;
  ackPayload.reserved[2] = 0;

  // Build response header
  const std::uint16_t respOpcode = (statusCode == 0)
                                       ? static_cast<std::uint16_t>(SystemOpcode::ACK)
                                       : static_cast<std::uint16_t>(SystemOpcode::NAK);

  AprotoHeader respHdr =
      buildHeader(cmdHeader.fullUid, respOpcode, cmdHeader.sequence,
                  static_cast<std::uint16_t>(sizeof(AckPayload)), true, // isResponse
                  false,                                                // ackRequested
                  includeCrc);

  // Encode packet
  apex::compat::rospan<std::uint8_t> payloadSpan(reinterpret_cast<const std::uint8_t*>(&ackPayload),
                                                 sizeof(AckPayload));

  return encodePacket(respHdr, payloadSpan, outBuf, bytesWritten);
}

/* ---------------------------- CRC Utilities ----------------------------- */

std::uint32_t computeCrc(apex::compat::rospan<std::uint8_t> data) noexcept {
  // Use hardware-accelerated CRC-32C (iSCSI) when available
  apex::checksums::crc::Crc32Iscsi crc;
  std::uint32_t result = 0;
  // Note: calculate() accepts const uint8_t*, which is compatible with rospan
  crc.calculate(data.data(), data.size(), result);
  return result;
}

Status appendCrc(apex::compat::rospan<std::uint8_t> data, apex::compat::mutable_bytes_span outBuf,
                 std::size_t offset) noexcept {
  if (outBuf.size() < offset + APROTO_CRC_SIZE) {
    return Status::ERROR_BUFFER_TOO_SMALL;
  }

  const std::uint32_t crc = computeCrc(data);

  // Write little-endian
  outBuf[offset + 0] = static_cast<std::uint8_t>(crc & 0xFF);
  outBuf[offset + 1] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
  outBuf[offset + 2] = static_cast<std::uint8_t>((crc >> 16) & 0xFF);
  outBuf[offset + 3] = static_cast<std::uint8_t>((crc >> 24) & 0xFF);

  return Status::SUCCESS;
}

/* ---------------------------- Packet Viewer ----------------------------- */

Status createPacketView(apex::compat::rospan<std::uint8_t> packet, PacketView& view) noexcept {
  // Validate packet
  Status st = validatePacket(packet);
  if (!isSuccess(st)) {
    return st;
  }

  // Decode header
  st = decodeHeader(packet, view.header);
  if (!isSuccess(st)) {
    return st;
  }

  // Set views
  view.raw = packet;
  view.payload = getPayload(packet);

  return Status::SUCCESS;
}

} // namespace aproto
} // namespace protocols
} // namespace system_core
