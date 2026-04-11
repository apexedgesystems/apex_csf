/**
 * @file FileTransferHandler.cpp
 * @brief Chunked file transfer over APROTO system opcodes.
 *
 * Upload: assembles file chunks into a staging file, verifies CRC32,
 * and atomically moves the completed file to the destination path.
 *
 * Download: opens a source file, computes CRC32, serves chunks on demand.
 */

#include "src/system/core/components/interface/apex/inc/FileTransferHandler.hpp"
#include "src/utilities/checksums/crc/inc/Crc.hpp"

#include <cstring>

#include <algorithm>
#include <array>
#include <filesystem>

namespace system_core {
namespace interface {

namespace aproto = protocols::aproto;
namespace fs = std::filesystem;

/* ----------------------------- Constants ----------------------------- */

static constexpr std::size_t CRC_READ_BUF_SIZE = 4096;
static constexpr std::size_t FILE_BEGIN_MIN_SIZE = sizeof(aproto::FileBeginPayload);
static constexpr std::size_t CHUNK_HEADER_SIZE = 2; // uint16_t chunkIndex

/* ----------------------------- FileTransferHandler Methods ----------------------------- */

FileTransferHandler::FileTransferHandler(fs::path fsRoot) noexcept
    : fsRoot_(std::move(fsRoot)), stageDir_(fsRoot_ / "stage"), stagePath_(stageDir_ / "pending") {}

FileTransferHandler::~FileTransferHandler() { resetState(); }

std::uint8_t FileTransferHandler::handleBegin(const std::uint8_t* payload,
                                              std::size_t payloadLen) noexcept {
  // Reject if any transfer already in progress.
  if (state_ == aproto::FileTransferState::RECEIVING ||
      state_ == aproto::FileTransferState::SENDING) {
    return static_cast<std::uint8_t>(aproto::NakStatus::TRANSFER_IN_PROGRESS);
  }

  // Validate payload size.
  if (payloadLen < FILE_BEGIN_MIN_SIZE) {
    return static_cast<std::uint8_t>(aproto::NakStatus::INVALID_PAYLOAD);
  }

  // Parse payload.
  aproto::FileBeginPayload begin{};
  std::memcpy(&begin, payload, sizeof(begin));

  // Extract null-terminated path.
  begin.path[sizeof(begin.path) - 1] = '\0';
  destinationPath_ = begin.path;

  // Validate destination path.
  if (destinationPath_.empty() || !isPathSafe(destinationPath_)) {
    return static_cast<std::uint8_t>(aproto::NakStatus::PATH_INVALID);
  }

  // Store transfer metadata.
  expectedTotalSize_ = begin.totalSize;
  expectedChunkSize_ = begin.chunkSize;
  expectedTotalChunks_ = begin.totalChunks;
  expectedCrc32_ = begin.crc32;
  chunksReceived_ = 0;
  bytesReceived_ = 0;

  // Create staging directory if needed.
  std::error_code ec;
  fs::create_directories(stageDir_, ec);
  if (ec) {
    return static_cast<std::uint8_t>(aproto::NakStatus::WRITE_FAILED);
  }

  // Remove any leftover staging file.
  fs::remove(stagePath_, ec);

  // Open staging file for writing.
  stageFile_.open(stagePath_, std::ios::binary | std::ios::trunc);
  if (!stageFile_.is_open()) {
    return static_cast<std::uint8_t>(aproto::NakStatus::WRITE_FAILED);
  }

  state_ = aproto::FileTransferState::RECEIVING;
  return static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS);
}

std::uint8_t FileTransferHandler::handleChunk(const std::uint8_t* payload,
                                              std::size_t payloadLen) noexcept {
  // Must be in RECEIVING state.
  if (state_ != aproto::FileTransferState::RECEIVING) {
    return static_cast<std::uint8_t>(aproto::NakStatus::NO_TRANSFER);
  }

  // Minimum: 2-byte chunk index.
  if (payloadLen < CHUNK_HEADER_SIZE) {
    return static_cast<std::uint8_t>(aproto::NakStatus::INVALID_PAYLOAD);
  }

  // Parse chunk index (little-endian).
  std::uint16_t chunkIndex = 0;
  std::memcpy(&chunkIndex, payload, sizeof(chunkIndex));

  // Verify sequential order.
  if (chunkIndex != chunksReceived_) {
    return static_cast<std::uint8_t>(aproto::NakStatus::CHUNK_OUT_OF_ORDER);
  }

  // Write chunk data to staging file.
  const std::uint8_t* DATA = payload + CHUNK_HEADER_SIZE;
  const std::size_t DATA_LEN = payloadLen - CHUNK_HEADER_SIZE;

  if (DATA_LEN > 0) {
    stageFile_.write(reinterpret_cast<const char*>(DATA), static_cast<std::streamsize>(DATA_LEN));
    if (!stageFile_.good()) {
      resetState();
      return static_cast<std::uint8_t>(aproto::NakStatus::WRITE_FAILED);
    }
  }

  ++chunksReceived_;
  bytesReceived_ += static_cast<std::uint32_t>(DATA_LEN);

  return static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS);
}

