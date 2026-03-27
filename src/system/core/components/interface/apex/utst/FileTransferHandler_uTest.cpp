/**
 * @file FileTransferHandler_uTest.cpp
 * @brief Unit tests for FileTransferHandler chunked file transfer.
 */

#include "src/system/core/components/interface/apex/inc/FileTransferHandler.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoTypes.hpp"
#include "src/utilities/checksums/crc/inc/Crc.hpp"

#include <cstdint>
#include <cstring>

#include <array>
#include <filesystem>
#include <fstream>

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace aproto = system_core::protocols::aproto;
namespace fs = std::filesystem;

/* ----------------------------- Test Fixture ----------------------------- */

class FileTransferHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create a unique temp directory per test instance to avoid parallel races.
    testRoot_ = fs::temp_directory_path() /
                ("apex_ft_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    fs::create_directories(testRoot_);
    handler_ = system_core::interface::FileTransferHandler(testRoot_);
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(testRoot_, ec);
  }

  /** @brief Build a FileBeginPayload for a given file content. */
  aproto::FileBeginPayload makeBeginPayload(const std::vector<std::uint8_t>& fileData,
                                            const std::string& destPath, std::uint16_t chunkSize) {
    aproto::FileBeginPayload begin{};
    begin.totalSize = static_cast<std::uint32_t>(fileData.size());
    begin.chunkSize = chunkSize;
    begin.totalChunks = static_cast<std::uint16_t>((fileData.size() + chunkSize - 1) / chunkSize);
    begin.crc32 = computeCrc32(fileData);
    std::memset(begin.path, 0, sizeof(begin.path));
    std::strncpy(begin.path, destPath.c_str(), sizeof(begin.path) - 1);
    return begin;
  }

  /** @brief Compute CRC32-C of a byte vector. */
  std::uint32_t computeCrc32(const std::vector<std::uint8_t>& data) {
    apex::checksums::crc::Crc32IscsiTable crc;
    if (!data.empty()) {
      crc.update(data.data(), data.size());
    }
    std::uint32_t result = 0;
    crc.finalize(result);
    return result;
  }

  /** @brief Send a complete file transfer (begin + chunks + end). */
  std::uint8_t sendFile(const std::vector<std::uint8_t>& fileData, const std::string& destPath,
                        std::uint16_t chunkSize) {
    const auto BEGIN = makeBeginPayload(fileData, destPath, chunkSize);
    auto status =
        handler_.handleBegin(reinterpret_cast<const std::uint8_t*>(&BEGIN), sizeof(BEGIN));
    if (status != 0)
      return status;

    const std::uint16_t TOTAL_CHUNKS = BEGIN.totalChunks;
    for (std::uint16_t i = 0; i < TOTAL_CHUNKS; ++i) {
      const std::size_t OFFSET = static_cast<std::size_t>(i) * chunkSize;
      const std::size_t LEN =
          std::min(static_cast<std::size_t>(chunkSize), fileData.size() - OFFSET);

      // Build chunk: 2-byte index + data.
      std::vector<std::uint8_t> chunk(2 + LEN);
      std::memcpy(chunk.data(), &i, 2);
      if (LEN > 0) {
        std::memcpy(chunk.data() + 2, fileData.data() + OFFSET, LEN);
      }

      status = handler_.handleChunk(chunk.data(), chunk.size());
      if (status != 0)
        return status;
    }

    std::array<std::uint8_t, sizeof(aproto::FileEndResponse)> resp{};
    std::size_t respLen = 0;
    return handler_.handleEnd(resp.data(), respLen);
  }

  fs::path testRoot_;
  system_core::interface::FileTransferHandler handler_;
};

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed handler rejects FILE_BEGIN (no fsRoot). */
TEST_F(FileTransferHandlerTest, DefaultConstructedRejectsBegin) {
  system_core::interface::FileTransferHandler defaultHandler;
  const std::vector<std::uint8_t> DATA{0x41, 0x42, 0x43};
  const auto BEGIN = makeBeginPayload(DATA, "test.bin", 64);
  const std::uint8_t STATUS =
      defaultHandler.handleBegin(reinterpret_cast<const std::uint8_t*>(&BEGIN), sizeof(BEGIN));
  EXPECT_NE(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS));
}

/* ----------------------------- FILE_BEGIN Tests ----------------------------- */

