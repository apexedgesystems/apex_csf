/**
 * @file UdpSocket_uTest.cpp
 * @brief Unit tests for the UDP socket server and UDP socket client.
 *
 * These tests verify that the UDP server correctly echoes datagrams sent from a UDP client.
 * We cover CONNECTED single echo, CONNECTED multiple clients, and UNCONNECTED mode.
 */

#include "src/utilities/compatibility/inc/compat_span.hpp" // apex::compat::bytes_span
#include "src/system/core/infrastructure/protocols/network/udp/inc/UdpSocketClient.hpp"
#include "src/system/core/infrastructure/protocols/network/udp/inc/UdpSocketServer.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <netdb.h>
#include <string>
#include <thread>
#include <vector>

using apex::protocols::udp::UDP_CLIENT_SUCCESS;
using apex::protocols::udp::UDP_SERVER_SUCCESS;
using apex::protocols::udp::UdpSocketClient;
using apex::protocols::udp::UdpSocketMode;
using apex::protocols::udp::UdpSocketServer;

/* ----------------------------- Delegate Helpers ----------------------------- */

namespace {

/**
 * @brief Echo callback context for Delegate-based callbacks.
 */
struct UdpEchoContext {
  UdpSocketServer* server;
};

/**
 * @brief RT-safe echo callback for UdpSocketServer.
 */
void udpEchoCallback(void* ctx) noexcept {
  auto* echoctx = static_cast<UdpEchoContext*>(ctx);
  std::array<uint8_t, 64> buffer{};
  for (;;) {
    auto info = echoctx->server->read(buffer, 0);
    if (info.nread <= 0)
      break;
    apex::compat::bytes_span out(reinterpret_cast<const uint8_t*>(buffer.data()),
                                 static_cast<size_t>(info.nread));
    std::string writeErr;
    (void)echoctx->server->write(info, out, 0, writeErr);
  }
}

} // namespace

/* ----------------------------- Test Helpers ----------------------------- */

// Simple polling helper for our nonblocking client I/O.
// Repeatedly calls `op()` until it returns != 0 or the deadline passes.
static inline ssize_t pollUntil(auto&& op, std::chrono::milliseconds maxWait) {
  const auto DEADLINE = std::chrono::steady_clock::now() + maxWait;
  ssize_t n = 0;
  do {
    n = op(); // >0 ok, <0 error, 0 would-block
    if (n != 0)
      return n;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (std::chrono::steady_clock::now() < DEADLINE);
  return n; // 0 (timeout) or <0 (error)
}

// Resolve a host:port into a PeerInfo (test-local helper; avoids adding API to client).
static bool resolvePeer(const std::string& host, const std::string& port,
                        UdpSocketClient::PeerInfo& out, std::string& err) {
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;

  struct addrinfo* addrs = nullptr;
  int rv = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &addrs);
  if (rv != 0) {
    err = std::string("getaddrinfo failed: ") + ::gai_strerror(rv);
    return false;
  }
  bool ok = false;
  for (auto* ai = addrs; ai != nullptr; ai = ai->ai_next) {
    if (ai->ai_addrlen <= sizeof(out.addr)) {
      std::memcpy(&out.addr, ai->ai_addr, ai->ai_addrlen);
      out.addrLen = static_cast<socklen_t>(ai->ai_addrlen);
      ok = true;
      break;
    }
  }
  ::freeaddrinfo(addrs);
  if (!ok)
    err = "no usable address from getaddrinfo";
  return ok;
}

/**
 * @test UdpSocketServerClientTest.SingleEcho
 * @brief Single CONNECTED client sends a datagram and receives an echoed response.
 */
