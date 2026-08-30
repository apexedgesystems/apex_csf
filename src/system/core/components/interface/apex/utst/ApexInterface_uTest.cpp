/**
 * @file ApexInterface_uTest.cpp
 * @brief End-to-end tests for ApexInterface with APROTO over SLIP/COBS framing.
 */

#include "src/system/core/components/interface/apex/inc/ApexInterface.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/IComponentResolver.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SystemComponentBase.hpp"
#include "src/system/core/components/interface/apex/inc/ApexInterfaceTunables.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoCodec.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoTypes.hpp"
#include "src/system/core/infrastructure/protocols/framing/cobs/inc/COBSFraming.hpp"
#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"
#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpSocketClient.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <cstdint>
#include <cstring>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace std::chrono_literals;
using system_core::interface::ApexInterface;
using system_core::interface::ApexInterfaceTunables;
using system_core::interface::FramingType;
using system_core::interface::InterfaceBase;
using system_core::interface::Status;

using apex::protocols::tcp::TCP_CLIENT_SUCCESS;
using apex::protocols::tcp::TcpSocketClient;

namespace aproto = system_core::protocols::aproto;

namespace {

/* ----------------------------- Helpers ----------------------------- */

/**
 * @brief SLIP-encode a payload.
 */
std::vector<std::uint8_t> slipEncode(apex::compat::bytes_span payload) {
  std::vector<std::uint8_t> out(payload.size() * 2 + 8);
  const auto R = apex::protocols::slip::encode(payload, out.data(), out.size());
  out.resize(static_cast<std::size_t>(R.bytesProduced));
  return out;
}

/**
 * @brief COBS-encode a payload (without trailing delimiter).
 */
std::vector<std::uint8_t> cobsEncode(apex::compat::bytes_span payload) {
  std::vector<std::uint8_t> out(payload.size() + payload.size() / 254 + 4);
  const auto R = apex::protocols::cobs::encode(payload, out.data(), out.size(), false);
  out.resize(static_cast<std::size_t>(R.bytesProduced));
  return out;
}

/**
 * @brief Build an APROTO packet with optional payload.
 */
std::vector<std::uint8_t> buildAprotoPacket(std::uint32_t fullUid, std::uint16_t opcode,
                                            std::uint16_t sequence,
                                            apex::compat::rospan<std::uint8_t> payload,
                                            bool ackRequested = true) {
  aproto::AprotoHeader hdr =
      aproto::buildHeader(fullUid, opcode, sequence, static_cast<std::uint16_t>(payload.size()),
                          false, // isResponse
                          ackRequested,
                          false); // includeCrc

  std::vector<std::uint8_t> packet(aproto::APROTO_HEADER_SIZE + payload.size());
  std::size_t bytesWritten = 0;
  [[maybe_unused]] aproto::Status enc_st = aproto::encodePacket(
      hdr, payload, apex::compat::mutable_bytes_span{packet.data(), packet.size()}, bytesWritten);
  packet.resize(bytesWritten);
  return packet;
}

/**
 * @brief Read until >=1 SLIP frame completes; return the latest completed frame.
 */
bool readAndDecodeSlipFrame(TcpSocketClient& client, std::vector<std::uint8_t>& decoded,
                            int perReadTimeoutMs, std::chrono::milliseconds overallTimeout = 2s) {
  std::vector<std::uint8_t> accum;
  std::array<std::uint8_t, 2048> buf{};

  apex::protocols::slip::DecodeConfig cfg{};
  apex::protocols::slip::DecodeState st{};

  std::vector<std::uint8_t> out(4096);
  std::vector<std::uint8_t> lastFrame;
  bool got = false;

  const auto DEADLINE = std::chrono::steady_clock::now() + overallTimeout;
  while (std::chrono::steady_clock::now() < DEADLINE) {
    std::string err;
    const ssize_t NREAD = client.read(buf, perReadTimeoutMs, err);
    if (NREAD > 0) {
      accum.insert(accum.end(), buf.begin(), buf.begin() + static_cast<std::size_t>(NREAD));

      for (;;) {
        if (out.size() < accum.size() + 8)
          out.resize(accum.size() + 8);

        const auto R = apex::protocols::slip::decodeChunk(
            st, cfg, apex::compat::bytes_span{accum.data(), accum.size()}, out.data(), out.size());

        if (R.status == apex::protocols::slip::Status::OUTPUT_FULL) {
          out.resize(out.size() * 2);
          continue;
        }

        if (R.frameCompleted && R.bytesProduced > 0) {
          lastFrame.assign(out.begin(), out.begin() + R.bytesProduced);
          got = true;

          if (R.bytesConsumed > 0 && R.bytesConsumed <= accum.size()) {
            accum.erase(accum.begin(),
                        accum.begin() + static_cast<std::ptrdiff_t>(R.bytesConsumed));
          } else {
            accum.clear();
          }
          st = apex::protocols::slip::DecodeState{};
          continue;
        }

        if (R.status == apex::protocols::slip::Status::NEED_MORE)
          break;

        if (R.bytesConsumed > 0 && R.bytesConsumed <= accum.size()) {
          accum.erase(accum.begin(), accum.begin() + static_cast<std::ptrdiff_t>(R.bytesConsumed));
        } else {
          accum.clear();
        }
        st = apex::protocols::slip::DecodeState{};
        if (accum.empty())
          break;
      }

      if (got) {
        decoded.swap(lastFrame);
        return true;
      }
    }
  }
  return false;
}

/**
 * @brief Read until >=1 COBS frame (terminated by 0x00) completes; return the latest.
 */
bool readAndDecodeCobsFrame(TcpSocketClient& client, std::vector<std::uint8_t>& decoded,
                            int perReadTimeoutMs, std::chrono::milliseconds overallTimeout = 2s) {
  std::vector<std::uint8_t> accum;
  std::array<std::uint8_t, 2048> buf{};

  apex::protocols::cobs::DecodeConfig cfg{};
  apex::protocols::cobs::DecodeState st{};

  std::vector<std::uint8_t> out(4096);
  std::vector<std::uint8_t> lastFrame;
  bool got = false;

  const auto DEADLINE = std::chrono::steady_clock::now() + overallTimeout;
  while (std::chrono::steady_clock::now() < DEADLINE) {
    std::string err;
    const ssize_t NREAD = client.read(buf, perReadTimeoutMs, err);
    if (NREAD > 0) {
      accum.insert(accum.end(), buf.begin(), buf.begin() + static_cast<std::size_t>(NREAD));

      for (;;) {
        auto it = std::find(accum.begin(), accum.end(), static_cast<std::uint8_t>(0x00));
        if (it == accum.end())
          break;

        const std::size_t IN_LEN = static_cast<std::size_t>(std::distance(accum.begin(), it)) + 1;
        if (out.size() < IN_LEN + 8)
          out.resize(IN_LEN + 8);

        const auto R = apex::protocols::cobs::decodeChunk(
            st, cfg, apex::compat::bytes_span{accum.data(), IN_LEN}, out.data(), out.size());

        if (R.bytesConsumed > 0 && R.bytesConsumed <= accum.size()) {
          accum.erase(accum.begin(), accum.begin() + static_cast<std::ptrdiff_t>(R.bytesConsumed));
        } else {
          accum.clear();
        }

        if (R.status == apex::protocols::cobs::Status::OUTPUT_FULL) {
          out.resize(out.size() * 2);
          continue;
        }

        if (R.status == apex::protocols::cobs::Status::OK && R.frameCompleted &&
            R.bytesProduced > 0) {
          lastFrame.assign(out.begin(), out.begin() + R.bytesProduced);
          got = true;
        }

        st = apex::protocols::cobs::DecodeState{};
      }

      if (got) {
        decoded.swap(lastFrame);
        return true;
      }
    }
  }
  return false;
}

/**
 * @brief Validate received APROTO packet is an ACK response.
 */
bool validateAckResponse(const std::vector<std::uint8_t>& packet, std::uint16_t expectedOpcode,
                         std::uint16_t expectedSeq, std::uint8_t expectedStatus = 0,
                         aproto::AckStage expectedStage = aproto::AckStage::RESULT,
                         std::vector<std::uint8_t>* extraOut = nullptr) {
  if (packet.size() < aproto::APROTO_HEADER_SIZE + aproto::APROTO_ACK_PAYLOAD_SIZE) {
    return false;
  }

  aproto::PacketView view{};
  if (aproto::createPacketView(apex::compat::rospan<std::uint8_t>{packet.data(), packet.size()},
                               view) != aproto::Status::SUCCESS) {
    return false;
  }

  // Should be a response.
  if (!view.isResponse()) {
    return false;
  }

  // Should be ACK or NAK opcode.
  const bool IS_ACK = (view.header.opcode == static_cast<std::uint16_t>(aproto::SystemOpcode::ACK));
  const bool IS_NAK = (view.header.opcode == static_cast<std::uint16_t>(aproto::SystemOpcode::NAK));
  if (!IS_ACK && !IS_NAK) {
    return false;
  }

  // Parse AckPayload.
  if (view.payload.size() < aproto::APROTO_ACK_PAYLOAD_SIZE) {
    return false;
  }

  aproto::AckPayload ack{};
  std::memcpy(&ack, view.payload.data(), sizeof(ack));

  if (ack.cmdOpcode != expectedOpcode) {
    return false;
  }
  if (ack.cmdSequence != expectedSeq) {
    return false;
  }
  if (ack.status != expectedStatus) {
    return false;
  }
  if (ack.stage != static_cast<std::uint8_t>(expectedStage)) {
    return false;
  }
  if (extraOut != nullptr) {
    extraOut->assign(view.payload.begin() + aproto::APROTO_ACK_PAYLOAD_SIZE, view.payload.end());
  }

  return true;
}

/**
 * @brief Component double whose handleCommand echoes the payload reversed.
 */
class QueuedEchoComponent final : public system_core::system_component::SystemComponentBase {
public:
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 200; }
  [[nodiscard]] const char* componentName() const noexcept override { return "QueuedEcho"; }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override { return 0; }

public:
  [[nodiscard]] std::uint8_t handleCommand(std::uint16_t /*opcode*/,
                                           apex::compat::rospan<std::uint8_t> payload,
                                           std::vector<std::uint8_t>& response) noexcept override {
    response.assign(payload.begin(), payload.end());
    std::reverse(response.begin(), response.end());
    calls.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }
  std::atomic<int> calls{0};
};