/** @test FILE_BEGIN with valid payload succeeds and enters RECEIVING state. */
TEST_F(FileTransferHandlerTest, BeginValidPayload) {
  const std::vector<std::uint8_t> DATA{0x41, 0x42, 0x43};
  const auto BEGIN = makeBeginPayload(DATA, "test.bin", 64);
  const std::uint8_t STATUS =
      handler_.handleBegin(reinterpret_cast<const std::uint8_t*>(&BEGIN), sizeof(BEGIN));
  EXPECT_EQ(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS));
}

/** @test FILE_BEGIN with too-small payload returns INVALID_PAYLOAD. */
TEST_F(FileTransferHandlerTest, BeginTooSmallPayload) {
  const std::array<std::uint8_t, 4> SMALL{};
  const std::uint8_t STATUS = handler_.handleBegin(SMALL.data(), SMALL.size());
  EXPECT_EQ(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::INVALID_PAYLOAD));
}

/** @test FILE_BEGIN while already receiving returns TRANSFER_IN_PROGRESS. */
TEST_F(FileTransferHandlerTest, BeginWhileReceiving) {
  const std::vector<std::uint8_t> DATA{0x41, 0x42, 0x43};
  const auto BEGIN = makeBeginPayload(DATA, "test.bin", 64);
  (void)handler_.handleBegin(reinterpret_cast<const std::uint8_t*>(&BEGIN), sizeof(BEGIN));

  const std::uint8_t STATUS =
      handler_.handleBegin(reinterpret_cast<const std::uint8_t*>(&BEGIN), sizeof(BEGIN));
  EXPECT_EQ(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::TRANSFER_IN_PROGRESS));
}

/** @test FILE_BEGIN with empty path returns PATH_INVALID. */
TEST_F(FileTransferHandlerTest, BeginEmptyPath) {
  const std::vector<std::uint8_t> DATA{0x41, 0x42, 0x43};
  auto begin = makeBeginPayload(DATA, "", 64);
  const std::uint8_t STATUS =
      handler_.handleBegin(reinterpret_cast<const std::uint8_t*>(&begin), sizeof(begin));
  EXPECT_EQ(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::PATH_INVALID));
}

/** @test FILE_BEGIN with directory traversal path returns PATH_INVALID. */
TEST_F(FileTransferHandlerTest, BeginTraversalPath) {
  const std::vector<std::uint8_t> DATA{0x41, 0x42, 0x43};
  const auto BEGIN = makeBeginPayload(DATA, "../etc/passwd", 64);
  const std::uint8_t STATUS =
      handler_.handleBegin(reinterpret_cast<const std::uint8_t*>(&BEGIN), sizeof(BEGIN));
  EXPECT_EQ(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::PATH_INVALID));
}

/** @test FILE_BEGIN with absolute path returns PATH_INVALID. */
TEST_F(FileTransferHandlerTest, BeginAbsolutePath) {
  const std::vector<std::uint8_t> DATA{0x41, 0x42, 0x43};
  const auto BEGIN = makeBeginPayload(DATA, "/etc/passwd", 64);
  const std::uint8_t STATUS =
      handler_.handleBegin(reinterpret_cast<const std::uint8_t*>(&BEGIN), sizeof(BEGIN));
  EXPECT_EQ(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::PATH_INVALID));
}

/* ----------------------------- FILE_CHUNK Tests ----------------------------- */

/** @test FILE_CHUNK without active transfer returns NO_TRANSFER. */
TEST_F(FileTransferHandlerTest, ChunkWithoutTransfer) {
  const std::array<std::uint8_t, 4> CHUNK{0, 0, 0x41, 0x42};
  const std::uint8_t STATUS = handler_.handleChunk(CHUNK.data(), CHUNK.size());
  EXPECT_EQ(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::NO_TRANSFER));
}

/** @test FILE_CHUNK with out-of-order index returns CHUNK_OUT_OF_ORDER. */
TEST_F(FileTransferHandlerTest, ChunkOutOfOrder) {
  const std::vector<std::uint8_t> DATA{0x41, 0x42, 0x43};
  const auto BEGIN = makeBeginPayload(DATA, "test.bin", 1);
  (void)handler_.handleBegin(reinterpret_cast<const std::uint8_t*>(&BEGIN), sizeof(BEGIN));

  // Send chunk index 1 instead of 0.
  std::array<std::uint8_t, 3> chunk{};
  const std::uint16_t IDX = 1;
  std::memcpy(chunk.data(), &IDX, 2);
  chunk[2] = 0x41;

  const std::uint8_t STATUS = handler_.handleChunk(chunk.data(), chunk.size());
  EXPECT_EQ(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::CHUNK_OUT_OF_ORDER));
}