TEST(UdpSocketServerClientTest, SingleEcho) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9092";

  // Server
  UdpSocketServer server(IP, PORT);
  std::string srvError;
  uint8_t srvInitStatus = server.init(srvError);
  ASSERT_EQ(srvInitStatus, UDP_SERVER_SUCCESS) << srvError;

  // Echo callback using RT-safe Delegate.
  UdpEchoContext echoCtx{&server};
  server.setOnDatagramReceived(apex::concurrency::Delegate<void>{udpEchoCallback, &echoCtx});

  // Event loop thread (sole epoll waiter).
  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed)) {
      server.processEvents(100);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Client (CONNECTED)
  UdpSocketClient client(IP, PORT);
  std::string cliError;
  uint8_t cliInitStatus = client.init(UdpSocketMode::CONNECTED, cliError);
  ASSERT_EQ(cliInitStatus, UDP_CLIENT_SUCCESS) << cliError;

  // Payload
  std::array<uint8_t, 2> expected = {0xAB, 0xCD};
  std::string writeErr;
  ssize_t nwrite = client.write(apex::compat::bytes_span(expected.data(), expected.size()),
                                /*timeout ignored*/ 0, writeErr);
  ASSERT_EQ(nwrite, static_cast<ssize_t>(expected.size())) << writeErr;

  // Receive echo with a small polling window.
  std::array<uint8_t, 64> echoBuf{};
  std::string readErr;
  ssize_t nread = pollUntil(
      [&]() {
        return client.read(apex::compat::bytes_span(echoBuf.data(), echoBuf.size()), readErr);
      },
      std::chrono::milliseconds(500));
  EXPECT_EQ(nread, static_cast<ssize_t>(expected.size())) << readErr;
  EXPECT_TRUE(std::equal(expected.begin(), expected.end(), echoBuf.begin()));

  // Teardown
  ioExit = true;
  server.stop(); // wake epoll_wait promptly
  ioThread.join();
}

/**
 * @test UdpSocketServerClientTest.MultipleClients
 * @brief Multiple CONNECTED clients concurrently send/echo datagrams.
 */
TEST(UdpSocketServerClientTest, MultipleClients) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9093";

  // Server
  UdpSocketServer server(IP, PORT);
  std::string srvError;
  uint8_t srvInitStatus = server.init(srvError);
  ASSERT_EQ(srvInitStatus, UDP_SERVER_SUCCESS) << srvError;

  // Echo callback using RT-safe Delegate.
  UdpEchoContext echoCtx{&server};
  server.setOnDatagramReceived(apex::concurrency::Delegate<void>{udpEchoCallback, &echoCtx});

  // Event loop
  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed)) {
      server.processEvents(100);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Clients
  const size_t NUM_CLIENTS = 10;
  std::vector<std::thread> clientThreads;
  clientThreads.reserve(NUM_CLIENTS);
  std::atomic_size_t successfulClients{0};
  std::array<uint8_t, 2> expected = {0x12, 0x34};

  for (size_t i = 0; i < NUM_CLIENTS; ++i) {
    clientThreads.emplace_back([&, i]() {
      UdpSocketClient client(IP, PORT);
      std::string cliError;
      uint8_t cliInitStatus = client.init(UdpSocketMode::CONNECTED, cliError);
      ASSERT_EQ(cliInitStatus, UDP_CLIENT_SUCCESS) << "Client " << i << ": " << cliError;

      std::string writeErr;
      ssize_t nwrite = client.write(apex::compat::bytes_span(expected.data(), expected.size()),
                                    /*timeout ignored*/ 0, writeErr);
      ASSERT_EQ(nwrite, static_cast<ssize_t>(expected.size()))
          << "Client " << i << ": " << writeErr;

      std::array<uint8_t, 64> echoBuf{};
      std::string readErr;
      ssize_t nread = pollUntil(
          [&]() {
            return client.read(apex::compat::bytes_span(echoBuf.data(), echoBuf.size()), readErr);
          },
          std::chrono::milliseconds(500));
      ASSERT_EQ(nread, static_cast<ssize_t>(expected.size())) << "Client " << i << ": " << readErr;
      ASSERT_TRUE(std::equal(expected.begin(), expected.end(), echoBuf.begin()))
          << "Client " << i << ": Echo mismatch.";

      successfulClients.fetch_add(1, std::memory_order_relaxed);
    });
  }

  for (auto& th : clientThreads)
    th.join();
  EXPECT_EQ(successfulClients.load(), NUM_CLIENTS);

  ioExit = true;
  server.stop();
  ioThread.join();
}

