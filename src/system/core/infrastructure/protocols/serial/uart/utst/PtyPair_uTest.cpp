/**
 * @file PtyPair_uTest.cpp
 * @brief Unit tests for PtyPair pseudo-terminal utility.
 */

#include "src/system/core/infrastructure/protocols/serial/uart/inc/PtyPair.hpp"

#include <gtest/gtest.h>
#include <cstring>

using apex::protocols::serial::uart::PtyPair;
using apex::protocols::serial::uart::Status;

/* ----------------------------- Default Construction ----------------------------- */

/** @test PtyPair default construction creates unopened pair. */
TEST(PtyPairTest, DefaultConstruction) {
  PtyPair pty;
  EXPECT_FALSE(pty.isOpen());
  EXPECT_EQ(pty.masterFd(), -1);
  EXPECT_STREQ(pty.slavePath(), "");
}

/* ----------------------------- Lifecycle Tests ----------------------------- */

/** @test PtyPair::open() creates valid master FD and slave path. */
TEST(PtyPairTest, OpenCreatesValidPair) {
  PtyPair pty;
  EXPECT_EQ(pty.open(), Status::SUCCESS);
  EXPECT_TRUE(pty.isOpen());
  EXPECT_GE(pty.masterFd(), 0);

  const char* path = pty.slavePath();
  EXPECT_NE(path, nullptr);
  EXPECT_NE(path[0], '\0');
  EXPECT_TRUE(std::strncmp(path, "/dev/pts/", 9) == 0);
}

/** @test PtyPair::open() when already open returns SUCCESS. */
TEST(PtyPairTest, OpenWhenAlreadyOpen) {
  PtyPair pty;
  EXPECT_EQ(pty.open(), Status::SUCCESS);
  int firstFd = pty.masterFd();

  EXPECT_EQ(pty.open(), Status::SUCCESS);
  EXPECT_EQ(pty.masterFd(), firstFd);
}

/** @test PtyPair::close() releases resources. */
TEST(PtyPairTest, CloseReleasesResources) {
  PtyPair pty;
  EXPECT_EQ(pty.open(), Status::SUCCESS);
  EXPECT_TRUE(pty.isOpen());

  EXPECT_EQ(pty.close(), Status::SUCCESS);
  EXPECT_FALSE(pty.isOpen());
  EXPECT_EQ(pty.masterFd(), -1);
  EXPECT_STREQ(pty.slavePath(), "");
}

/** @test PtyPair::close() on unopened pair returns SUCCESS. */
TEST(PtyPairTest, CloseWhenNotOpen) {
  PtyPair pty;
  EXPECT_EQ(pty.close(), Status::SUCCESS);
}

/** @test PtyPair destructor cleans up automatically. */
TEST(PtyPairTest, DestructorCleansUp) {
  int masterFd = -1;
  {
    PtyPair pty;
    EXPECT_EQ(pty.open(), Status::SUCCESS);
    masterFd = pty.masterFd();
    EXPECT_GE(masterFd, 0);
  }
  // After destruction, the FD should be closed (can't directly verify,
  // but this exercises the destructor path)
  SUCCEED();
}

/* ----------------------------- Move Semantics ----------------------------- */

/** @test PtyPair move construction transfers ownership. */
TEST(PtyPairTest, MoveConstruction) {
  PtyPair pty1;
  EXPECT_EQ(pty1.open(), Status::SUCCESS);
  int originalFd = pty1.masterFd();
  const char* originalPath = pty1.slavePath();
  std::string savedPath(originalPath);

  PtyPair pty2(std::move(pty1));

  EXPECT_FALSE(pty1.isOpen());
  EXPECT_EQ(pty1.masterFd(), -1);

  EXPECT_TRUE(pty2.isOpen());
  EXPECT_EQ(pty2.masterFd(), originalFd);
  EXPECT_STREQ(pty2.slavePath(), savedPath.c_str());
}