std::uint8_t FileTransferHandler::handleEnd(std::uint8_t* response,
                                            std::size_t& responseLen) noexcept {
  responseLen = 0;

  // Must be in RECEIVING state.
  if (state_ != aproto::FileTransferState::RECEIVING) {
    return static_cast<std::uint8_t>(aproto::NakStatus::NO_TRANSFER);
  }

  // Close staging file to flush.
  stageFile_.close();

  // Build response.
  aproto::FileEndResponse resp{};
  resp.bytesWritten = bytesReceived_;
  std::memset(resp.reserved, 0, sizeof(resp.reserved));

  // Verify CRC32 of assembled file.
  const std::uint32_t ACTUAL_CRC = computeFileCrc32(stagePath_);
  if (ACTUAL_CRC != expectedCrc32_) {
    resp.status = 1; // CRC mismatch.
    std::memcpy(response, &resp, sizeof(resp));
    responseLen = sizeof(resp);
    state_ = aproto::FileTransferState::ERROR;
    // Clean up partial file.
    std::error_code ec;
    fs::remove(stagePath_, ec);
    state_ = aproto::FileTransferState::IDLE;
    return static_cast<std::uint8_t>(aproto::NakStatus::CRC_MISMATCH);
  }

  // Verify completeness.
  if (chunksReceived_ != expectedTotalChunks_) {
    resp.status = 2; // Incomplete.
    std::memcpy(response, &resp, sizeof(resp));
    responseLen = sizeof(resp);
    resetState();
    return static_cast<std::uint8_t>(aproto::NakStatus::INVALID_PAYLOAD);
  }

  // Move staging file to destination.
  const fs::path DEST = fsRoot_ / destinationPath_;

  // Create parent directories.
  std::error_code ec;
  fs::create_directories(DEST.parent_path(), ec);
  if (ec) {
    resp.status = static_cast<std::uint8_t>(aproto::NakStatus::WRITE_FAILED);
    std::memcpy(response, &resp, sizeof(resp));
    responseLen = sizeof(resp);
    resetState();
    return static_cast<std::uint8_t>(aproto::NakStatus::WRITE_FAILED);
  }

  // Atomic rename (same filesystem).
  fs::rename(stagePath_, DEST, ec);
  if (ec) {
    // Rename failed (cross-device?) - try copy + delete.
    fs::copy_file(stagePath_, DEST, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      resp.status = static_cast<std::uint8_t>(aproto::NakStatus::WRITE_FAILED);
      std::memcpy(response, &resp, sizeof(resp));
      responseLen = sizeof(resp);
      resetState();
      return static_cast<std::uint8_t>(aproto::NakStatus::WRITE_FAILED);
    }
    fs::remove(stagePath_, ec);
  }

  resp.status = 0; // Success.
  std::memcpy(response, &resp, sizeof(resp));
  responseLen = sizeof(resp);

  state_ = aproto::FileTransferState::IDLE;
  return static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS);
}

std::uint8_t FileTransferHandler::handleAbort() noexcept {
  resetState();
  return static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS);
}