/**
 * @brief Resolver double mapping exactly one fullUid.
 */
class SingleResolver final : public system_core::system_component::IComponentResolver {
public:
  SingleResolver(std::uint32_t uid,
                 system_core::system_component::SystemComponentBase* comp) noexcept
      : uid_(uid), comp_(comp) {}
  [[nodiscard]] system_core::system_component::SystemComponentBase*
  getComponent(std::uint32_t fullUid) noexcept override {
    return fullUid == uid_ ? comp_ : nullptr;
  }
  [[nodiscard]] const system_core::system_component::SystemComponentBase*
  getComponent(std::uint32_t fullUid) const noexcept override {
    return fullUid == uid_ ? comp_ : nullptr;
  }

private:
  std::uint32_t uid_;
  system_core::system_component::SystemComponentBase* comp_;
};

} // namespace

/* ----------------------------- Tests ----------------------------- */

/** @test A queued command's outcome reaches ground: QUEUED then COMPLETION. */
TEST(ApexInterfaceTest, QueuedCommandEmitsCompletionFrame) {
  ApexInterface iface;

  ApexInterfaceTunables tun{};
  {
    std::string host = "127.0.0.1";
    std::snprintf(tun.host.data(), tun.host.size(), "%s", host.c_str());
  }
  tun.port = 6203;
  tun.framing = FramingType::SLIP;

  ASSERT_EQ(iface.configure(tun), Status::SUCCESS);

  // A routed component with an inbox: commands to it take the queued
  // path (interim QUEUED frame at accept, COMPLETION at drain).
  constexpr std::uint32_t TARGET_UID = 0x00C900;
  QueuedEchoComponent echo;
  SingleResolver resolver(TARGET_UID, &echo);
  iface.setComponentResolver(&resolver);
  ASSERT_NE(iface.allocateQueues(TARGET_UID), nullptr);
  iface.freezeQueues();

  std::atomic_bool run{true};
  std::atomic<std::size_t> drained{0};
  std::thread poller([&]() {
    while (run.load(std::memory_order_relaxed)) {
      iface.drainCompletionFrames(); // EXT_IO side of the completion hand-off.
      iface.pollSockets(25);
      iface.drainTelemetryOutboxes();
      drained.fetch_add(iface.drainCommandsToComponents(), std::memory_order_relaxed);
      std::this_thread::sleep_for(1ms);
    }
  });

  std::this_thread::sleep_for(100ms);

  TcpSocketClient cli("127.0.0.1", std::to_string(tun.port));
  std::string err;
  ASSERT_EQ(cli.init(1000, err), TCP_CLIENT_SUCCESS) << err;

  // Component-range opcode: below 0x0080 the router treats commands as
  // (unknown) system opcodes and NAKs without touching the resolver.
  const std::uint16_t OPCODE = 0x0642;
  const std::uint16_t SEQ = 77;
  const std::array<std::uint8_t, 4> PAYLOAD{0x01, 0x02, 0x03, 0x04};
  auto packet = buildAprotoPacket(
      TARGET_UID, OPCODE, SEQ, apex::compat::rospan<std::uint8_t>{PAYLOAD.data(), PAYLOAD.size()});

  auto enc = slipEncode(apex::compat::bytes_span{packet.data(), packet.size()});
  std::string writeErr;
  const ssize_t NWRITE =
      cli.write(apex::compat::bytes_span{enc.data(), enc.size()}, 1000, writeErr);
  ASSERT_EQ(NWRITE, static_cast<ssize_t>(enc.size())) << writeErr;

  // Frame 1: interim QUEUED acknowledgment (status 0, empty extra).
  std::vector<std::uint8_t> frame;
  ASSERT_TRUE(readAndDecodeSlipFrame(cli, frame, 500)) << "QUEUED frame timeout";
  EXPECT_TRUE(validateAckResponse(frame, OPCODE, SEQ, 0, aproto::AckStage::QUEUED))
      << "first frame must be the QUEUED interim ack";

  // Frame 2: deferred COMPLETION with the component's response bytes.
  std::vector<std::uint8_t> extra;
  const bool GOT_COMPLETION = readAndDecodeSlipFrame(cli, frame, 500);
  const int HANDLER_CALLS = echo.calls.load(std::memory_order_relaxed);
  if (!GOT_COMPLETION) {
    run = false;
    poller.join();
    FAIL() << "COMPLETION timeout (handler calls: " << HANDLER_CALLS
           << ", drained: " << drained.load() << ")";
  }
  EXPECT_TRUE(validateAckResponse(frame, OPCODE, SEQ, 0, aproto::AckStage::COMPLETION, &extra))
      << "second frame must be the COMPLETION result";
  const std::vector<std::uint8_t> REVERSED{0x04, 0x03, 0x02, 0x01};
  EXPECT_EQ(extra, REVERSED) << "completion extra must carry the handler's response";

  run = false;
  poller.join();
  EXPECT_EQ(iface.shutdown(), Status::SUCCESS);
}

