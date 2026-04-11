#ifndef APEX_SYSTEM_CORE_PROTOCOLS_APROTO_TYPES_HPP
#define APEX_SYSTEM_CORE_PROTOCOLS_APROTO_TYPES_HPP
/**
 * @file AprotoTypes.hpp
 * @brief Core type definitions for APROTO command/telemetry protocol.
 *
 * APROTO (Apex Protocol) is a lightweight binary protocol for commanding
 * system components and receiving telemetry. It provides:
 * - 14-byte fixed header for fast parsing
 * - Component-addressed routing via fullUid
 * - Optional CRC32 integrity checking
 * - Optional AEAD encryption (AES-256-GCM or ChaCha20-Poly1305)
 * - ACK/NAK response correlation
 *
 * @note RT-safe: All types are POD with compile-time sizes.
 */

#include <cstddef>
#include <cstdint>

namespace system_core {
namespace protocols {
namespace aproto {

/* ------------------------------ Constants ------------------------------- */

/// Protocol magic bytes: "AP" in little-endian (0x50, 0x41)
constexpr std::uint16_t APROTO_MAGIC = 0x5041;

/// Current protocol version
constexpr std::uint8_t APROTO_VERSION = 1;

/// Fixed header size in bytes
constexpr std::size_t APROTO_HEADER_SIZE = 14;

/// Maximum payload size (limited by 16-bit payloadLength field)
constexpr std::size_t APROTO_MAX_PAYLOAD = 65535;

/// CRC32 trailer size when crcPresent flag is set
constexpr std::size_t APROTO_CRC_SIZE = 4;

/// ACK/NAK payload size
constexpr std::size_t APROTO_ACK_PAYLOAD_SIZE = 8;

/// Encryption metadata size: keyIndex(1) + nonce(12) = 13 bytes
constexpr std::size_t APROTO_CRYPTO_META_SIZE = 13;

/// Nonce/IV size for AEAD algorithms (AES-GCM, ChaCha20-Poly1305)
constexpr std::size_t APROTO_NONCE_SIZE = 12;

/// Authentication tag size for AEAD algorithms
constexpr std::size_t APROTO_AUTH_TAG_SIZE = 16;

/* ----------------------------- AprotoFlags ------------------------------ */

/**
 * @struct AprotoFlags
 * @brief Protocol flags packed into single byte.
 *
 * Bit layout (LSB first):
 *   [0]   internalOrigin  - 0=external (socket), 1=internal (model-to-model)
 *   [1]   isResponse      - 0=command/request, 1=response/telemetry
 *   [2]   ackRequested    - 1=sender expects ACK/NAK response
 *   [3]   crcPresent      - 1=4-byte CRC32 follows encrypted payload (or plaintext if not
 * encrypted) [4]   encryptedPresent - 1=payload is AEAD encrypted; crypto metadata precedes payload
 *   [5:7] reserved1       - Reserved, must be 0
 *
 * When encryptedPresent=1:
 *   - CryptoMeta (13 bytes) immediately follows header
 *   - Payload is ciphertext with AUTH_TAG appended (16 bytes)
 *   - AAD for AEAD = header bytes (14 bytes)
 *   - If crcPresent=1, CRC32 covers header + cryptoMeta + ciphertext + authTag
 *
 * @note RT-safe: POD type.
 */
struct AprotoFlags {
  std::uint8_t internalOrigin : 1;   ///< 0=external (socket), 1=internal (model-to-model)
  std::uint8_t isResponse : 1;       ///< 0=command, 1=response/telemetry
  std::uint8_t ackRequested : 1;     ///< 1=expects ACK/NAK
  std::uint8_t crcPresent : 1;       ///< 1=CRC32 trailer present
  std::uint8_t encryptedPresent : 1; ///< 1=AEAD encrypted payload with crypto metadata
  std::uint8_t reserved1 : 3;        ///< Reserved, must be 0
};

static_assert(sizeof(AprotoFlags) == 1, "AprotoFlags must be 1 byte");

/* ----------------------------- AprotoHeader ----------------------------- */

/**
 * @struct AprotoHeader
 * @brief Fixed 14-byte packet header.
 *
 * Wire format (little-endian):
 *   Offset  Size  Field
 *   0       2     magic         - APROTO_MAGIC (0x5041)
 *   2       1     version       - APROTO_VERSION (1)
 *   3       1     flags         - AprotoFlags
 *   4       4     fullUid       - Target component (componentId << 8 | instanceIndex)
 *   8       2     opcode        - Component-specific operation
 *   10      2     sequence      - Sequence number for correlation
 *   12      2     payloadLength - Payload size in bytes
 *
 * @note RT-safe: POD type, packed for wire transmission.
 */
struct AprotoHeader {
  std::uint16_t magic;         ///< APROTO_MAGIC
  std::uint8_t version;        ///< APROTO_VERSION
  AprotoFlags flags;           ///< Protocol flags
  std::uint32_t fullUid;       ///< Target component fullUid
  std::uint16_t opcode;        ///< Component-specific operation code
  std::uint16_t sequence;      ///< Sequence number for ACK correlation
  std::uint16_t payloadLength; ///< Payload size in bytes
} __attribute__((packed));

static_assert(sizeof(AprotoHeader) == 14, "AprotoHeader must be 14 bytes");

/* ------------------------------ AckPayload ------------------------------ */

/**
 * @struct AckPayload
 * @brief Standard ACK/NAK response payload.
 *
 * Sent in response to commands with ackRequested=1.
 *
 * Wire format:
 *   Offset  Size  Field
 *   0       2     cmdOpcode   - Original command opcode
 *   2       2     cmdSequence - Original command sequence
 *   4       1     status      - 0=success, nonzero=error code
 *   5       3     reserved    - Reserved, set to 0
 *
 * @note RT-safe: POD type, packed for wire transmission.
 */
struct AckPayload {
  std::uint16_t cmdOpcode;   ///< Original command opcode
  std::uint16_t cmdSequence; ///< Original command sequence
  std::uint8_t status;       ///< 0=success, nonzero=error
  std::uint8_t reserved[3];  ///< Reserved, set to 0
} __attribute__((packed));

static_assert(sizeof(AckPayload) == 8, "AckPayload must be 8 bytes");

/* ------------------------------ CryptoMeta ------------------------------ */

/**
 * @struct CryptoMeta
 * @brief Encryption metadata for AEAD-encrypted payloads.
 *
 * Present immediately after header when encryptedPresent=1.
 * Provides key selection and nonce for AEAD decryption.
 *
 * Wire format:
 *   Offset  Size  Field
 *   0       1     keyIndex - Pre-shared key index (0-255)
 *   1       12    nonce    - AEAD nonce/IV (must be unique per message)
 *
 * Key management:
 *   - keyIndex selects from a pre-shared key table
 *   - Both endpoints must have the same key at the same index
 *   - Key rotation: update key table, then use new index
 *
 * Nonce requirements:
 *   - MUST be unique for each message with the same key
 *   - Recommended: 4-byte counter + 8-byte random, or monotonic counter
 *   - Nonce reuse with same key breaks AEAD security
 *
 * @note RT-safe: POD type, packed for wire transmission.
 */
struct CryptoMeta {
  std::uint8_t keyIndex;                 ///< Key table index (0-255)
  std::uint8_t nonce[APROTO_NONCE_SIZE]; ///< 12-byte AEAD nonce/IV
} __attribute__((packed));

static_assert(sizeof(CryptoMeta) == 13, "CryptoMeta must be 13 bytes");
static_assert(sizeof(CryptoMeta) == APROTO_CRYPTO_META_SIZE, "CryptoMeta size mismatch");

/* -------------------------- Standard Opcodes ---------------------------- */

/**
 * @enum SystemOpcode
 * @brief Reserved system-level opcodes (0x0000-0x00FF).
 *
 * These opcodes are handled by the executive or interface component.
 * Component-specific opcodes should use 0x0100 and above.
 */
enum class SystemOpcode : std::uint16_t {
  NOOP = 0x0000,       ///< No operation, returns ACK
  PING = 0x0001,       ///< Echo payload back
  GET_STATUS = 0x0002, ///< Get component status
  RESET = 0x0003,      ///< Reset component state