std::uint8_t FileTransferHandler::handleStatus(std::uint8_t* response,
                                               std::size_t& responseLen) const noexcept {
  aproto::FileStatusResponse resp{};
  resp.state = static_cast<std::uint8_t>(state_);
  resp.reserved = 0;

  if (state_ == aproto::FileTransferState::SENDING) {
    // Download in progress: report chunks/bytes sent.
    resp.chunksReceived = chunksSent_;
    resp.bytesReceived = bytesSent_;
  } else {
    // Upload or idle: report chunks/bytes received.
    resp.chunksReceived = chunksReceived_;
    resp.bytesReceived = bytesReceived_;
  }

  std::memcpy(response, &resp, sizeof(resp));
  responseLen = sizeof(resp);
  return static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS);
}

/* ----------------------------- Download (FILE_GET / FILE_READ_CHUNK) ---- */

static constexpr std::size_t FILE_GET_MIN_SIZE = sizeof(aproto::FileGetPayload);
static constexpr std::size_t READ_CHUNK_HEADER_SIZE = 2; // uint16_t chunkIndex

std::uint8_t FileTransferHandler::handleGet(const std::uint8_t* payload, std::size_t payloadLen,
                                            std::uint8_t* response,
                                            std::size_t& responseLen) noexcept {
  responseLen = 0;

  // Reject if any transfer already in progress.
  if (state_ == aproto::FileTransferState::RECEIVING ||
      state_ == aproto::FileTransferState::SENDING) {
    return static_cast<std::uint8_t>(aproto::NakStatus::TRANSFER_IN_PROGRESS);
  }

  // Validate payload size.
  if (payloadLen < FILE_GET_MIN_SIZE) {
    return static_cast<std::uint8_t>(aproto::NakStatus::INVALID_PAYLOAD);
  }

  // Parse payload.
  aproto::FileGetPayload get{};
  std::memcpy(&get, payload, sizeof(get));
  get.path[sizeof(get.path) - 1] = '\0';

  const std::string PATH(get.path);

  // Validate path.
  if (PATH.empty() || !isPathSafe(PATH)) {
    return static_cast<std::uint8_t>(aproto::NakStatus::PATH_INVALID);
  }

  // Resolve full path and check existence.
  const fs::path FULL_PATH = fsRoot_ / PATH;
  std::error_code ec;
  if (!fs::exists(FULL_PATH, ec) || fs::is_directory(FULL_PATH, ec)) {
    return static_cast<std::uint8_t>(aproto::NakStatus::FILE_NOT_FOUND);
  }

  // Get file size.
  const auto FILE_SIZE = fs::file_size(FULL_PATH, ec);
  if (ec || FILE_SIZE == 0) {
    return static_cast<std::uint8_t>(aproto::NakStatus::FILE_NOT_FOUND);
  }

  // Open for reading.
  sendFile_.open(FULL_PATH, std::ios::binary);
  if (!sendFile_.is_open()) {
    return static_cast<std::uint8_t>(aproto::NakStatus::READ_FAILED);
  }

  // Compute CRC32 of the source file.
  sendCrc32_ = computeFileCrc32(FULL_PATH);

  // Determine chunk size: use client's max or default to 4096.
  sendChunkSize_ = (get.maxChunkSize > 0) ? get.maxChunkSize : 4096;
  sendTotalSize_ = static_cast<std::uint32_t>(FILE_SIZE);
  sendTotalChunks_ =
      static_cast<std::uint16_t>((sendTotalSize_ + sendChunkSize_ - 1) / sendChunkSize_);
  chunksSent_ = 0;
  bytesSent_ = 0;

  state_ = aproto::FileTransferState::SENDING;

  // Build response.
  aproto::FileGetResponse resp{};
  resp.totalSize = sendTotalSize_;
  resp.chunkSize = sendChunkSize_;
  resp.totalChunks = sendTotalChunks_;
  resp.crc32 = sendCrc32_;

  std::memcpy(response, &resp, sizeof(resp));
  responseLen = sizeof(resp);
  return static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS);
}

