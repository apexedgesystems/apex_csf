/**
 * @file UnixSocket_uTest.cpp
 * @brief Unit tests for the Unix domain socket server and client.
 *
 * These tests verify that the Unix socket server correctly accepts connections,
 * handles data echoing, and supports multiple concurrent clients.
 */

#include "src/system/core/infrastructure/protocols/network/unix/inc/UnixSocketClient.hpp"
#include "src/system/core/infrastructure/protocols/network/unix/inc/UnixSocketServer.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using apex::protocols::unix_socket::ConnectionStats;
using apex::protocols::unix_socket::UNIX_CLIENT_SUCCESS;
using apex::protocols::unix_socket::UNIX_SERVER_SUCCESS;
using apex::protocols::unix_socket::UnixSocketClient;
using apex::protocols::unix_socket::UnixSocketMode;
using apex::protocols::unix_socket::UnixSocketServer;

/* ----------------------------- Test Helpers ----------------------------- */

namespace {

/**
 * @brief Echo callback context for Delegate-based callbacks.
 */
struct EchoContext {
  UnixSocketServer* server;
};

/**
 * @brief RT-safe echo callback for UnixSocketServer.
 */
void echoCallback(void* ctx, int clientfd) noexcept {
  auto* echoctx = static_cast<EchoContext*>(ctx);
  std::array<uint8_t, 1024> buf{};
  apex::compat::bytes_span span(buf.data(), buf.size());
  ssize_t nread = echoctx->server->read(clientfd, span);
  if (nread > 0) {
    apex::compat::bytes_span out(buf.data(), static_cast<size_t>(nread));
    (void)echoctx->server->write(clientfd, out);
  }
}

/**
 * @brief Generate a unique socket path for testing.
 */
std::string uniqueSocketPath(const char* prefix) {
  static std::atomic<int> counter{0};
  return std::string("/tmp/") + prefix + "_" + std::to_string(::getpid()) + "_" +
         std::to_string(counter.fetch_add(1)) + ".sock";
}

} // namespace

/* ----------------------------- Server Tests ----------------------------- */

/**
 * @test UnixSocketServerTest.InitSuccess
 * @brief Verifies that the Unix socket server initializes successfully.
 */
TEST(UnixSocketServerTest, InitSuccess) {
  const std::string PATH = uniqueSocketPath("init_test");

  UnixSocketServer server(PATH);
  uint8_t status = server.init(true);
  EXPECT_EQ(status, UNIX_SERVER_SUCCESS);
  EXPECT_EQ(server.path(), PATH);
}

/**
 * @test UnixSocketServerTest.PathTooLong
 * @brief Verifies that the server rejects paths exceeding sun_path limit.
 */
TEST(UnixSocketServerTest, PathTooLong) {
  // sun_path is typically 108 bytes; create a path exceeding this
  std::string longPath(200, 'x');
  UnixSocketServer server(longPath);
  std::string error;
  uint8_t status = server.init(true, &error);
  EXPECT_NE(status, UNIX_SERVER_SUCCESS);
  EXPECT_FALSE(error.empty());
}

/* ----------------------------- Echo Tests ----------------------------- */

/**
 * @test UnixSocketServerTest.SingleConnection
 * @brief Verifies that the Unix server accepts a single connection and correctly echoes data.
 */