/** @test APROTO NOOP command over SLIP returns ACK with correct correlation. */
TEST(ApexInterfaceTest, AprotoNoopOverSlip) {
  ApexInterface iface;

  ApexInterfaceTunables tun{};
  {
    std::string host = "127.0.0.1";
    std::snprintf(tun.host.data(), tun.host.size(), "%s", host.c_str());
  }
  tun.port = 6200;
  tun.framing = FramingType::SLIP;

  ASSERT_EQ(iface.configure(tun), Status::SUCCESS);

  std::atomic_bool run{true};
  std::thread poller([&]() {
    while (run.load(std::memory_order_relaxed)) {
      iface.pollSockets(25);
      // Process queued commands (tick-synchronized in real system).
      iface.drainTelemetryOutboxes();
      iface.drainCommandsToComponents();
      std::this_thread::sleep_for(1ms);
    }
  });

  std::this_thread::sleep_for(100ms);

  TcpSocketClient cli("127.0.0.1", std::to_string(tun.port));
  std::string err;
  ASSERT_EQ(cli.init(1000, err), TCP_CLIENT_SUCCESS) << err;

  // Build APROTO NOOP command (addressed to interface fullUid=0x0400).
  const std::uint16_t OPCODE = static_cast<std::uint16_t>(aproto::SystemOpcode::NOOP);
  const std::uint16_t SEQ = 42;
  const std::uint32_t IFACE_UID = iface.fullUid();
  auto packet = buildAprotoPacket(IFACE_UID, OPCODE, SEQ, {});

  // SLIP-encode and send.
  auto enc = slipEncode(apex::compat::bytes_span{packet.data(), packet.size()});
  std::string writeErr;
  const ssize_t NWRITE =
      cli.write(apex::compat::bytes_span{enc.data(), enc.size()}, 1000, writeErr);
  ASSERT_EQ(NWRITE, static_cast<ssize_t>(enc.size())) << writeErr;

  // Read and validate ACK response.
  std::vector<std::uint8_t> response;
  ASSERT_TRUE(readAndDecodeSlipFrame(cli, response, 500)) << "ACK timeout";
  EXPECT_TRUE(validateAckResponse(response, OPCODE, SEQ, 0)) << "Invalid ACK";

  // Stop and join the poller before shutdown, so shutdown() does not race the
  // in-flight pollSockets()/tryDequeueTxMessage() on the TX SPSC queue.
  run = false;
  poller.join();
  auto shutStatus = iface.shutdown();
  EXPECT_EQ(shutStatus, Status::SUCCESS);
}

