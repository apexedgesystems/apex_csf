/**
 * @file CanEventLoop_uTest.cpp
 * @test CanEventLoop epoll-based event-driven CAN reception.
 */

#include "CanAdapter_TestSupport_uTest.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanBusAdapter.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanEventLoop.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/VCanInterface.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>

using apex::protocols::fieldbus::can::CANBusAdapter;
using apex::protocols::fieldbus::can::CanConfig;
using apex::protocols::fieldbus::can::CanEventCallback;
using apex::protocols::fieldbus::can::CanEventLoop;
using apex::protocols::fieldbus::can::CanFrame;
using apex::protocols::fieldbus::can::Status;
using apex::protocols::fieldbus::can::util::VCanInterface;
using test_support::createTestCANSocket;
using test_support::testIfName;

/* ----------------------------- Initialization Tests ----------------------------- */

/** @test CanEventLoop default construction is uninitialized. */
TEST(CanEventLoopInit, DefaultConstructionUninitialized) {
  CanEventLoop loop;
  EXPECT_FALSE(loop.isInitialized());
  EXPECT_EQ(loop.epollFd(), -1);
  EXPECT_EQ(loop.adapterCount(), 0u);
}

/** @test CanEventLoop::init() creates epoll fd. */
TEST(CanEventLoopInit, InitCreatesEpollFd) {
  CanEventLoop loop;
  EXPECT_TRUE(loop.init());
  EXPECT_TRUE(loop.isInitialized());
  EXPECT_GE(loop.epollFd(), 0);
}

/** @test CanEventLoop::init() is idempotent. */
TEST(CanEventLoopInit, InitIdempotent) {
  CanEventLoop loop;
  EXPECT_TRUE(loop.init());
  int fd1 = loop.epollFd();
  EXPECT_TRUE(loop.init());
  int fd2 = loop.epollFd();
  EXPECT_EQ(fd1, fd2);
}

/* ----------------------------- Add/Remove Tests ----------------------------- */

/** @test CanEventLoop::add() before init returns false. */
TEST(CanEventLoopAddRemove, AddBeforeInitFails) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("EventLoop Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  CanEventLoop loop; // Not initialized
  EXPECT_FALSE(loop.add(&adapter, CanEventCallback{}));
}

/** @test CanEventLoop::add() with nullptr returns false. */
TEST(CanEventLoopAddRemove, AddNullptrFails) {
  CanEventLoop loop;
  ASSERT_TRUE(loop.init());
  EXPECT_FALSE(loop.add(nullptr, CanEventCallback{}));
}

/** @test CanEventLoop::add() with unconfigured adapter returns false. */
TEST(CanEventLoopAddRemove, AddUnconfiguredAdapterFails) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("EventLoop Test", vcan.interfaceName());
  // Not configured - socketFd() returns -1

  CanEventLoop loop;
  ASSERT_TRUE(loop.init());
  EXPECT_FALSE(loop.add(&adapter, CanEventCallback{}));
}

/** @test CanEventLoop::add() succeeds with configured adapter. */
TEST(CanEventLoopAddRemove, AddConfiguredAdapterSucceeds) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("EventLoop Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  CanEventLoop loop;
  ASSERT_TRUE(loop.init());
  EXPECT_TRUE(loop.add(&adapter, CanEventCallback{}));
  EXPECT_EQ(loop.adapterCount(), 1u);
}

/** @test CanEventLoop::add() same adapter twice returns false. */
TEST(CanEventLoopAddRemove, AddSameAdapterTwiceFails) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("EventLoop Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  CanEventLoop loop;
  ASSERT_TRUE(loop.init());
  EXPECT_TRUE(loop.add(&adapter, CanEventCallback{}));
  EXPECT_FALSE(loop.add(&adapter, CanEventCallback{}));
  EXPECT_EQ(loop.adapterCount(), 1u);
}

/** @test CanEventLoop::remove() succeeds for registered adapter. */
TEST(CanEventLoopAddRemove, RemoveRegisteredAdapterSucceeds) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("EventLoop Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  CanEventLoop loop;
  ASSERT_TRUE(loop.init());
  EXPECT_TRUE(loop.add(&adapter, CanEventCallback{}));
  EXPECT_EQ(loop.adapterCount(), 1u);

  EXPECT_TRUE(loop.remove(&adapter));
  EXPECT_EQ(loop.adapterCount(), 0u);
}

/** @test CanEventLoop::remove() returns false for unregistered adapter. */
TEST(CanEventLoopAddRemove, RemoveUnregisteredAdapterFails) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("EventLoop Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  CanEventLoop loop;
  ASSERT_TRUE(loop.init());
  EXPECT_FALSE(loop.remove(&adapter));
}

/* ----------------------------- Poll Tests ----------------------------- */

/** @test CanEventLoop::poll() returns 0 when no adapters registered. */
TEST(CanEventLoopPoll, PollNoAdaptersReturnsZero) {
  CanEventLoop loop;
  ASSERT_TRUE(loop.init());
  EXPECT_EQ(loop.poll(0), 0);
}

/** @test CanEventLoop::poll() returns 0 when no data available. */
TEST(CanEventLoopPoll, PollNoDataReturnsZero) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("EventLoop Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  CanEventLoop loop;
  ASSERT_TRUE(loop.init());
  EXPECT_TRUE(loop.add(&adapter, CanEventCallback{}));

  // No data sent, nonblocking poll should return 0
  EXPECT_EQ(loop.poll(0), 0);
}

/** @test CanEventLoop::poll() invokes callback when data available. */
TEST(CanEventLoopPoll, PollInvokesCallbackOnData) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("EventLoop Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  // Callback state
  struct CallbackState {
    std::atomic<int> callCount{0};
    CANBusAdapter* lastAdapter{nullptr};
  } state;

  auto handler = [](void* ctx, CANBusAdapter* adapter, std::uint32_t /*events*/) noexcept {
    auto* s = static_cast<CallbackState*>(ctx);
    s->callCount++;
    s->lastAdapter = adapter;

    // Drain frames
    CanFrame frame;
    while (adapter->recv(frame, 0) == Status::SUCCESS) {
      // Consume
    }
  };

  CanEventLoop loop;
  ASSERT_TRUE(loop.init());
  EXPECT_TRUE(loop.add(&adapter, CanEventCallback{handler, &state}));

  // Send a frame from external socket
  struct can_frame tx{};
  tx.can_id = 0x100;
  tx.can_dlc = 4;
  ASSERT_EQ(::write(extSock, &tx, sizeof(tx)), (ssize_t)sizeof(tx));

  // Poll should invoke callback
  int dispatched = loop.poll(100);
  EXPECT_GE(dispatched, 1);
  EXPECT_GE(state.callCount.load(), 1);
  EXPECT_EQ(state.lastAdapter, &adapter);

  ::close(extSock);
}

/** @test CanEventLoop::poll() before init returns 0. */
TEST(CanEventLoopPoll, PollBeforeInitReturnsZero) {
  CanEventLoop loop;
  EXPECT_EQ(loop.poll(0), 0);
}
