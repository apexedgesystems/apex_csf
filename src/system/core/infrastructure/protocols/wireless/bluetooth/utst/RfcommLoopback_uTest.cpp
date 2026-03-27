/**
 * @file RfcommLoopback_uTest.cpp
 * @brief Unit tests for RfcommLoopback.
 */

#include "RfcommLoopback.hpp"

#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <thread>

namespace bt = apex::protocols::wireless::bluetooth;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify default construction creates closed loopback. */
TEST(RfcommLoopback, DefaultConstruction) {
  bt::RfcommLoopback loopback;

  EXPECT_FALSE(loopback.isOpen());
  EXPECT_EQ(loopback.serverFd(), -1);
  EXPECT_FALSE(loopback.clientReleased());
}

/* ----------------------------- Lifecycle Tests ----------------------------- */

/** @test Verify open creates valid socket pair. */
TEST(RfcommLoopback, OpenCreatesSocketPair) {
  bt::RfcommLoopback loopback;
  ASSERT_EQ(loopback.open(), bt::Status::SUCCESS);

  EXPECT_TRUE(loopback.isOpen());
  EXPECT_GE(loopback.serverFd(), 0);
  EXPECT_FALSE(loopback.clientReleased());
}

/** @test Verify open fails when already open. */
TEST(RfcommLoopback, OpenFailsWhenAlreadyOpen) {
  bt::RfcommLoopback loopback;
  ASSERT_EQ(loopback.open(), bt::Status::SUCCESS);

  EXPECT_EQ(loopback.open(), bt::Status::ERROR_ALREADY_CONNECTED);
}

/** @test Verify close cleans up. */
TEST(RfcommLoopback, CloseCleanesUp) {
  bt::RfcommLoopback loopback;
  ASSERT_EQ(loopback.open(), bt::Status::SUCCESS);

  loopback.close();

  EXPECT_FALSE(loopback.isOpen());
  EXPECT_EQ(loopback.serverFd(), -1);
}

/** @test Verify destructor closes sockets. */
TEST(RfcommLoopback, DestructorCloses) {
  int serverFd = -1;
  {
    bt::RfcommLoopback loopback;
    ASSERT_EQ(loopback.open(), bt::Status::SUCCESS);
    serverFd = loopback.serverFd();
    EXPECT_GE(serverFd, 0);
  }
  // After destruction, FD should be closed
  // We can't easily verify this without platform-specific checks
  // but at least we verify no crashes
}

/* ----------------------------- Client FD Release Tests ----------------------------- */

/** @test Verify releaseClientFd returns valid FD. */
TEST(RfcommLoopback, ReleaseClientFdReturnsValidFd) {
  bt::RfcommLoopback loopback;
  ASSERT_EQ(loopback.open(), bt::Status::SUCCESS);

  int clientFd = loopback.releaseClientFd();

  EXPECT_GE(clientFd, 0);
  EXPECT_TRUE(loopback.clientReleased());

  // Clean up manually since we own the FD now
  ::close(clientFd);
}

/** @test Verify releaseClientFd returns -1 on second call. */
TEST(RfcommLoopback, ReleaseClientFdSecondCallFails) {
  bt::RfcommLoopback loopback;
  ASSERT_EQ(loopback.open(), bt::Status::SUCCESS);

  int fd1 = loopback.releaseClientFd();
  int fd2 = loopback.releaseClientFd();

  EXPECT_GE(fd1, 0);
  EXPECT_EQ(fd2, -1);

  ::close(fd1);
}

/** @test Verify releaseClientFd returns -1 when not open. */
TEST(RfcommLoopback, ReleaseClientFdWhenNotOpen) {
  bt::RfcommLoopback loopback;
  EXPECT_EQ(loopback.releaseClientFd(), -1);
}

/* ----------------------------- I/O Tests ----------------------------- */

/** @test Verify server write and read via released client FD. */
TEST(RfcommLoopback, ServerWriteClientRead) {
  bt::RfcommLoopback loopback;
  ASSERT_EQ(loopback.open(), bt::Status::SUCCESS);

  int clientFd = loopback.releaseClientFd();
  ASSERT_GE(clientFd, 0);

  // Write from server
  const std::uint8_t writeData[] = {0xDE, 0xAD, 0xBE, 0xEF};
  std::size_t written = 0;
  ASSERT_EQ(loopback.serverWrite({writeData, sizeof(writeData)}, written, 100),
            bt::Status::SUCCESS);
  EXPECT_EQ(written, sizeof(writeData));

  // Read from client
  std::uint8_t readBuf[16];
  ssize_t n = ::read(clientFd, readBuf, sizeof(readBuf));
  EXPECT_EQ(n, 4);
  EXPECT_EQ(std::memcmp(readBuf, writeData, 4), 0);

  ::close(clientFd);
}