TEST(UnixSocketServerTest, SingleConnection) {
  const std::string PATH = uniqueSocketPath("single_conn");

  // Create and initialize the server
  UnixSocketServer server(PATH);
  uint8_t srvInitStatus = server.init(true);
  ASSERT_EQ(srvInitStatus, UNIX_SERVER_SUCCESS);

  // Register echo-on-read callback
  EchoContext echoCtx{&server};
  server.setOnClientReadable(apex::concurrency::Delegate<void, int>{echoCallback, &echoCtx});

  // Event loop thread
  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed)) {
      server.processEvents(100);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Small delay to ensure server is ready
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Client: send/receive echo test
  {
    UnixSocketClient client(PATH);
    uint8_t cliInitStatus = client.init();
    ASSERT_EQ(cliInitStatus, UNIX_CLIENT_SUCCESS);

    // Prepare test payload
    std::array<uint8_t, 4> expected = {0xDE, 0xAD, 0xBE, 0xEF};
    apex::compat::bytes_span writeSpan(expected.data(), expected.size());
    ssize_t nwrite = client.write(writeSpan);
    EXPECT_EQ(nwrite, static_cast<ssize_t>(expected.size()));

    // Wait for echo
    bool readable = client.waitReadable(1000);
    EXPECT_TRUE(readable);

    std::array<uint8_t, 32> echoBuf{};
    apex::compat::bytes_span readSpan(echoBuf.data(), echoBuf.size());
    ssize_t nread = client.read(readSpan);
    EXPECT_EQ(nread, static_cast<ssize_t>(expected.size()));
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), echoBuf.begin()));

    // Verify stats
    ConnectionStats stats = client.stats();
    EXPECT_EQ(stats.bytesTx, expected.size());
    EXPECT_EQ(stats.bytesRx, expected.size());
    EXPECT_EQ(stats.packetsTx, 1U);
    EXPECT_EQ(stats.packetsRx, 1U);
  }

  // Shutdown event loop
  ioExit = true;
  server.stop();
  ioThread.join();
}

/**
 * @test UnixSocketServerTest.MultipleClients
 * @brief Verifies that the Unix server handles multiple concurrent client connections.
 */
TEST(UnixSocketServerTest, MultipleClients) {
  const std::string PATH = uniqueSocketPath("multi_conn");

  // Create and initialize the server
  UnixSocketServer server(PATH);
  uint8_t srvInitStatus = server.init(true);
  ASSERT_EQ(srvInitStatus, UNIX_SERVER_SUCCESS);

  // Echo-on-read callback
  EchoContext echoCtx{&server};
  server.setOnClientReadable(apex::concurrency::Delegate<void, int>{echoCallback, &echoCtx});

  // Event loop thread
  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed)) {
      server.processEvents(100);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Small delay
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Launch multiple client threads
  const size_t NUM_CLIENTS = 10;
  std::vector<std::thread> clientThreads;
  clientThreads.reserve(NUM_CLIENTS);
  std::atomic_size_t successfulClients{0};
  std::array<uint8_t, 2> expected = {0xBE, 0xEF};

  for (size_t i = 0; i < NUM_CLIENTS; ++i) {
    clientThreads.emplace_back([&, i]() {
      UnixSocketClient client(PATH);
      uint8_t cliInitStatus = client.init();
      EXPECT_EQ(cliInitStatus, UNIX_CLIENT_SUCCESS) << "Client " << i << " init failed";

      apex::compat::bytes_span writeSpan(expected.data(), expected.size());
      ssize_t nwrite = client.write(writeSpan);
      EXPECT_EQ(nwrite, static_cast<ssize_t>(expected.size())) << "Client " << i << " write failed";

      bool readable = client.waitReadable(1000);
      EXPECT_TRUE(readable) << "Client " << i << " not readable";

      std::array<uint8_t, 32> echoBuf{};
      apex::compat::bytes_span readSpan(echoBuf.data(), echoBuf.size());
      ssize_t nread = client.read(readSpan);
      EXPECT_EQ(nread, static_cast<ssize_t>(expected.size())) << "Client " << i << " read failed";
      EXPECT_TRUE(std::equal(expected.begin(), expected.end(), echoBuf.begin()))
          << "Client " << i << " echo mismatch";

      successfulClients.fetch_add(1, std::memory_order_relaxed);
    });
  }

  for (auto& th : clientThreads)
    th.join();
  EXPECT_EQ(successfulClients.load(), NUM_CLIENTS);

  // Shutdown
  ioExit = true;
  server.stop();
  ioThread.join();
}