/**
 * @test UdpSocketServerClientTest.UnconnectedEcho
 * @brief Verifies UNCONNECTED mode path using readFrom()/writeTo() on the client.
 *
 * Server remains the same; client binds locally (UNCONNECTED), sends via writeTo(),
 * and reads via readFrom() capturing peer metadata.
 */
TEST(UdpSocketServerClientTest, UnconnectedEcho) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9094";

  // Server
  UdpSocketServer server(IP, PORT);
  std::string srvError;
  uint8_t srvInitStatus = server.init(srvError);
  ASSERT_EQ(srvInitStatus, UDP_SERVER_SUCCESS) << srvError;

  // Echo callback using RT-safe Delegate.
  UdpEchoContext echoCtx{&server};
  server.setOnDatagramReceived(apex::concurrency::Delegate<void>{udpEchoCallback, &echoCtx});

  // Event loop
  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed)) {
      server.processEvents(100);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Client (UNCONNECTED): bind to ephemeral local, then send to server using writeTo()
  UdpSocketClient client("", "0"); // INADDR_ANY:ephemeral
  std::string cliError;
  uint8_t cliInitStatus = client.init(UdpSocketMode::UNCONNECTED, cliError);
  ASSERT_EQ(cliInitStatus, UDP_CLIENT_SUCCESS) << cliError;

  // Resolve server into PeerInfo (test-local helper)
  UdpSocketClient::PeerInfo srvPeer{};
  std::string resErr;
  ASSERT_TRUE(resolvePeer(IP, PORT, srvPeer, resErr)) << resErr;

  // Send payload to server
  std::array<uint8_t, 3> expected = {0xDE, 0xAD, 0x01};
  std::string writeErr;
  ssize_t nwrite =
      client.writeTo(apex::compat::bytes_span(expected.data(), expected.size()), srvPeer,
                     /*timeout ignored*/ 0, writeErr);
  ASSERT_EQ(nwrite, static_cast<ssize_t>(expected.size())) << writeErr;

  // Read echoed payload (capture peer)
  std::array<uint8_t, 64> echoBuf{};
  UdpSocketClient::PeerInfo from{};
  std::string readErr;
  ssize_t nread = pollUntil(
      [&]() {
        return client.readFrom(apex::compat::bytes_span(echoBuf.data(), echoBuf.size()), from,
                               readErr);
      },
      std::chrono::milliseconds(500));
  EXPECT_EQ(nread, static_cast<ssize_t>(expected.size())) << readErr;
  EXPECT_TRUE(std::equal(expected.begin(), expected.end(), echoBuf.begin()));

  ioExit = true;
  server.stop();
  ioThread.join();
}

/* ========================= Optional (disabled) coverage =========================
 * Enable these when your CI/network environment allows broadcast or multicast.
 */

/** @test Broadcast echo round-trip (disabled: requires SO_BROADCAST in test environment). */
TEST(UdpSocketServerClientTest, DISABLED_BroadcastEcho) {
  GTEST_SKIP() << "Enable when broadcast is permitted in the test environment.";
  // Example outline:
  // - server bind on 0.0.0.0:PORT, echo callback as above
  // - client.setBroadcast(true); client.init(UNCONNECTED);
  // - resolve 255.255.255.255:PORT to PeerInfo; writeTo(); readFrom();
}

/** @test Multicast echo round-trip (disabled: requires multicast routing in test environment). */
TEST(UdpSocketServerClientTest, DISABLED_MulticastEcho) {
  GTEST_SKIP() << "Enable when multicast join/loopback is configured in the test environment.";
  // Example outline:
  // - server: setMulticastInterface(), joinMulticast(group), echo callback
  // - client: setMulticastInterface(), optional join for loopback, send to group; read echo
}