  // File transfer: upload (0x0020-0x0024)
  FILE_BEGIN = 0x0020,  ///< Initiate file upload (FileBeginPayload)
  FILE_CHUNK = 0x0021,  ///< File data chunk (chunkIndex + data)
  FILE_END = 0x0022,    ///< Finalize upload (CRC verify + commit)
  FILE_ABORT = 0x0023,  ///< Abort current transfer (upload or download)
  FILE_STATUS = 0x0024, ///< Query transfer status

  // File transfer: download (0x0025-0x0026)
  FILE_GET = 0x0025, ///< Request file download (FileGetPayload)
  FILE_READ_CHUNK =
      0x0026, ///< Read chunk from target (chunkIndex payload, chunk data in ACK extra)

  ACK = 0x00FE, ///< Acknowledgment (response only)
  NAK = 0x00FF, ///< Negative acknowledgment (response only)
};

/* ----------------------------- NAK Status Codes ----------------------------- */

/// Extended NAK status codes for system operations.
enum class NakStatus : std::uint8_t {
  SUCCESS = 0,              ///< Operation completed.
  UNKNOWN_OPCODE = 2,       ///< Unrecognized opcode.
  NO_RESOLVER = 3,          ///< Component resolver not attached.
  COMPONENT_NOT_FOUND = 4,  ///< fullUid not in registry.
  FRAMES_DROPPED = 7,       ///< Dropped frames due to framing errors.
  COMPONENT_LOCKED = 8,     ///< Component is locked for updates.
  TRANSFER_IN_PROGRESS = 9, ///< FILE_BEGIN while already receiving.
  NO_TRANSFER = 10,         ///< FILE_CHUNK/END without FILE_BEGIN.
  CHUNK_OUT_OF_ORDER = 11,  ///< chunkIndex does not match expected.
  CRC_MISMATCH = 12,        ///< File CRC32 verification failed.
  PATH_INVALID = 13,        ///< Destination path escapes filesystem root.
  WRITE_FAILED = 14,        ///< Filesystem write error.
  VALIDATION_FAILED = 15,   ///< TPRM validation rejected by component.
  APPLY_FAILED = 16,        ///< A/B bank swap failed.
  DLOPEN_FAILED = 17,       ///< dlopen() returned error.
  FACTORY_NOT_FOUND = 18,   ///< .so missing apex_create_component symbol.
  INIT_FAILED = 19,         ///< New component init() failed.
  NOT_SWAPPABLE = 20,       ///< Component does not support library reload.
  INVALID_PAYLOAD = 21,     ///< Payload too small or malformed.
  FILE_NOT_FOUND = 22,      ///< FILE_GET: requested file does not exist.
  READ_FAILED = 23,         ///< FILE_READ_CHUNK: filesystem read error.
  NO_DOWNLOAD = 24,         ///< FILE_READ_CHUNK without prior FILE_GET.
  CHUNK_OUT_OF_RANGE = 25,  ///< FILE_READ_CHUNK: chunkIndex >= totalChunks.
};

/* ----------------------------- File Transfer Types ----------------------------- */

/// File transfer state machine.
enum class FileTransferState : std::uint8_t {
  IDLE = 0,      ///< No transfer in progress.
  RECEIVING = 1, ///< Upload: chunks being received.
  COMPLETE = 2,  ///< Transfer complete (CRC verified).
  ERROR = 3,     ///< Transfer failed.
  SENDING = 4,   ///< Download: file open for chunk reads.
};

/// Payload for FILE_BEGIN opcode.
struct FileBeginPayload {
  std::uint32_t totalSize;   ///< Total file size in bytes.
  std::uint16_t chunkSize;   ///< Bytes per chunk (last chunk may be smaller).
  std::uint16_t totalChunks; ///< Total number of chunks.
  std::uint32_t crc32;       ///< CRC32 of entire file (verified on FILE_END).
  char path[64];             ///< Destination path relative to .apex_fs/ root.
} __attribute__((packed));

static_assert(sizeof(FileBeginPayload) == 76, "FileBeginPayload must be 76 bytes");

/// Response payload for FILE_END opcode.
struct FileEndResponse {
  std::uint8_t status;        ///< 0 = success, 1 = CRC mismatch, 2 = incomplete.
  std::uint8_t reserved[3];   ///< Padding.
  std::uint32_t bytesWritten; ///< Total bytes written to file.
} __attribute__((packed));

static_assert(sizeof(FileEndResponse) == 8, "FileEndResponse must be 8 bytes");

/// Response payload for FILE_STATUS opcode.
struct FileStatusResponse {
  std::uint8_t state;           ///< FileTransferState value.
  std::uint8_t reserved;        ///< Padding.
  std::uint16_t chunksReceived; ///< Chunks received so far.
  std::uint32_t bytesReceived;  ///< Bytes received so far.
} __attribute__((packed));

static_assert(sizeof(FileStatusResponse) == 8, "FileStatusResponse must be 8 bytes");

/// Payload for FILE_GET opcode (download request).
struct FileGetPayload {
  char path[64];              ///< Source path relative to .apex_fs/ root.
  std::uint16_t maxChunkSize; ///< Maximum bytes per chunk (client's buffer limit).
} __attribute__((packed));

static_assert(sizeof(FileGetPayload) == 66, "FileGetPayload must be 66 bytes");

/// Response payload for FILE_GET opcode (returned in ACK extra data).
struct FileGetResponse {
  std::uint32_t totalSize;   ///< File size in bytes.
  std::uint16_t chunkSize;   ///< Actual bytes per chunk (may be <= maxChunkSize).
  std::uint16_t totalChunks; ///< Total number of chunks.
  std::uint32_t crc32;       ///< CRC32 of entire file (client verifies after last chunk).
} __attribute__((packed));

static_assert(sizeof(FileGetResponse) == 12, "FileGetResponse must be 12 bytes");

/* -------------------------- Helper Functions ---------------------------- */

/**
 * @brief Build flags byte from individual settings.
 * @param isResponse True for response/telemetry packets.
 * @param ackRequested True to request ACK/NAK.
 * @param crcPresent True if CRC32 trailer follows payload.
 * @param encryptedPresent True if payload is AEAD encrypted with crypto metadata.
 * @return Populated AprotoFlags.
 * @note RT-safe.
 */
[[nodiscard]] inline AprotoFlags makeFlags(bool isResponse, bool ackRequested, bool crcPresent,
                                           bool encryptedPresent = false) noexcept {
  AprotoFlags f{};
  f.internalOrigin = 0; // Default: external origin.
  f.isResponse = isResponse ? 1 : 0;
  f.ackRequested = ackRequested ? 1 : 0;
  f.crcPresent = crcPresent ? 1 : 0;
  f.encryptedPresent = encryptedPresent ? 1 : 0;
  f.reserved1 = 0;
  return f;
}

/**
 * @brief Convert flags struct to raw byte.
 * @param f Flags struct.
 * @return Raw byte value.
 * @note RT-safe.
 */
[[nodiscard]] inline std::uint8_t flagsToByte(AprotoFlags f) noexcept {
  std::uint8_t b = 0;
  b |= (f.internalOrigin & 0x01);
  b |= (f.isResponse & 0x01) << 1;
  b |= (f.ackRequested & 0x01) << 2;
  b |= (f.crcPresent & 0x01) << 3;
  b |= (f.encryptedPresent & 0x01) << 4;
  b |= (f.reserved1 & 0x07) << 5;
  return b;
}

/**
 * @brief Convert raw byte to flags struct.
 * @param b Raw byte value.
 * @return Parsed flags struct.
 * @note RT-safe.
 */
[[nodiscard]] inline AprotoFlags byteToFlags(std::uint8_t b) noexcept {
  AprotoFlags f{};
  f.internalOrigin = b & 0x01;
  f.isResponse = (b >> 1) & 0x01;
  f.ackRequested = (b >> 2) & 0x01;
  f.crcPresent = (b >> 3) & 0x01;
  f.encryptedPresent = (b >> 4) & 0x01;
  f.reserved1 = (b >> 5) & 0x07;
  return f;
}

/**
 * @brief Calculate total packet size including crypto metadata and optional CRC.
 * @param hdr Header with payloadLength, encryptedPresent, and crcPresent flags.
 * @return Total packet size in bytes.
 * @note RT-safe.
 *
 * Packet layout:
 *   - Header (14 bytes)
 *   - [CryptoMeta (13 bytes)] if encryptedPresent
 *   - Payload (payloadLength bytes) - includes AUTH_TAG if encrypted
 *   - [CRC32 (4 bytes)] if crcPresent
 */
[[nodiscard]] inline std::size_t packetSize(const AprotoHeader& hdr) noexcept {
  std::size_t size = APROTO_HEADER_SIZE + hdr.payloadLength;
  if (hdr.flags.encryptedPresent) {
    size += APROTO_CRYPTO_META_SIZE;
  }
  if (hdr.flags.crcPresent) {
    size += APROTO_CRC_SIZE;
  }
  return size;
}

} // namespace aproto
} // namespace protocols
} // namespace system_core

#endif // APEX_SYSTEM_CORE_PROTOCOLS_APROTO_TYPES_HPP