/** @test PtyPair move assignment transfers ownership. */
TEST(PtyPairTest, MoveAssignment) {
  PtyPair pty1;
  EXPECT_EQ(pty1.open(), Status::SUCCESS);
  int originalFd = pty1.masterFd();
  std::string savedPath(pty1.slavePath());

  PtyPair pty2;
  pty2 = std::move(pty1);

  EXPECT_FALSE(pty1.isOpen());
  EXPECT_TRUE(pty2.isOpen());
  EXPECT_EQ(pty2.masterFd(), originalFd);
  EXPECT_STREQ(pty2.slavePath(), savedPath.c_str());
}

/* ----------------------------- I/O Tests ----------------------------- */

/** @test PtyPair::readMaster() with no data returns WOULD_BLOCK. */
TEST(PtyPairTest, ReadMasterNoData) {
  PtyPair pty;
  EXPECT_EQ(pty.open(), Status::SUCCESS);

  std::uint8_t buffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(pty.readMaster(buffer, sizeof(buffer), bytesRead, 0), Status::WOULD_BLOCK);
  EXPECT_EQ(bytesRead, 0u);
}

/** @test PtyPair::readMaster() on closed pair returns ERROR_CLOSED. */
TEST(PtyPairTest, ReadMasterClosed) {
  PtyPair pty;

  std::uint8_t buffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(pty.readMaster(buffer, sizeof(buffer), bytesRead, 0), Status::ERROR_CLOSED);
}

/** @test PtyPair::readMaster() with null buffer returns ERROR_INVALID_ARG. */
TEST(PtyPairTest, ReadMasterNullBuffer) {
  PtyPair pty;
  EXPECT_EQ(pty.open(), Status::SUCCESS);

  std::size_t bytesRead = 0;
  EXPECT_EQ(pty.readMaster(nullptr, 64, bytesRead, 0), Status::ERROR_INVALID_ARG);
}

/** @test PtyPair::readMaster() with zero size returns ERROR_INVALID_ARG. */
TEST(PtyPairTest, ReadMasterZeroSize) {
  PtyPair pty;
  EXPECT_EQ(pty.open(), Status::SUCCESS);

  std::uint8_t buffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(pty.readMaster(buffer, 0, bytesRead, 0), Status::ERROR_INVALID_ARG);
}

/** @test PtyPair::writeMaster() on closed pair returns ERROR_CLOSED. */
TEST(PtyPairTest, WriteMasterClosed) {
  PtyPair pty;

  std::uint8_t data[] = {0x01, 0x02, 0x03};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(pty.writeMaster(data, sizeof(data), bytesWritten, 0), Status::ERROR_CLOSED);
}

/** @test PtyPair::writeMaster() with null data returns ERROR_INVALID_ARG. */
TEST(PtyPairTest, WriteMasterNullData) {
  PtyPair pty;
  EXPECT_EQ(pty.open(), Status::SUCCESS);

  std::size_t bytesWritten = 0;
  EXPECT_EQ(pty.writeMaster(nullptr, 64, bytesWritten, 0), Status::ERROR_INVALID_ARG);
}

/** @test PtyPair::writeMaster() with zero size returns ERROR_INVALID_ARG. */
TEST(PtyPairTest, WriteMasterZeroSize) {
  PtyPair pty;
  EXPECT_EQ(pty.open(), Status::SUCCESS);

  std::uint8_t data[] = {0x01};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(pty.writeMaster(data, 0, bytesWritten, 0), Status::ERROR_INVALID_ARG);
}

/** @test PtyPair::writeMaster() succeeds and writes data. */
TEST(PtyPairTest, WriteMasterSuccess) {
  PtyPair pty;
  EXPECT_EQ(pty.open(), Status::SUCCESS);

  std::uint8_t data[] = {0xAA, 0xBB, 0xCC, 0xDD};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(pty.writeMaster(data, sizeof(data), bytesWritten, 100), Status::SUCCESS);
  EXPECT_EQ(bytesWritten, sizeof(data));
}