/** @test APROTO PING command over SLIP echoes payload back. */
TEST(ApexInterfaceTest, AprotoPingOverSlip) {
  ApexInterface iface;

  ApexInterfaceTunables tun{};
  {
    std::string host = "127.0.0.1";
    std::snprintf(tun.host.data(), tun.host.size(), "%s", host.c_str());
  }
  tun.port = 6210;
  tun.framing = FramingType::SLIP;

  ASSERT_EQ(iface.configure(tun), Status::SUCCESS);

  std::atomic_bool run{true};
  std::thread poller([&]() {
    while (run.load(std::memory_order_relaxed)) {
      iface.pollSockets(25);
      // Process queued commands (tick-synchronized in real system).
      iface.drainTelemetryOutboxes();
      iface.drainCommandsToComponents();
      std::this_thread::sleep_for(1ms);
    }
  });

  std::this_thread::sleep_for(100ms);

  TcpSocketClient cli("127.0.0.1", std::to_string(tun.port));
  std::string err;
  ASSERT_EQ(cli.init(1000, err), TCP_CLIENT_SUCCESS) << err;

  // Build APROTO PING command with payload (addressed to interface fullUid=0x0400).
  const std::uint16_t OPCODE = static_cast<std::uint16_t>(aproto::SystemOpcode::PING);
  const std::uint16_t SEQ = 100;
  const std::uint32_t IFACE_UID = iface.fullUid();
  std::vector<std::uint8_t> pingPayload = {0xDE, 0xAD, 0xBE, 0xEF};
  auto packet =
      buildAprotoPacket(IFACE_UID, OPCODE, SEQ,
                        apex::compat::rospan<std::uint8_t>{pingPayload.data(), pingPayload.size()});

  // SLIP-encode and send.
  auto enc = slipEncode(apex::compat::bytes_span{packet.data(), packet.size()});
  std::string writeErr;
  const ssize_t NWRITE =
      cli.write(apex::compat::bytes_span{enc.data(), enc.size()}, 1000, writeErr);
  ASSERT_EQ(NWRITE, static_cast<ssize_t>(enc.size())) << writeErr;

  // Read response.
  std::vector<std::uint8_t> response;
  ASSERT_TRUE(readAndDecodeSlipFrame(cli, response, 500)) << "PING response timeout";

  // Validate it's an ACK.
  ASSERT_GE(response.size(), aproto::APROTO_HEADER_SIZE + aproto::APROTO_ACK_PAYLOAD_SIZE);
  aproto::PacketView view{};
  ASSERT_EQ(aproto::createPacketView(
                apex::compat::rospan<std::uint8_t>{response.data(), response.size()}, view),
            aproto::Status::SUCCESS);
  EXPECT_TRUE(view.isResponse());
  EXPECT_EQ(view.header.opcode, static_cast<std::uint16_t>(aproto::SystemOpcode::ACK));

  // Check that echoed payload follows AckPayload.
  ASSERT_GE(view.payload.size(), aproto::APROTO_ACK_PAYLOAD_SIZE + pingPayload.size());
  std::vector<std::uint8_t> echoedPayload(view.payload.data() + aproto::APROTO_ACK_PAYLOAD_SIZE,
                                          view.payload.data() + aproto::APROTO_ACK_PAYLOAD_SIZE +
                                              pingPayload.size());
  EXPECT_EQ(echoedPayload, pingPayload);

  // Stop and join the poller before shutdown, so shutdown() does not race the
  // in-flight pollSockets()/tryDequeueTxMessage() on the TX SPSC queue.
  run = false;
  poller.join();
  auto shutStatus = iface.shutdown();
  EXPECT_EQ(shutStatus, Status::SUCCESS);
}

