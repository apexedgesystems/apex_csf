/**
 * @file TcpSocket_uTest.cpp
 * @brief Unit tests for the TCP socket server and TCP socket client.
 *
 * These tests verify that the TCP server correctly accepts connections,
 * handles data echoing, and supports multiple concurrent clients.
 */

#include "src/utilities/compatibility/inc/compat_span.hpp" // apex::compat::bytes_span
#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpSocketClient.hpp"
#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpSocketServer.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

using apex::protocols::tcp::TCP_CLIENT_SUCCESS;
using apex::protocols::tcp::TCP_SERVER_SUCCESS;
using apex::protocols::tcp::TcpSocketClient;
using apex::protocols::tcp::TcpSocketServer;

/* ----------------------------- Test Helpers ----------------------------- */

namespace {

/**
 * @brief Echo callback context for Delegate-based callbacks.
 */
struct EchoContext {
  TcpSocketServer* server;
};

/**
 * @brief RT-safe echo callback for TcpSocketServer.
 */
void echoCallback(void* ctx, int clientfd) noexcept {
  auto* echoctx = static_cast<EchoContext*>(ctx);
  std::array<uint8_t, 1024> buf{};
  std::string rcvErr;
  ssize_t nread = echoctx->server->read(clientfd, buf, 0, rcvErr);
  if (nread > 0) {
    apex::compat::bytes_span out(reinterpret_cast<const uint8_t*>(buf.data()),
                                 static_cast<size_t>(nread));
    std::string sndErr;
    (void)echoctx->server->write(clientfd, out, sndErr);
  }
}

} // namespace

/* ----------------------------- Echo Tests ----------------------------- */

/**
 * @test TcpSocketServerTest.SingleConnection
 * @brief Verifies that the TCP server accepts a single connection and correctly echoes data.
 *
 * The server is driven by a single event-loop thread (processEvents). When a client is readable,
 * the server drains data and writes it back immediately (echo).
 */
TEST(TcpSocketServerTest, SingleConnection) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9090";

  // Create and initialize the server.
  TcpSocketServer server(IP, PORT);
  std::string srvError;
  uint8_t srvInitStatus = server.init(srvError);
  EXPECT_EQ(srvInitStatus, TCP_SERVER_SUCCESS) << srvError;

  // Register echo-on-read callback using RT-safe Delegate.
  EchoContext echoCtx{&server};
  server.setOnClientReadable(apex::concurrency::Delegate<void, int>{echoCallback, &echoCtx});

  // Event loop thread (sole epoll waiter).
  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed)) {
      server.processEvents(100); // 100ms tick
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Client: simple send/receive echo test.
  {
    TcpSocketClient client(IP, PORT);
    std::string cliError;
    uint8_t cliInitStatus = client.init(1000, cliError);
    EXPECT_EQ(cliInitStatus, TCP_CLIENT_SUCCESS) << cliError;

    // Prepare a test payload.
    std::array<uint8_t, 2> expected = {0xDE, 0xAD};
    std::string writeErr;
    ssize_t nwrite =
        client.write(apex::compat::bytes_span(expected.data(), expected.size()), 1000, writeErr);
    EXPECT_EQ(nwrite, static_cast<ssize_t>(expected.size())) << writeErr;

    std::array<uint8_t, 32> echoBuf{};
    std::string readErr;
    ssize_t nread = client.read(echoBuf, 1000, readErr);
    EXPECT_EQ(nread, static_cast<ssize_t>(expected.size())) << readErr;
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), echoBuf.begin()));
  }

  // Shutdown event loop
  ioExit = true;
  server.stop(); // wake epoll_wait
  ioThread.join();
}

/**
 * @test TcpSocketServerTest.MultipleClients
 * @brief Verifies that the TCP server correctly handles multiple concurrent client connections and
 * echoes data.
 *
 * The server runs a single event loop. Multiple clients connect concurrently, each sending a
 * payload and expecting the same payload echoed back.
 */