/** @test FILE_CHUNK with too-small payload returns INVALID_PAYLOAD. */
TEST_F(FileTransferHandlerTest, ChunkTooSmall) {
  const std::vector<std::uint8_t> DATA{0x41, 0x42, 0x43};
  const auto BEGIN = makeBeginPayload(DATA, "test.bin", 64);
  (void)handler_.handleBegin(reinterpret_cast<const std::uint8_t*>(&BEGIN), sizeof(BEGIN));

  const std::array<std::uint8_t, 1> TINY{0};
  const std::uint8_t STATUS = handler_.handleChunk(TINY.data(), TINY.size());
  EXPECT_EQ(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::INVALID_PAYLOAD));
}

/* ----------------------------- FILE_END Tests ----------------------------- */

/** @test FILE_END without active transfer returns NO_TRANSFER. */
TEST_F(FileTransferHandlerTest, EndWithoutTransfer) {
  std::array<std::uint8_t, sizeof(aproto::FileEndResponse)> resp{};
  std::size_t respLen = 0;
  const std::uint8_t STATUS = handler_.handleEnd(resp.data(), respLen);
  EXPECT_EQ(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::NO_TRANSFER));
}

/* ----------------------------- Complete Transfer Tests ----------------------------- */

/** @test Single-chunk file transfer succeeds and creates destination file. */
TEST_F(FileTransferHandlerTest, SingleChunkTransfer) {
  const std::vector<std::uint8_t> DATA{0x48, 0x65, 0x6C, 0x6C, 0x6F};
  const std::uint8_t STATUS = sendFile(DATA, "output/hello.bin", 64);
  EXPECT_EQ(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS));

  // Verify file exists and matches.
  const fs::path DEST = testRoot_ / "output/hello.bin";
  ASSERT_TRUE(fs::exists(DEST));

  std::ifstream f(DEST, std::ios::binary);
  std::vector<std::uint8_t> contents((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
  EXPECT_EQ(contents, DATA);
}

/** @test Multi-chunk file transfer succeeds. */
TEST_F(FileTransferHandlerTest, MultiChunkTransfer) {
  // 10 bytes, 3 bytes per chunk = 4 chunks (3+3+3+1).
  const std::vector<std::uint8_t> DATA{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  const std::uint8_t STATUS = sendFile(DATA, "multi.bin", 3);
  EXPECT_EQ(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS));

  const fs::path DEST = testRoot_ / "multi.bin";
  ASSERT_TRUE(fs::exists(DEST));

  std::ifstream f(DEST, std::ios::binary);
  std::vector<std::uint8_t> contents((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
  EXPECT_EQ(contents, DATA);
}

/** @test Transfer with wrong CRC returns CRC_MISMATCH. */
TEST_F(FileTransferHandlerTest, CrcMismatch) {
  const std::vector<std::uint8_t> DATA{0x41, 0x42, 0x43};
  auto begin = makeBeginPayload(DATA, "test.bin", 64);
  begin.crc32 = 0xDEADBEEF; // Wrong CRC.

  (void)handler_.handleBegin(reinterpret_cast<const std::uint8_t*>(&begin), sizeof(begin));

  // Send chunk.
  std::vector<std::uint8_t> chunk(2 + DATA.size());
  const std::uint16_t IDX = 0;
  std::memcpy(chunk.data(), &IDX, 2);
  std::memcpy(chunk.data() + 2, DATA.data(), DATA.size());
  (void)handler_.handleChunk(chunk.data(), chunk.size());

  // End should fail with CRC mismatch.
  std::array<std::uint8_t, sizeof(aproto::FileEndResponse)> resp{};
  std::size_t respLen = 0;
  const std::uint8_t STATUS = handler_.handleEnd(resp.data(), respLen);
  EXPECT_EQ(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::CRC_MISMATCH));
  EXPECT_GT(respLen, 0U);
}

/** @test Transfer to nested directory creates parent directories. */
TEST_F(FileTransferHandlerTest, NestedDirectoryCreation) {
  const std::vector<std::uint8_t> DATA{0x01, 0x02};
  const std::uint8_t STATUS = sendFile(DATA, "a/b/c/deep.bin", 64);
  EXPECT_EQ(STATUS, static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS));

  const fs::path DEST = testRoot_ / "a/b/c/deep.bin";
  ASSERT_TRUE(fs::exists(DEST));
}

/* ----------------------------- FILE_ABORT Tests ----------------------------- */

/** @test FILE_ABORT resets state and allows new transfer. */
TEST_F(FileTransferHandlerTest, AbortResetsState) {
  const std::vector<std::uint8_t> DATA{0x41, 0x42, 0x43};
  const auto BEGIN = makeBeginPayload(DATA, "test.bin", 64);
  (void)handler_.handleBegin(reinterpret_cast<const std::uint8_t*>(&BEGIN), sizeof(BEGIN));

  const std::uint8_t ABORT_STATUS = handler_.handleAbort();
  EXPECT_EQ(ABORT_STATUS, static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS));

  // Should be able to start a new transfer now.
  const std::uint8_t BEGIN_STATUS =
      handler_.handleBegin(reinterpret_cast<const std::uint8_t*>(&BEGIN), sizeof(BEGIN));
  EXPECT_EQ(BEGIN_STATUS, static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS));
}