/* ----------------------------- Max Connections Tests ----------------------------- */

/**
 * @test UnixSocketServerTest.MaxConnections
 * @brief Verifies that the server enforces max connections limit.
 */
TEST(UnixSocketServerTest, MaxConnections) {
  const std::string PATH = uniqueSocketPath("max_conn");

  UnixSocketServer server(PATH);
  uint8_t srvInitStatus = server.init(true);
  ASSERT_EQ(srvInitStatus, UNIX_SERVER_SUCCESS);

  // Limit to 2 connections
  server.setMaxConnections(2);
  EXPECT_EQ(server.maxConnections(), 2U);

  // Event loop thread
  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed)) {
      server.processEvents(50);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Connect 2 clients (should succeed)
  UnixSocketClient client1(PATH);
  UnixSocketClient client2(PATH);
  uint8_t status1 = client1.init();
  uint8_t status2 = client2.init();
  EXPECT_EQ(status1, UNIX_CLIENT_SUCCESS);
  EXPECT_EQ(status2, UNIX_CLIENT_SUCCESS);

  // Give server time to process accepts
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_EQ(server.connectionCount(), 2U);

  // Third client: connection may succeed at kernel level but server will reject
  // after accept (closing it immediately). The client might see ECONNRESET or similar.
  // For this test we just verify the server count stays at 2.
  UnixSocketClient client3(PATH);
  (void)client3.init(); // May succeed or fail depending on timing

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_LE(server.connectionCount(), 2U);

  ioExit = true;
  server.stop();
  ioThread.join();
}

/* ----------------------------- Statistics Tests ----------------------------- */

/**
 * @test UnixSocketServerTest.Statistics
 * @brief Verifies that the server correctly tracks connection statistics.
 */