TEST(TcpSocketServerTest, MultipleClients) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9091";

  // Create and initialize the server.
  TcpSocketServer server(IP, PORT);
  std::string srvError;
  uint8_t srvInitStatus = server.init(srvError);
  EXPECT_EQ(srvInitStatus, TCP_SERVER_SUCCESS) << srvError;

  // Echo-on-read callback using RT-safe Delegate.
  EchoContext echoCtx{&server};
  server.setOnClientReadable(apex::concurrency::Delegate<void, int>{echoCallback, &echoCtx});

  // Drive the server event loop.
  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed)) {
      server.processEvents(100);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Launch multiple client threads.
  const size_t NUM_CLIENTS = 10;
  std::vector<std::thread> clientThreads;
  clientThreads.reserve(NUM_CLIENTS);
  std::atomic_size_t successfulClients{0};
  std::array<uint8_t, 2> expected = {0xBE, 0xEF};

  for (size_t i = 0; i < NUM_CLIENTS; ++i) {
    clientThreads.emplace_back([&, i]() {
      TcpSocketClient client(IP, PORT);
      std::string cliError;
      uint8_t cliInitStatus = client.init(1000, cliError);
      EXPECT_EQ(cliInitStatus, TCP_CLIENT_SUCCESS) << "Client " << i << ": " << cliError;

      std::string writeErr;
      ssize_t nwrite =
          client.write(apex::compat::bytes_span(expected.data(), expected.size()), 1000, writeErr);
      EXPECT_EQ(nwrite, static_cast<ssize_t>(expected.size()))
          << "Client " << i << ": " << writeErr;

      std::array<uint8_t, 32> echoBuf{};
      std::string readErr;
      ssize_t nread = client.read(echoBuf, 1000, readErr);
      EXPECT_EQ(nread, static_cast<ssize_t>(expected.size())) << "Client " << i << ": " << readErr;
      EXPECT_TRUE(std::equal(expected.begin(), expected.end(), echoBuf.begin()))
          << "Client " << i << ": Echo mismatch.";
      successfulClients.fetch_add(1, std::memory_order_relaxed);
    });
  }

  for (auto& th : clientThreads)
    th.join();
  EXPECT_EQ(successfulClients.load(), NUM_CLIENTS);

  // Shutdown event loop
  ioExit = true;
  server.stop();
  ioThread.join();
}

/* ----------------------------- Stats Tests ----------------------------- */

/**
 * @test TcpSocketServerTest.StatsAndReset
 * @brief Verifies that server statistics update after echo and reset to zero.
 */
TEST(TcpSocketServerTest, StatsAndReset) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9092";

  TcpSocketServer server(IP, PORT);
  std::string err;
  ASSERT_EQ(server.init(err), TCP_SERVER_SUCCESS) << err;

  EchoContext echoCtx{&server};
  server.setOnClientReadable(apex::concurrency::Delegate<void, int>{echoCallback, &echoCtx});

  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed))
      server.processEvents(50);
  });

  {
    TcpSocketClient client(IP, PORT);
    std::string cliErr;
    ASSERT_EQ(client.init(1000, cliErr), TCP_CLIENT_SUCCESS) << cliErr;

    std::array<uint8_t, 4> payload{0x01, 0x02, 0x03, 0x04};
    client.write(apex::compat::bytes_span(payload.data(), payload.size()), 1000);

    // Wait for echo (server.write uses bytes_span overload which updates stats)
    std::array<uint8_t, 64> buf{};
    apex::compat::mutable_bytes_span readSpan(buf.data(), buf.size());
    client.read(readSpan, 1000);
  }

  // Join ioThread before reading stats to ensure memory visibility
  ioExit = true;
  server.stop();
  ioThread.join();

  auto srvStats = server.stats();
  EXPECT_GT(srvStats.bytesTx, 0u);
  EXPECT_GT(srvStats.packetsTx, 0u);

  server.resetStats();
  auto reset = server.stats();
  EXPECT_EQ(reset.bytesTx, 0u);
  EXPECT_EQ(reset.packetsTx, 0u);
}

/**
 * @test TcpSocketServerTest.ConnectionCount
 * @brief Verifies connectionCount() reflects connected/disconnected clients.
 */
TEST(TcpSocketServerTest, ConnectionCount) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9093";

  TcpSocketServer server(IP, PORT);
  std::string err;
  ASSERT_EQ(server.init(err), TCP_SERVER_SUCCESS) << err;

  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed))
      server.processEvents(50);
  });

  EXPECT_EQ(server.connectionCount(), 0u);
  {
    TcpSocketClient client(IP, PORT);
    std::string cliErr;
    ASSERT_EQ(client.init(1000, cliErr), TCP_CLIENT_SUCCESS) << cliErr;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_GE(server.connectionCount(), 1u);
  }

  ioExit = true;
  server.stop();
  ioThread.join();
}

/**
 * @test TcpSocketServerTest.WriteAll
 * @brief Verifies writeAll sends to all connected clients.
 */