/* ----------------------------- Additional Tests ----------------------------- */

namespace {

/**
 * @brief Echo callback using the bytes_span overload of server.read().
 */
struct UdpSpanEchoCtx {
  UdpSocketServer* server;
};

void udpSpanEchoCallback(void* ctx) noexcept {
  auto* ec = static_cast<UdpSpanEchoCtx*>(ctx);
  std::array<uint8_t, 64> buf{};
  UdpSocketServer::RecvInfo info{};
  for (;;) {
    ssize_t n = ec->server->read(apex::compat::bytes_span(buf.data(), buf.size()), info);
    if (n <= 0)
      break;
    apex::compat::bytes_span out(buf.data(), static_cast<size_t>(n));
    (void)ec->server->write(info, out, 0);
  }
}

/**
 * @brief Client-side readback context for processEvents callback testing.
 */
struct ClientReadbackCtx {
  UdpSocketClient* client;
  std::atomic_int fired{0};
  std::array<uint8_t, 64> buf{};
};

void clientReadableCallback(void* ctx) noexcept {
  auto* c = static_cast<ClientReadbackCtx*>(ctx);
  ssize_t n = c->client->read(apex::compat::bytes_span(c->buf.data(), c->buf.size()));
  if (n > 0)
    c->fired.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

/**
 * @test UdpSocketServerClientTest.ServerBytesSpanAPI
 * @brief Exercises the bytes_span overloads of server.read() and server.write().
 */
TEST(UdpSocketServerClientTest, ServerBytesSpanAPI) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9095";

  UdpSocketServer server(IP, PORT);
  std::string srvError;
  ASSERT_EQ(server.init(srvError), UDP_SERVER_SUCCESS) << srvError;

  UdpSpanEchoCtx echoCtx{&server};
  server.setOnDatagramReceived(apex::concurrency::Delegate<void>{udpSpanEchoCallback, &echoCtx});

  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed)) {
      server.processEvents(100);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  UdpSocketClient client(IP, PORT);
  std::string cliError;
  ASSERT_EQ(client.init(UdpSocketMode::CONNECTED, cliError), UDP_CLIENT_SUCCESS) << cliError;

  std::array<uint8_t, 4> payload = {0x11, 0x22, 0x33, 0x44};
  std::string writeErr;
  ssize_t nw = client.write(apex::compat::bytes_span(payload.data(), payload.size()), 0, writeErr);
  ASSERT_EQ(nw, static_cast<ssize_t>(payload.size())) << writeErr;

  std::array<uint8_t, 64> echoBuf{};
  std::string readErr;
  ssize_t nr = pollUntil(
      [&]() {
        return client.read(apex::compat::bytes_span(echoBuf.data(), echoBuf.size()), readErr);
      },
      std::chrono::milliseconds(500));
  EXPECT_EQ(nr, static_cast<ssize_t>(payload.size())) << readErr;
  EXPECT_TRUE(std::equal(payload.begin(), payload.end(), echoBuf.begin()));

  ioExit = true;
  server.stop();
  ioThread.join();
}

/**
 * @test UdpSocketServerClientTest.StatsAndReset
 * @brief Verifies stats accumulate via the bytes_span I/O path and reset clears them.
 */
