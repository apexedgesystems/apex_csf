#ifndef APEX_SYSTEM_CORE_INTERFACE_FILE_TRANSFER_HANDLER_HPP
#define APEX_SYSTEM_CORE_INTERFACE_FILE_TRANSFER_HANDLER_HPP
/**
 * @file FileTransferHandler.hpp
 * @brief Chunked file transfer over APROTO system opcodes.
 *
 * Upload (ground -> target):
 *   FILE_BEGIN, FILE_CHUNK, FILE_END, FILE_ABORT, FILE_STATUS
 *   Assembles chunks into a staging file, verifies CRC32, atomically commits.
 *
 * Download (target -> ground):
 *   FILE_GET, FILE_READ_CHUNK, FILE_ABORT, FILE_STATUS
 *   Opens a file for reading, serves chunks on demand, client verifies CRC32.
 *
 * Design:
 *  - One transfer at a time (upload OR download, not both).
 *  - State machine: IDLE -> RECEIVING (upload) or SENDING (download) -> IDLE.
 *  - FILE_ABORT resets state for either direction.
 *  - FILE_STATUS reports progress for either direction.
 *  - All paths validated to be under filesystem root (no traversal).
 *  - NOT RT-safe: performs file I/O. Called from Interface's system opcode handler.
 */

#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace system_core {
namespace interface {

/* ----------------------------- FileTransferHandler ----------------------------- */

class FileTransferHandler {
public:
  /** @brief Default constructor (no filesystem root, must call setRoot before use). */
  FileTransferHandler() noexcept = default;

  /**
   * @brief Construct with filesystem root and staging directory.
   * @param fsRoot Absolute path to .apex_fs/ root.
   */
  explicit FileTransferHandler(std::filesystem::path fsRoot) noexcept;

  ~FileTransferHandler();

  FileTransferHandler(FileTransferHandler&&) noexcept = default;
  FileTransferHandler& operator=(FileTransferHandler&&) noexcept = default;
  FileTransferHandler(const FileTransferHandler&) = delete;
  FileTransferHandler& operator=(const FileTransferHandler&) = delete;

  /**
   * @brief Handle FILE_BEGIN opcode.
   * @param payload Raw payload bytes (must be FileBeginPayload).
   * @param payloadLen Payload length.
   * @return NakStatus code.
   */
  [[nodiscard]] std::uint8_t handleBegin(const std::uint8_t* payload,
                                         std::size_t payloadLen) noexcept;

  /**
   * @brief Handle FILE_CHUNK opcode.
   * @param payload Raw payload bytes (chunkIndex:u16 + data).
   * @param payloadLen Payload length.
   * @return NakStatus code.
   */
  [[nodiscard]] std::uint8_t handleChunk(const std::uint8_t* payload,
                                         std::size_t payloadLen) noexcept;

  /**
   * @brief Handle FILE_END opcode.
   * @param response Output buffer for FileEndResponse (8 bytes).
   * @param responseLen Output: bytes written to response.
   * @return NakStatus code.
   */
  [[nodiscard]] std::uint8_t handleEnd(std::uint8_t* response, std::size_t& responseLen) noexcept;

  /**
   * @brief Handle FILE_ABORT opcode.
   * @return NakStatus code (always SUCCESS).
   */
  [[nodiscard]] std::uint8_t handleAbort() noexcept;

  /**
   * @brief Handle FILE_STATUS opcode.
   * @param response Output buffer for FileStatusResponse (8 bytes).
   * @param responseLen Output: bytes written to response.
   * @return NakStatus code (always SUCCESS).
   */
  [[nodiscard]] std::uint8_t handleStatus(std::uint8_t* response,
                                          std::size_t& responseLen) const noexcept;

  /* ----------------------------- Download (FILE_GET / FILE_READ_CHUNK) ---- */

  /**
   * @brief Handle FILE_GET opcode (begin download).
   * @param payload Raw payload bytes (must be FileGetPayload).
   * @param payloadLen Payload length.
   * @param response Output buffer for FileGetResponse (12 bytes).
   * @param responseLen Output: bytes written to response.
   * @return NakStatus code.
   */
  [[nodiscard]] std::uint8_t handleGet(const std::uint8_t* payload, std::size_t payloadLen,
                                       std::uint8_t* response, std::size_t& responseLen) noexcept;

  /**
   * @brief Handle FILE_READ_CHUNK opcode (read one chunk).
   * @param payload Raw payload bytes (chunkIndex:u16).
   * @param payloadLen Payload length.
   * @param response Output buffer for chunk data (up to sendChunkSize_ bytes).
   * @param responseLen Output: bytes written to response.
   * @return NakStatus code.
   */
  [[nodiscard]] std::uint8_t handleReadChunk(const std::uint8_t* payload, std::size_t payloadLen,
                                             std::uint8_t* response,
                                             std::size_t& responseLen) noexcept;

private:
  /** @brief Reset state to IDLE, close staging file, delete partial file. */
  void resetState() noexcept;

  /** @brief Compute CRC32 of a file on disk. */
  [[nodiscard]] std::uint32_t computeFileCrc32(const std::filesystem::path& path) const noexcept;

  /** @brief Validate destination path is under fsRoot_ (no traversal). */
  [[nodiscard]] bool isPathSafe(const std::string& relativePath) const noexcept;

  std::filesystem::path fsRoot_;    ///< .apex_fs/ root.
  std::filesystem::path stageDir_;  ///< .apex_fs/stage/ directory.
  std::filesystem::path stagePath_; ///< .apex_fs/stage/pending (current transfer).

  protocols::aproto::FileTransferState state_{protocols::aproto::FileTransferState::IDLE};

  // Transfer metadata (set by FILE_BEGIN)
  std::uint32_t expectedTotalSize_{0};
  std::uint16_t expectedChunkSize_{0};
  std::uint16_t expectedTotalChunks_{0};
  std::uint32_t expectedCrc32_{0};
  std::string destinationPath_; ///< Relative path under fsRoot_.

  // Upload progress
  std::uint16_t chunksReceived_{0};
  std::uint32_t bytesReceived_{0};
  std::ofstream stageFile_; ///< Output stream to staging file.

  // Download (sending) state
  std::uint32_t sendTotalSize_{0};
  std::uint16_t sendChunkSize_{0};
  std::uint16_t sendTotalChunks_{0};
  std::uint32_t sendCrc32_{0};
  std::uint16_t chunksSent_{0};
  std::uint32_t bytesSent_{0};
  std::ifstream sendFile_; ///< Input stream from source file.
};

} // namespace interface
} // namespace system_core

#endif // APEX_SYSTEM_CORE_INTERFACE_FILE_TRANSFER_HANDLER_HPP