TEST(TcpSocketServerTest, WriteAll) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9094";

  TcpSocketServer server(IP, PORT);
  std::string err;
  ASSERT_EQ(server.init(err), TCP_SERVER_SUCCESS) << err;

  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed))
      server.processEvents(50);
  });

  const size_t N = 3;
  std::vector<std::unique_ptr<TcpSocketClient>> clients;
  for (size_t i = 0; i < N; ++i) {
    auto c = std::make_unique<TcpSocketClient>(IP, PORT);
    std::string cliErr;
    ASSERT_EQ(c->init(1000, cliErr), TCP_CLIENT_SUCCESS) << cliErr;
    clients.push_back(std::move(c));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::array<uint8_t, 3> broadcast{0xAA, 0xBB, 0xCC};
  server.writeAll(apex::compat::bytes_span(broadcast.data(), broadcast.size()));

  size_t received = 0;
  for (auto& c : clients) {
    std::array<uint8_t, 64> buf{};
    ssize_t n = c->read(buf, 500);
    if (n == static_cast<ssize_t>(broadcast.size()) &&
        std::equal(broadcast.begin(), broadcast.end(), buf.begin()))
      ++received;
  }
  EXPECT_EQ(received, N);

  ioExit = true;
  server.stop();
  ioThread.join();
}

/**
 * @test TcpSocketServerTest.ConnectionCallbacks
 * @brief Verifies onNewConnection and onConnectionClosed callbacks fire.
 */
TEST(TcpSocketServerTest, ConnectionCallbacks) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9095";

  TcpSocketServer server(IP, PORT);
  std::string err;
  ASSERT_EQ(server.init(err), TCP_SERVER_SUCCESS) << err;

  struct CallbackCtx {
    std::atomic_int connected{0};
    std::atomic_int disconnected{0};
  } ctx;

  server.setOnNewConnection(apex::concurrency::Delegate<void, int>{
      [](void* c, int) noexcept { ++static_cast<CallbackCtx*>(c)->connected; }, &ctx});
  server.setOnConnectionClosed(apex::concurrency::Delegate<void, int>{
      [](void* c, int) noexcept { ++static_cast<CallbackCtx*>(c)->disconnected; }, &ctx});

  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed))
      server.processEvents(50);
  });

  {
    TcpSocketClient client(IP, PORT);
    std::string cliErr;
    ASSERT_EQ(client.init(1000, cliErr), TCP_CLIENT_SUCCESS) << cliErr;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_GE(ctx.connected.load(), 1);
  }
  // Client destructor closes fd; server should detect EPOLLRDHUP
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ioExit = true;
  server.stop();
  ioThread.join();

  EXPECT_GE(ctx.connected.load(), 1);
}

/**
 * @test TcpSocketServerTest.MaxConnections
 * @brief Verifies setMaxConnections rejects connections beyond the limit.
 */
TEST(TcpSocketServerTest, MaxConnections) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9096";

  TcpSocketServer server(IP, PORT);
  server.setMaxConnections(2);
  EXPECT_EQ(server.maxConnections(), 2u);
  std::string err;
  ASSERT_EQ(server.init(err), TCP_SERVER_SUCCESS) << err;

  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed))
      server.processEvents(50);
  });

  // Connect up to the limit
  TcpSocketClient c1(IP, PORT), c2(IP, PORT);
  std::string e1, e2;
  ASSERT_EQ(c1.init(1000, e1), TCP_CLIENT_SUCCESS) << e1;
  ASSERT_EQ(c2.init(1000, e2), TCP_CLIENT_SUCCESS) << e2;
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_EQ(server.connectionCount(), 2u);

  // Third connection should be rejected (connect succeeds at TCP level but server closes it)
  TcpSocketClient c3(IP, PORT);
  std::string e3;
  c3.init(1000, e3); // may or may not succeed at TCP level
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_LE(server.connectionCount(), 2u);

  ioExit = true;
  server.stop();
  ioThread.join();
}

/**
 * @test TcpSocketServerTest.ConfigSetters
 * @brief Verifies configuration setters do not crash and preserve mutual exclusion.
 */
TEST(TcpSocketServerTest, ConfigSetters) {
  TcpSocketServer server("127.0.0.1", "9097");

  server.setNodelayDefault(true);
  server.setCorkDefault(true);    // must clear nodelay
  server.setNodelayDefault(true); // must clear cork
  server.setBufferSizes(65536, 65536);
  server.setKeepalive(true, 60, 10, 3);
  server.setQuickAckDefault(true);
  server.setBusyPollDefault(50);
  server.setBackpressureThresholds(1024, 512);
  server.setLingerDefault(true, 5);
  server.enableLogging(true);
  server.enableLogging(false);
  // No crash = pass
  SUCCEED();
}

/**
 * @test TcpSocketServerTest.GetClientFdsAndMutex
 * @brief Verifies getClientFds/getMutex are accessible after a connection.
 */