/* ----------------------------- FILE_STATUS Tests ----------------------------- */

/** @test FILE_STATUS returns current transfer state. */
TEST_F(FileTransferHandlerTest, StatusReportsProgress) {
  // Idle status.
  std::array<std::uint8_t, sizeof(aproto::FileStatusResponse)> resp{};
  std::size_t respLen = 0;
  (void)handler_.handleStatus(resp.data(), respLen);
  EXPECT_EQ(respLen, sizeof(aproto::FileStatusResponse));

  aproto::FileStatusResponse statusResp{};
  std::memcpy(&statusResp, resp.data(), sizeof(statusResp));
  EXPECT_EQ(statusResp.state, static_cast<std::uint8_t>(aproto::FileTransferState::IDLE));

  // Start transfer.
  const std::vector<std::uint8_t> DATA{0x41, 0x42, 0x43, 0x44};
  const auto BEGIN = makeBeginPayload(DATA, "test.bin", 2);
  (void)handler_.handleBegin(reinterpret_cast<const std::uint8_t*>(&BEGIN), sizeof(BEGIN));

  // Send one chunk.
  std::array<std::uint8_t, 4> chunk{};
  const std::uint16_t IDX = 0;
  std::memcpy(chunk.data(), &IDX, 2);
  chunk[2] = 0x41;
  chunk[3] = 0x42;
  (void)handler_.handleChunk(chunk.data(), chunk.size());

  // Check status mid-transfer.
  (void)handler_.handleStatus(resp.data(), respLen);
  std::memcpy(&statusResp, resp.data(), sizeof(statusResp));
  EXPECT_EQ(statusResp.state, static_cast<std::uint8_t>(aproto::FileTransferState::RECEIVING));
  EXPECT_EQ(statusResp.chunksReceived, 1);
  EXPECT_EQ(statusResp.bytesReceived, 2U);
}

/** @test Back-to-back transfers succeed. */
TEST_F(FileTransferHandlerTest, BackToBackTransfers) {
  const std::vector<std::uint8_t> DATA1{0x01, 0x02, 0x03};
  const std::vector<std::uint8_t> DATA2{0x04, 0x05, 0x06, 0x07};

  EXPECT_EQ(sendFile(DATA1, "file1.bin", 64),
            static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS));
  EXPECT_EQ(sendFile(DATA2, "file2.bin", 64),
            static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS));

  // Both files should exist.
  EXPECT_TRUE(fs::exists(testRoot_ / "file1.bin"));
  EXPECT_TRUE(fs::exists(testRoot_ / "file2.bin"));
}

/** @test Overwriting an existing file succeeds. */
TEST_F(FileTransferHandlerTest, OverwriteExistingFile) {
  const std::vector<std::uint8_t> DATA1{0x01, 0x02};
  const std::vector<std::uint8_t> DATA2{0x03, 0x04, 0x05};

  EXPECT_EQ(sendFile(DATA1, "overwrite.bin", 64),
            static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS));
  EXPECT_EQ(sendFile(DATA2, "overwrite.bin", 64),
            static_cast<std::uint8_t>(aproto::NakStatus::SUCCESS));

  // File should contain DATA2.
  const fs::path DEST = testRoot_ / "overwrite.bin";
  std::ifstream f(DEST, std::ios::binary);
  std::vector<std::uint8_t> contents((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
  EXPECT_EQ(contents, DATA2);
}