std::uint8_t FileTransferHandler::handleReadChunk(const std::uint8_t* payload,
                                                  std::size_t payloadLen, std::uint8_t* response,
                                                  std::size_t& responseLen) noexcept {
  responseLen = 0;

  // Must be in SENDING state.
  if (state_ != aproto::FileTransferState::SENDING) {
    return static_cast<std::uint8_t>(aproto::NakStatus::NO_DOWNLOAD);
  }

  // Parse chunk index.
  if (payloadLen < READ_CHUNK_HEADER_SIZE) {
    return static_cast<std::uint8_t>(aproto::NakStatus::INVALID_PAYLOAD);
  }

  std::uint16_t chunkIndex = 0;
  std::memcpy(&chunkIndex, payload, sizeof(chunkIndex));

  // Range check.
  if (chunkIndex >= sendTotalChunks_) {
    return static_cast<std::uint8_t>(aproto::NakStatus::CHUNK_OUT_OF_RANGE);
  }

  // Seek to chunk offset.
  const auto OFFSET = static_cast<std::streamoff>(chunkIndex) * sendChunkSize_;
  sendFile_.seekg(OFFSET, std::ios::beg);
  if (!sendFile_.good()) {
    resetState();
    return static_cast<std::uint8_t>(aproto::NakStatus::READ_FAILED);
  }

  // Read chunk data.
  const std::size_t REMAINING =
      sendTotalSize_ - static_cast<std::uint32_t>(chunkIndex) * sendChunkSize_;
  const std::size_t READ_SIZE = std::min(static_cast<std::size_t>(sendChunkSize_), REMAINING);

  sendFile_.read(reinterpret_cast<char*>(response), static_cast<std::streamsize>(READ_SIZE));
  if (!sendFile_.good() && !sendFile_.eof()) {
    resetState();
    return static_cast<std::uint8_t>(aproto::NakStatus::READ_FAILED);
  }

  responseLen = static_cast<std::size_t>(sendFile_.gcount());
  ++chunksSent_;
  bytesSent_ += static_cast<std::uint32_t>(responseLen);

  // Auto-complete after last chunk.
  if (chunksSent_ >= sendTotalChunks_) {
    sendFile_.close();
    state_ = aproto::FileTransferState::IDLE;
  }

  return static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS);
}

/* ----------------------------- File Helpers ----------------------------- */

void FileTransferHandler::resetState() noexcept {
  // Upload cleanup.
  if (stageFile_.is_open()) {
    stageFile_.close();
  }
  std::error_code ec;
  fs::remove(stagePath_, ec);

  expectedTotalSize_ = 0;
  expectedChunkSize_ = 0;
  expectedTotalChunks_ = 0;
  expectedCrc32_ = 0;
  destinationPath_.clear();
  chunksReceived_ = 0;
  bytesReceived_ = 0;

  // Download cleanup.
  if (sendFile_.is_open()) {
    sendFile_.close();
  }
  sendTotalSize_ = 0;
  sendChunkSize_ = 0;
  sendTotalChunks_ = 0;
  sendCrc32_ = 0;
  chunksSent_ = 0;
  bytesSent_ = 0;

  state_ = aproto::FileTransferState::IDLE;
}

std::uint32_t FileTransferHandler::computeFileCrc32(const fs::path& path) const noexcept {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return 0;
  }

  apex::checksums::crc::Crc32IscsiTable crc;
  std::array<char, CRC_READ_BUF_SIZE> buf{};

  while (file.read(buf.data(), static_cast<std::streamsize>(buf.size())) || file.gcount() > 0) {
    crc.update(reinterpret_cast<const std::uint8_t*>(buf.data()),
               static_cast<std::size_t>(file.gcount()));
    if (file.eof()) {
      break;
    }
  }

  std::uint32_t result = 0;
  crc.finalize(result);
  return result;
}

bool FileTransferHandler::isPathSafe(const std::string& relativePath) const noexcept {
  // Reject paths with directory traversal.
  if (relativePath.find("..") != std::string::npos) {
    return false;
  }

  // Reject absolute paths.
  if (!relativePath.empty() && relativePath[0] == '/') {
    return false;
  }

  // Verify resolved path is under fsRoot_.
  std::error_code ec;
  const fs::path RESOLVED = fs::weakly_canonical(fsRoot_ / relativePath, ec);
  if (ec) {
    return false;
  }

  const fs::path ROOT_CANONICAL = fs::weakly_canonical(fsRoot_, ec);
  if (ec) {
    return false;
  }

  // Check that resolved path starts with the root.
  const std::string RESOLVED_STR = RESOLVED.string();
  const std::string ROOT_STR = ROOT_CANONICAL.string();
  return RESOLVED_STR.compare(0, ROOT_STR.size(), ROOT_STR) == 0;
}

} // namespace interface
} // namespace system_core