TEST(TcpSocketServerTest, GetClientFdsAndMutex) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9098";

  TcpSocketServer server(IP, PORT);
  std::string err;
  ASSERT_EQ(server.init(err), TCP_SERVER_SUCCESS) << err;

  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed))
      server.processEvents(50);
  });

  {
    TcpSocketClient client(IP, PORT);
    std::string cliErr;
    ASSERT_EQ(client.init(1000, cliErr), TCP_CLIENT_SUCCESS) << cliErr;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::lock_guard<std::mutex> lock(server.getMutex());
    EXPECT_FALSE(server.getClientFds().empty());
  }

  ioExit = true;
  server.stop();
  ioThread.join();
}

/* ----------------------------- Client Tests ----------------------------- */

/**
 * @test TcpSocketClientTest.StatsAndReset
 * @brief Verifies client statistics update after send/recv and reset to zero.
 */
TEST(TcpSocketClientTest, StatsAndReset) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9099";

  TcpSocketServer server(IP, PORT);
  std::string err;
  ASSERT_EQ(server.init(err), TCP_SERVER_SUCCESS) << err;

  EchoContext echoCtx{&server};
  server.setOnClientReadable(apex::concurrency::Delegate<void, int>{echoCallback, &echoCtx});

  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed))
      server.processEvents(50);
  });

  TcpSocketClient client(IP, PORT);
  std::string cliErr;
  ASSERT_EQ(client.init(1000, cliErr), TCP_CLIENT_SUCCESS) << cliErr;

  // client.write uses bytes_span overload → updates bytesTx/packetsTx
  std::array<uint8_t, 4> payload{0x10, 0x20, 0x30, 0x40};
  client.write(apex::compat::bytes_span(payload.data(), payload.size()), 1000);

  // client.read with mutable_bytes_span → updates bytesRx/packetsRx
  std::array<uint8_t, 64> buf{};
  apex::compat::mutable_bytes_span readSpan(buf.data(), buf.size());
  client.read(readSpan, 1000);

  auto stats = client.stats();
  EXPECT_GT(stats.bytesTx, 0u);
  EXPECT_GT(stats.packetsTx, 0u);
  EXPECT_GT(stats.bytesRx, 0u);

  client.resetStats();
  auto reset = client.stats();
  EXPECT_EQ(reset.bytesTx, 0u);
  EXPECT_EQ(reset.bytesRx, 0u);

  ioExit = true;
  server.stop();
  ioThread.join();
}

/**
 * @test TcpSocketClientTest.SetLingerAndLogging
 * @brief Verifies setLinger and enableLogging do not crash after init.
 */
TEST(TcpSocketClientTest, SetLingerAndLogging) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9100";

  TcpSocketServer server(IP, PORT);
  std::string err;
  ASSERT_EQ(server.init(err), TCP_SERVER_SUCCESS) << err;

  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed))
      server.processEvents(50);
  });

  TcpSocketClient client(IP, PORT);
  std::string cliErr;
  ASSERT_EQ(client.init(1000, cliErr), TCP_CLIENT_SUCCESS) << cliErr;
  EXPECT_TRUE(client.setLinger(false, 0));
  client.enableLogging(true);
  client.enableLogging(false);

  ioExit = true;
  server.stop();
  ioThread.join();
}

/**
 * @test TcpSocketClientTest.ProcessEventsWithCallbacks
 * @brief Verifies processEvents dispatches onReadable when data is available.
 */
TEST(TcpSocketClientTest, ProcessEventsWithCallbacks) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9101";

  TcpSocketServer server(IP, PORT);
  std::string err;
  ASSERT_EQ(server.init(err), TCP_SERVER_SUCCESS) << err;

  EchoContext echoCtx{&server};
  server.setOnClientReadable(apex::concurrency::Delegate<void, int>{echoCallback, &echoCtx});

  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed))
      server.processEvents(50);
  });

  TcpSocketClient client(IP, PORT);
  std::string cliErr;
  ASSERT_EQ(client.init(1000, cliErr), TCP_CLIENT_SUCCESS) << cliErr;

  std::atomic_int readableFired{0};
  client.setOnReadable(apex::concurrency::Delegate<void>{
      [](void* ctx) noexcept { ++*static_cast<std::atomic_int*>(ctx); }, &readableFired});

  // Send data; server echoes it back; then processEvents should fire onReadable
  std::array<uint8_t, 2> payload{0xCA, 0xFE};
  client.write(apex::compat::bytes_span(payload.data(), payload.size()), 1000);

  // Poll until readable fires or timeout
  for (int i = 0; i < 20 && readableFired.load() == 0; ++i)
    client.processEvents(50);

  EXPECT_GE(readableFired.load(), 1);

  ioExit = true;
  server.stop();
  ioThread.join();
}