/** @test APROTO NOOP command over COBS returns ACK.
 *  @note Disabled - COBS framing has issues with TX callback timing.
 */
TEST(ApexInterfaceTest, DISABLED_AprotoNoopOverCobs) {
  ApexInterface iface;

  ApexInterfaceTunables tun{};
  {
    std::string host = "127.0.0.1";
    std::snprintf(tun.host.data(), tun.host.size(), "%s", host.c_str());
  }
  tun.port = 6220;
  tun.framing = FramingType::COBS;

  ASSERT_EQ(iface.configure(tun), Status::SUCCESS);

  std::atomic_bool run{true};
  std::thread poller([&]() {
    while (run.load(std::memory_order_relaxed)) {
      iface.pollSockets(25);
      std::this_thread::sleep_for(1ms);
    }
  });

  std::this_thread::sleep_for(150ms);

  TcpSocketClient cli("127.0.0.1", std::to_string(tun.port));
  std::string err;
  ASSERT_EQ(cli.init(1000, err), TCP_CLIENT_SUCCESS) << err;

  // Build APROTO NOOP command.
  const std::uint16_t OPCODE = static_cast<std::uint16_t>(aproto::SystemOpcode::NOOP);
  const std::uint16_t SEQ = 77;
  auto packet = buildAprotoPacket(0, OPCODE, SEQ, {});

  // COBS-encode with delimiter.
  auto enc = cobsEncode(apex::compat::bytes_span{packet.data(), packet.size()});
  enc.push_back(0x00);

  std::string writeErr;
  const ssize_t NWRITE =
      cli.write(apex::compat::bytes_span{enc.data(), enc.size()}, 1000, writeErr);
  ASSERT_EQ(NWRITE, static_cast<ssize_t>(enc.size())) << writeErr;

  // Give server time to process and respond.
  std::this_thread::sleep_for(50ms);

  // Read and validate ACK response.
  std::vector<std::uint8_t> response;
  ASSERT_TRUE(readAndDecodeCobsFrame(cli, response, 500, 3s)) << "ACK timeout";
  EXPECT_TRUE(validateAckResponse(response, OPCODE, SEQ, 0)) << "Invalid ACK";

  // Stop and join the poller before shutdown, so shutdown() does not race the
  // in-flight pollSockets()/tryDequeueTxMessage() on the TX SPSC queue.
  run = false;
  poller.join();
  auto shutStatus = iface.shutdown();
  EXPECT_EQ(shutStatus, Status::SUCCESS);
}