TEST(UnixSocketServerTest, Statistics) {
  const std::string PATH = uniqueSocketPath("stats_test");

  UnixSocketServer server(PATH);
  uint8_t srvInitStatus = server.init(true);
  ASSERT_EQ(srvInitStatus, UNIX_SERVER_SUCCESS);

  // Echo callback
  EchoContext echoCtx{&server};
  server.setOnClientReadable(apex::concurrency::Delegate<void, int>{echoCallback, &echoCtx});

  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed)) {
      server.processEvents(100);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Initial stats should be zero (except connectedAtNs)
  ConnectionStats initialStats = server.stats();
  EXPECT_EQ(initialStats.bytesRx, 0U);
  EXPECT_EQ(initialStats.bytesTx, 0U);
  EXPECT_GT(initialStats.connectedAtNs, 0);

  // Connect and send data
  {
    UnixSocketClient client(PATH);
    uint8_t cliInitStatus = client.init();
    ASSERT_EQ(cliInitStatus, UNIX_CLIENT_SUCCESS);

    std::array<uint8_t, 100> data{};
    std::fill(data.begin(), data.end(), 0xAB);
    apex::compat::bytes_span writeSpan(data.data(), data.size());
    ssize_t nwrite = client.write(writeSpan);
    EXPECT_EQ(nwrite, 100);

    client.waitReadable(1000);
    std::array<uint8_t, 128> buf{};
    apex::compat::bytes_span readSpan(buf.data(), buf.size());
    (void)client.read(readSpan);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Check stats
  ConnectionStats finalStats = server.stats();
  EXPECT_EQ(finalStats.bytesRx, 100U);
  EXPECT_EQ(finalStats.bytesTx, 100U);
  EXPECT_EQ(finalStats.packetsRx, 1U);
  EXPECT_EQ(finalStats.packetsTx, 1U);
  EXPECT_GT(finalStats.lastActivityNs, initialStats.connectedAtNs);

  // Reset stats
  server.resetStats();
  ConnectionStats resetStats = server.stats();
  EXPECT_EQ(resetStats.bytesRx, 0U);
  EXPECT_EQ(resetStats.bytesTx, 0U);

  ioExit = true;
  server.stop();
  ioThread.join();
}

/* ----------------------------- Additional Tests ----------------------------- */

/**
 * @test UnixSocketServerTest.ServerConnectionCallbacks
 * @brief Exercises setOnNewConnection, setOnConnectionClosed, and setOnClientWritable.
 */
TEST(UnixSocketServerTest, ServerConnectionCallbacks) {
  const std::string PATH = uniqueSocketPath("cb_test");

  UnixSocketServer server(PATH);
  ASSERT_EQ(server.init(true), UNIX_SERVER_SUCCESS);

  std::atomic_int newConnFired{0};
  std::atomic_int closedFired{0};

  server.setOnNewConnection(apex::concurrency::Delegate<void, int>{
      [](void* ctx, int /*fd*/) noexcept {
        static_cast<std::atomic_int*>(ctx)->fetch_add(1, std::memory_order_relaxed);
      },
      &newConnFired});

  server.setOnConnectionClosed(apex::concurrency::Delegate<void, int>{
      [](void* ctx, int /*fd*/) noexcept {
        static_cast<std::atomic_int*>(ctx)->fetch_add(1, std::memory_order_relaxed);
      },
      &closedFired});

  server.setOnClientWritable(apex::concurrency::Delegate<void, int>{});

  EchoContext echoCtx{&server};
  server.setOnClientReadable(apex::concurrency::Delegate<void, int>{echoCallback, &echoCtx});

  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed)) {
      server.processEvents(50);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  {
    UnixSocketClient client(PATH);
    ASSERT_EQ(client.init(), UNIX_CLIENT_SUCCESS);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    // client destructor closes connection
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_GT(newConnFired.load(), 0);

  ioExit = true;
  server.stop();
  ioThread.join();

  EXPECT_GT(closedFired.load(), 0);
}

/**
 * @test UnixSocketServerTest.ClientProcessEventsAndStop
 * @brief Exercises client processEvents() event loop with callbacks and stop().
 */
TEST(UnixSocketServerTest, ClientProcessEventsAndStop) {
  const std::string PATH = uniqueSocketPath("cli_evt");

  UnixSocketServer server(PATH);
  ASSERT_EQ(server.init(true), UNIX_SERVER_SUCCESS);

  EchoContext echoCtx{&server};
  server.setOnClientReadable(apex::concurrency::Delegate<void, int>{echoCallback, &echoCtx});

  std::atomic_bool srvExit{false};
  std::thread srvThread([&]() {
    while (!srvExit.load(std::memory_order_relaxed)) {
      server.processEvents(50);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  UnixSocketClient client(PATH);
  ASSERT_EQ(client.init(), UNIX_CLIENT_SUCCESS);

  std::atomic_int readableFired{0};
  client.setOnReadable(apex::concurrency::Delegate<void>{
      [](void* ctx) noexcept {
        static_cast<std::atomic_int*>(ctx)->fetch_add(1, std::memory_order_relaxed);
      },
      &readableFired});

  client.setOnWritable(apex::concurrency::Delegate<void>{});
  client.setOnDisconnected(apex::concurrency::Delegate<void>{});

  std::atomic_bool cliExited{false};
  std::thread cliThread([&]() {
    client.processEvents(5000);
    cliExited.store(true, std::memory_order_relaxed);
  });

  std::array<uint8_t, 2> pkt = {0x11, 0x22};
  client.write(apex::compat::bytes_span(pkt.data(), pkt.size()));

  const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  while (readableFired.load() == 0 && std::chrono::steady_clock::now() < DEADLINE)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

  EXPECT_GT(readableFired.load(), 0);

  client.stop();
  cliThread.join();
  EXPECT_TRUE(cliExited.load());

  srvExit = true;
  server.stop();
  srvThread.join();
}