/** @test Verify client write and server read. */
TEST(RfcommLoopback, ClientWriteServerRead) {
  bt::RfcommLoopback loopback;
  ASSERT_EQ(loopback.open(), bt::Status::SUCCESS);

  int clientFd = loopback.releaseClientFd();
  ASSERT_GE(clientFd, 0);

  // Write from client
  const std::uint8_t writeData[] = {0xCA, 0xFE, 0xBA, 0xBE};
  ssize_t n = ::write(clientFd, writeData, sizeof(writeData));
  EXPECT_EQ(n, 4);

  // Read from server
  std::uint8_t readBuf[16];
  std::size_t bytesRead = 0;
  ASSERT_EQ(loopback.serverRead({readBuf, sizeof(readBuf)}, bytesRead, 100), bt::Status::SUCCESS);
  EXPECT_EQ(bytesRead, sizeof(writeData));
  EXPECT_EQ(std::memcmp(readBuf, writeData, 4), 0);

  ::close(clientFd);
}

/** @test Verify serverRead returns WOULD_BLOCK with no data. */
TEST(RfcommLoopback, ServerReadWouldBlock) {
  bt::RfcommLoopback loopback;
  ASSERT_EQ(loopback.open(), bt::Status::SUCCESS);

  std::uint8_t buf[16];
  std::size_t bytesRead = 0;

  // Non-blocking poll (timeout=0)
  bt::Status status = loopback.serverRead({buf, sizeof(buf)}, bytesRead, 0);
  EXPECT_EQ(status, bt::Status::WOULD_BLOCK);
  EXPECT_EQ(bytesRead, 0u);
}

/** @test Verify serverWrite with empty data succeeds. */
TEST(RfcommLoopback, ServerWriteEmptyData) {
  bt::RfcommLoopback loopback;
  ASSERT_EQ(loopback.open(), bt::Status::SUCCESS);

  std::size_t written = 999;
  apex::compat::bytes_span empty;
  bt::Status status = loopback.serverWrite(empty, written, 0);
  EXPECT_EQ(status, bt::Status::SUCCESS);
  EXPECT_EQ(written, 0u);
}

/** @test Verify serverRead with empty buffer succeeds. */
TEST(RfcommLoopback, ServerReadEmptyBuffer) {
  bt::RfcommLoopback loopback;
  ASSERT_EQ(loopback.open(), bt::Status::SUCCESS);

  std::size_t bytesRead = 999;
  apex::compat::mutable_bytes_span empty;
  bt::Status status = loopback.serverRead(empty, bytesRead, 0);
  EXPECT_EQ(status, bt::Status::SUCCESS);
  EXPECT_EQ(bytesRead, 0u);
}

/** @test Verify serverRead fails when not connected. */
TEST(RfcommLoopback, ServerReadNotConnected) {
  bt::RfcommLoopback loopback;

  std::uint8_t buf[16];
  std::size_t bytesRead = 0;
  EXPECT_EQ(loopback.serverRead({buf, sizeof(buf)}, bytesRead, 0), bt::Status::ERROR_NOT_CONNECTED);
}

/** @test Verify serverWrite fails when not connected. */
TEST(RfcommLoopback, ServerWriteNotConnected) {
  bt::RfcommLoopback loopback;

  const std::uint8_t data[] = {0x01};
  std::size_t written = 0;
  EXPECT_EQ(loopback.serverWrite({data, sizeof(data)}, written, 0),
            bt::Status::ERROR_NOT_CONNECTED);
}

/* ----------------------------- Move Semantics Tests ----------------------------- */

/** @test Verify move construction. */
TEST(RfcommLoopback, MoveConstruction) {
  bt::RfcommLoopback loopback1;
  ASSERT_EQ(loopback1.open(), bt::Status::SUCCESS);
  int originalFd = loopback1.serverFd();

  bt::RfcommLoopback loopback2(std::move(loopback1));

  EXPECT_FALSE(loopback1.isOpen());
  EXPECT_TRUE(loopback2.isOpen());
  EXPECT_EQ(loopback2.serverFd(), originalFd);
}

/** @test Verify move assignment. */
TEST(RfcommLoopback, MoveAssignment) {
  bt::RfcommLoopback loopback1;
  ASSERT_EQ(loopback1.open(), bt::Status::SUCCESS);
  int originalFd = loopback1.serverFd();

  bt::RfcommLoopback loopback2;
  loopback2 = std::move(loopback1);

  EXPECT_FALSE(loopback1.isOpen());
  EXPECT_TRUE(loopback2.isOpen());
  EXPECT_EQ(loopback2.serverFd(), originalFd);
}