/** @test Command to unknown component returns NAK with component-not-found status. */
TEST(ApexInterfaceTest, AprotoUnknownComponentNak) {
  ApexInterface iface;

  ApexInterfaceTunables tun{};
  {
    std::string host = "127.0.0.1";
    std::snprintf(tun.host.data(), tun.host.size(), "%s", host.c_str());
  }
  tun.port = 6230;
  tun.framing = FramingType::SLIP;

  ASSERT_EQ(iface.configure(tun), Status::SUCCESS);
  // Note: No registry set, so component routing will fail.

  std::atomic_bool run{true};
  std::thread poller([&]() {
    while (run.load(std::memory_order_relaxed)) {
      iface.pollSockets(25);
      std::this_thread::sleep_for(1ms);
    }
  });

  std::this_thread::sleep_for(100ms);

  TcpSocketClient cli("127.0.0.1", std::to_string(tun.port));
  std::string err;
  ASSERT_EQ(cli.init(1000, err), TCP_CLIENT_SUCCESS) << err;

  // Build APROTO command to a component (opcode > 0xFF routes to component).
  const std::uint32_t UNKNOWN_UID = 0x12345600;
  const std::uint16_t COMP_OPCODE = 0x0100; // Component-level opcode.
  const std::uint16_t SEQ = 99;
  auto packet = buildAprotoPacket(UNKNOWN_UID, COMP_OPCODE, SEQ, {});

  // SLIP-encode and send.
  auto enc = slipEncode(apex::compat::bytes_span{packet.data(), packet.size()});
  std::string writeErr;
  const ssize_t NWRITE =
      cli.write(apex::compat::bytes_span{enc.data(), enc.size()}, 1000, writeErr);
  ASSERT_EQ(NWRITE, static_cast<ssize_t>(enc.size())) << writeErr;

  // Read response - should be NAK with status 3 (no registry).
  std::vector<std::uint8_t> response;
  ASSERT_TRUE(readAndDecodeSlipFrame(cli, response, 200)) << "NAK timeout";
  EXPECT_TRUE(validateAckResponse(response, COMP_OPCODE, SEQ, 3)) << "Expected NAK with status 3";

  // Stop and join the poller before shutdown, so shutdown() does not race the
  // in-flight pollSockets()/tryDequeueTxMessage() on the TX SPSC queue.
  run = false;
  poller.join();
  auto shutStatus = iface.shutdown();
  EXPECT_EQ(shutStatus, Status::SUCCESS);
}