TEST(UdpSocketServerClientTest, StatsAndReset) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9100";

  UdpSocketServer server(IP, PORT);
  std::string srvError;
  ASSERT_EQ(server.init(srvError), UDP_SERVER_SUCCESS) << srvError;

  UdpSpanEchoCtx echoCtx{&server};
  server.setOnDatagramReceived(apex::concurrency::Delegate<void>{udpSpanEchoCallback, &echoCtx});

  std::atomic_bool ioExit{false};
  std::thread ioThread([&]() {
    while (!ioExit.load(std::memory_order_relaxed)) {
      server.processEvents(100);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  UdpSocketClient client(IP, PORT);
  std::string cliError;
  ASSERT_EQ(client.init(UdpSocketMode::CONNECTED, cliError), UDP_CLIENT_SUCCESS) << cliError;

  std::array<uint8_t, 8> payload = {1, 2, 3, 4, 5, 6, 7, 8};
  std::string writeErr;
  client.write(apex::compat::bytes_span(payload.data(), payload.size()), 0, writeErr);

  std::array<uint8_t, 64> buf{};
  std::string readErr;
  pollUntil(
      [&]() { return client.read(apex::compat::bytes_span(buf.data(), buf.size()), readErr); },
      std::chrono::milliseconds(500));

  ioExit = true;
  server.stop();
  ioThread.join(); // Join before reading stats for memory visibility

  auto srvStats = server.stats();
  EXPECT_GT(srvStats.bytesTx, 0u);
  EXPECT_GT(srvStats.datagramsTx, 0u);

  server.resetStats();
  auto srvCleared = server.stats();
  EXPECT_EQ(srvCleared.bytesTx, 0u);
  EXPECT_EQ(srvCleared.datagramsTx, 0u);

  auto cliStats = client.stats();
  EXPECT_GT(cliStats.bytesTx, 0u);

  client.resetStats();
  EXPECT_EQ(client.stats().bytesTx, 0u);
}

/**
 * @test UdpSocketServerClientTest.ServerReadWriteBatch
 * @brief Exercises readBatch() and writeBatch() via recvmmsg/sendmmsg.
 */
TEST(UdpSocketServerClientTest, ServerReadWriteBatch) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9096";

  UdpSocketServer server(IP, PORT);
  std::string srvError;
  ASSERT_EQ(server.init(srvError), UDP_SERVER_SUCCESS) << srvError;

  UdpSocketClient client(IP, PORT);
  std::string cliError;
  ASSERT_EQ(client.init(UdpSocketMode::CONNECTED, cliError), UDP_CLIENT_SUCCESS) << cliError;

  const size_t COUNT = 3;
  std::array<uint8_t, 4> pkt = {0xAA, 0xBB, 0xCC, 0xDD};
  for (size_t i = 0; i < COUNT; ++i) {
    std::string writeErr;
    client.write(apex::compat::bytes_span(pkt.data(), pkt.size()), 0, writeErr);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::array<std::array<uint8_t, 64>, COUNT> bufs{};
  std::array<apex::compat::bytes_span, COUNT> spans = {
      apex::compat::bytes_span(bufs[0].data(), 64), apex::compat::bytes_span(bufs[1].data(), 64),
      apex::compat::bytes_span(bufs[2].data(), 64)};
  std::array<UdpSocketServer::RecvInfo, COUNT> infos{};

  int nrecv = server.readBatch(spans.data(), infos.data(), COUNT);
  EXPECT_GT(nrecv, 0);

  // Build echo spans using actual received byte counts
  std::array<apex::compat::bytes_span, COUNT> echoSpans = {
      apex::compat::bytes_span(bufs[0].data(),
                               static_cast<size_t>(infos[0].nread > 0 ? infos[0].nread : 0)),
      apex::compat::bytes_span(bufs[1].data(),
                               static_cast<size_t>(infos[1].nread > 0 ? infos[1].nread : 0)),
      apex::compat::bytes_span(bufs[2].data(),
                               static_cast<size_t>(infos[2].nread > 0 ? infos[2].nread : 0))};

  int nsent = server.writeBatch(infos.data(), echoSpans.data(), static_cast<size_t>(nrecv));
  EXPECT_GE(nsent, 0);

  // Drain echoes
  std::array<uint8_t, 64> recvBuf{};
  for (int i = 0; i < nsent; ++i) {
    pollUntil(
        [&]() { return client.read(apex::compat::bytes_span(recvBuf.data(), recvBuf.size())); },
        std::chrono::milliseconds(200));
  }
}

/**
 * @test UdpSocketServerClientTest.ClientProcessEventsWithCallbacks
 * @brief Exercises client processEvents() event loop with setOnReadable callback.
 */
TEST(UdpSocketServerClientTest, ClientProcessEventsWithCallbacks) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9097";

  UdpSocketServer server(IP, PORT);
  std::string srvError;
  ASSERT_EQ(server.init(srvError), UDP_SERVER_SUCCESS) << srvError;

  UdpEchoContext echoCtx{&server};
  server.setOnDatagramReceived(apex::concurrency::Delegate<void>{udpEchoCallback, &echoCtx});

  std::atomic_bool srvExit{false};
  std::thread srvThread([&]() {
    while (!srvExit.load(std::memory_order_relaxed)) {
      server.processEvents(50);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  UdpSocketClient client(IP, PORT);
  std::string cliError;
  ASSERT_EQ(client.init(UdpSocketMode::CONNECTED, cliError), UDP_CLIENT_SUCCESS) << cliError;

  ClientReadbackCtx cbCtx{&client};
  client.setOnReadable(apex::concurrency::Delegate<void>{clientReadableCallback, &cbCtx});

  std::atomic_bool cliExit{false};
  std::thread cliThread([&]() {
    while (!cliExit.load(std::memory_order_relaxed)) {
      client.processEvents(50);
    }
  });

  std::array<uint8_t, 2> pkt = {0x77, 0x88};
  std::string writeErr;
  client.write(apex::compat::bytes_span(pkt.data(), pkt.size()), 0, writeErr);

  const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  while (cbCtx.fired.load() == 0 && std::chrono::steady_clock::now() < DEADLINE)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

  EXPECT_GT(cbCtx.fired.load(), 0);

  cliExit = true;
  client.stop();
  cliThread.join();

  srvExit = true;
  server.stop();
  srvThread.join();
}

/**
 * @test UdpSocketServerClientTest.ClientStopWakesLoop
 * @brief Verifies that client stop() breaks out of processEvents() promptly.
 */
TEST(UdpSocketServerClientTest, ClientStopWakesLoop) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9098";

  UdpSocketClient client(IP, PORT);
  std::string cliError;
  ASSERT_EQ(client.init(UdpSocketMode::CONNECTED, cliError), UDP_CLIENT_SUCCESS) << cliError;

  std::atomic_bool exited{false};
  std::thread cliThread([&]() {
    client.processEvents(5000);
    exited.store(true, std::memory_order_relaxed);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  client.stop();
  cliThread.join();

  EXPECT_TRUE(exited.load());
}

/**
 * @test UdpSocketServerClientTest.ClientWaitReadable
 * @brief Verifies waitReadable(0) polls without blocking; true after data arrives.
 */
TEST(UdpSocketServerClientTest, ClientWaitReadable) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9099";

  UdpSocketServer server(IP, PORT);
  std::string srvError;
  ASSERT_EQ(server.init(srvError), UDP_SERVER_SUCCESS) << srvError;

  UdpEchoContext echoCtx{&server};
  server.setOnDatagramReceived(apex::concurrency::Delegate<void>{udpEchoCallback, &echoCtx});

  std::atomic_bool srvExit{false};
  std::thread srvThread([&]() {
    while (!srvExit.load(std::memory_order_relaxed)) {
      server.processEvents(50);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  UdpSocketClient client(IP, PORT);
  std::string cliError;
  ASSERT_EQ(client.init(UdpSocketMode::CONNECTED, cliError), UDP_CLIENT_SUCCESS) << cliError;

  EXPECT_FALSE(client.waitReadable(0));

  std::array<uint8_t, 2> pkt = {0x55, 0x66};
  std::string writeErr;
  client.write(apex::compat::bytes_span(pkt.data(), pkt.size()), 0, writeErr);

  bool readable = false;
  const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  while (!readable && std::chrono::steady_clock::now() < DEADLINE)
    readable = client.waitReadable(10);

  EXPECT_TRUE(readable);

  srvExit = true;
  server.stop();
  srvThread.join();
}

/**
 * @test UdpSocketServerClientTest.ClientPeerManagement
 * @brief Exercises resolvePeer(), connectPeer(), and disconnectPeer().
 */
TEST(UdpSocketServerClientTest, ClientPeerManagement) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9101";

  UdpSocketClient client("", "0");
  std::string cliError;
  ASSERT_EQ(client.init(UdpSocketMode::UNCONNECTED, cliError), UDP_CLIENT_SUCCESS) << cliError;

  UdpSocketClient::PeerInfo peer{};
  std::string resolveErr;
  EXPECT_TRUE(client.resolvePeer(IP, PORT, peer, resolveErr)) << resolveErr;
  EXPECT_GT(peer.addrLen, 0u);

  std::string connErr;
  EXPECT_TRUE(client.connectPeer(peer, connErr)) << connErr;

  std::string discErr;
  EXPECT_TRUE(client.disconnectPeer(discErr)) << discErr;
}

/**
 * @test UdpSocketServerClientTest.ConfigSetters
 * @brief Exercises configuration setters, logging toggles, and setOnError.
 */
TEST(UdpSocketServerClientTest, ConfigSetters) {
  const std::string IP = "127.0.0.1";
  const std::string PORT = "9103";

  UdpSocketServer server(IP, PORT);
  server.setReusePort(true);
  server.setBroadcast(false);
  server.setTosDscp(0);
  server.setPktInfoV4(false);
  server.setPktInfoV6(false);

  std::string srvError;
  ASSERT_EQ(server.init(srvError), UDP_SERVER_SUCCESS) << srvError;

  server.enableLogging(true);
  server.setLogLevel(1);
  server.setLogLevel(0);
  server.enableLogging(false);

  bool errorCalled = false;
  server.setOnError([&](const std::string&) { errorCalled = true; });
  EXPECT_FALSE(errorCalled);

  UdpSocketClient client(IP, PORT);
  client.setReusePort(false);
  client.setBroadcast(false);
  client.setTosDscp(-1);
  client.setPktInfoV4(false);
  client.setPktInfoV6(false);

  std::string cliError;
  ASSERT_EQ(client.init(UdpSocketMode::CONNECTED, cliError), UDP_CLIENT_SUCCESS) << cliError;

  client.enableLogging(false);
  client.setOnReadable(apex::concurrency::Delegate<void>{});
  client.setOnWritable(apex::concurrency::Delegate<void>{});
}

/**
 * @test UdpSocketServerClientTest.MulticastV4JoinLeave
 * @brief Exercises joinMulticastV4(), setMulticastLoopV4(), setMulticastTtlV4(),
 *        and leaveMulticastV4() on the loopback interface.
 */
TEST(UdpSocketServerClientTest, MulticastV4JoinLeave) {
  UdpSocketServer server("", "9104");
  server.setReusePort(true);
  std::string srvError;
  ASSERT_EQ(server.init(srvError), UDP_SERVER_SUCCESS) << srvError;

  // 239.1.2.3 is in the administratively scoped range (safe for local testing)
  const uint32_t GROUP_BE = ::inet_addr("239.1.2.3");
  EXPECT_TRUE(server.joinMulticastV4(GROUP_BE, 0));
  EXPECT_TRUE(server.setMulticastLoopV4(true));
  EXPECT_TRUE(server.setMulticastTtlV4(1));
  EXPECT_TRUE(server.leaveMulticastV4(GROUP_BE, 0));

  // Client-side multicast
  UdpSocketClient client("", "0");
  std::string cliError;
  ASSERT_EQ(client.init(UdpSocketMode::UNCONNECTED, cliError), UDP_CLIENT_SUCCESS) << cliError;

  EXPECT_TRUE(client.joinMulticastV4(GROUP_BE, 0));
  EXPECT_TRUE(client.setMulticastLoopV4(true));
  EXPECT_TRUE(client.setMulticastTtlV4(1));
  EXPECT_TRUE(client.leaveMulticastV4(GROUP_BE, 0));
}
