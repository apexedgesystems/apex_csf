/**
 * @file ByteTrace_uTest.cpp
 * @brief Unit tests for ByteTrace mixin and format helpers.
 *
 * Coverage:
 *  - TraceDirection enum and toString
 *  - ByteTrace lifecycle (attach, detach, enable, disable)
 *  - Trace invocation (enabled/disabled, attached/detached)
 *  - Callback data correctness (direction, bytes, length, userData)
 *  - formatBytesHex with various sizes and truncation
 *  - formatTraceMessage output format
 *  - Null pointer and zero-size edge cases
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstring>

using apex::protocols::ByteTrace;
using apex::protocols::formatBytesHex;
using apex::protocols::formatTraceMessage;
using apex::protocols::toString;
using apex::protocols::TraceCallback;
using apex::protocols::TraceDirection;

/* ----------------------------- Test Helpers ----------------------------- */

namespace {

/** @brief Concrete subclass exposing protected invokeTrace for testing. */
class TestableTrace : public ByteTrace {
public:
  using ByteTrace::invokeTrace;
};

/** @brief Captures trace callback invocations for verification. */
struct TraceCapture {
  TraceDirection lastDir{};
  std::vector<std::uint8_t> lastData;
  std::size_t lastLen{0};
  int callCount{0};
};

/** @brief Trace callback that records invocation details. */
void captureCallback(TraceDirection dir, const std::uint8_t* data, std::size_t len,
                     void* userData) noexcept {
  auto* cap = static_cast<TraceCapture*>(userData);
  cap->lastDir = dir;
  cap->lastData.assign(data, data + len);
  cap->lastLen = len;
  ++cap->callCount;
}

} // namespace

/* ----------------------------- Enum Tests ----------------------------- */

/** @test Verifies TraceDirection::RX converts to "RX". */
TEST(ByteTraceTest, ToStringRX) { EXPECT_STREQ(toString(TraceDirection::RX), "RX"); }

/** @test Verifies TraceDirection::TX converts to "TX". */
TEST(ByteTraceTest, ToStringTX) { EXPECT_STREQ(toString(TraceDirection::TX), "TX"); }

/** @test Verifies TraceDirection enum values. */
TEST(ByteTraceTest, TraceDirectionValues) {
  EXPECT_EQ(static_cast<std::uint8_t>(TraceDirection::RX), 0);
  EXPECT_EQ(static_cast<std::uint8_t>(TraceDirection::TX), 1);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verifies default state: not attached, not enabled. */
TEST(ByteTraceTest, DefaultState) {
  TestableTrace trace;
  EXPECT_FALSE(trace.traceAttached());
  EXPECT_FALSE(trace.traceEnabled());
}

/* ----------------------------- Lifecycle Tests ----------------------------- */

/** @test Verifies attach sets attached state. */
TEST(ByteTraceTest, AttachSetsAttached) {
  TestableTrace trace;
  TraceCapture cap;
  trace.attachTrace(captureCallback, &cap);
  EXPECT_TRUE(trace.traceAttached());
}

/** @test Verifies detach clears attached state and disables tracing. */
TEST(ByteTraceTest, DetachClearsState) {
  TestableTrace trace;
  TraceCapture cap;
  trace.attachTrace(captureCallback, &cap);
  trace.setTraceEnabled(true);
  EXPECT_TRUE(trace.traceAttached());
  EXPECT_TRUE(trace.traceEnabled());

  trace.detachTrace();
  EXPECT_FALSE(trace.traceAttached());
  EXPECT_FALSE(trace.traceEnabled());
}

/** @test Verifies enable/disable toggle. */
TEST(ByteTraceTest, EnableDisableToggle) {
  TestableTrace trace;
  EXPECT_FALSE(trace.traceEnabled());

  trace.setTraceEnabled(true);
  EXPECT_TRUE(trace.traceEnabled());

  trace.setTraceEnabled(false);
  EXPECT_FALSE(trace.traceEnabled());
}

/** @test Verifies attach replaces previous callback. */
TEST(ByteTraceTest, AttachReplacesCallback) {
  TestableTrace trace;
  TraceCapture cap1;
  TraceCapture cap2;

  trace.attachTrace(captureCallback, &cap1);
  trace.setTraceEnabled(true);

  const std::uint8_t DATA[] = {0xAA};
  trace.invokeTrace(TraceDirection::TX, DATA, 1);
  EXPECT_EQ(cap1.callCount, 1);
  EXPECT_EQ(cap2.callCount, 0);

  trace.attachTrace(captureCallback, &cap2);
  trace.invokeTrace(TraceDirection::TX, DATA, 1);
  EXPECT_EQ(cap1.callCount, 1);
  EXPECT_EQ(cap2.callCount, 1);
}

/* ----------------------------- Invoke Tests ----------------------------- */

/** @test Verifies invokeTrace delivers correct data when enabled. */
TEST(ByteTraceTest, InvokeDeliversData) {
  TestableTrace trace;
  TraceCapture cap;
  trace.attachTrace(captureCallback, &cap);
  trace.setTraceEnabled(true);

  const std::uint8_t DATA[] = {0xDE, 0xAD, 0xBE, 0xEF};
  trace.invokeTrace(TraceDirection::RX, DATA, 4);

  EXPECT_EQ(cap.callCount, 1);
  EXPECT_EQ(cap.lastDir, TraceDirection::RX);
  EXPECT_EQ(cap.lastLen, 4u);
  ASSERT_EQ(cap.lastData.size(), 4u);
  EXPECT_EQ(cap.lastData[0], 0xDE);
  EXPECT_EQ(cap.lastData[1], 0xAD);
  EXPECT_EQ(cap.lastData[2], 0xBE);
  EXPECT_EQ(cap.lastData[3], 0xEF);
}

/** @test Verifies invokeTrace respects TX direction. */
TEST(ByteTraceTest, InvokeTxDirection) {
  TestableTrace trace;
  TraceCapture cap;
  trace.attachTrace(captureCallback, &cap);
  trace.setTraceEnabled(true);

  const std::uint8_t DATA[] = {0x01};
  trace.invokeTrace(TraceDirection::TX, DATA, 1);
  EXPECT_EQ(cap.lastDir, TraceDirection::TX);
}

/** @test Verifies invokeTrace does nothing when not enabled. */
TEST(ByteTraceTest, InvokeSkipsWhenDisabled) {
  TestableTrace trace;
  TraceCapture cap;
  trace.attachTrace(captureCallback, &cap);
  // Not enabled

  const std::uint8_t DATA[] = {0xFF};
  trace.invokeTrace(TraceDirection::RX, DATA, 1);
  EXPECT_EQ(cap.callCount, 0);
}

/** @test Verifies invokeTrace does nothing when not attached. */
TEST(ByteTraceTest, InvokeSkipsWhenNotAttached) {
  TestableTrace trace;
  trace.setTraceEnabled(true);

  // No callback attached - should not crash
  const std::uint8_t DATA[] = {0xFF};
  trace.invokeTrace(TraceDirection::RX, DATA, 1);
}

/** @test Verifies invokeTrace does nothing after detach. */
TEST(ByteTraceTest, InvokeSkipsAfterDetach) {
  TestableTrace trace;
  TraceCapture cap;
  trace.attachTrace(captureCallback, &cap);
  trace.setTraceEnabled(true);

  const std::uint8_t DATA[] = {0xAA};
  trace.invokeTrace(TraceDirection::TX, DATA, 1);
  EXPECT_EQ(cap.callCount, 1);

  trace.detachTrace();
  trace.invokeTrace(TraceDirection::TX, DATA, 1);
  EXPECT_EQ(cap.callCount, 1); // No additional call
}

/** @test Verifies attach with nullptr userData works. */
TEST(ByteTraceTest, AttachNullUserData) {
  static int callCount = 0;
  callCount = 0;

  auto nullCb = [](TraceDirection, const std::uint8_t*, std::size_t, void* ud) noexcept {
    EXPECT_EQ(ud, nullptr);
    ++callCount;
  };

  TestableTrace trace;
  trace.attachTrace(nullCb);
  trace.setTraceEnabled(true);

  const std::uint8_t DATA[] = {0x42};
  trace.invokeTrace(TraceDirection::RX, DATA, 1);
  EXPECT_EQ(callCount, 1);
}

/* ----------------------------- formatBytesHex Tests ----------------------------- */

/** @test Verifies formatBytesHex with typical input. */
TEST(ByteTraceTest, FormatBytesHexTypical) {
  const std::uint8_t DATA[] = {0xDE, 0xAD, 0xBE, 0xEF};
  char buf[64];
  std::size_t len = formatBytesHex(DATA, 4, buf, sizeof(buf));

  EXPECT_STREQ(buf, "DE AD BE EF");
  EXPECT_EQ(len, 11u);
}

/** @test Verifies formatBytesHex with single byte. */
TEST(ByteTraceTest, FormatBytesHexSingleByte) {
  const std::uint8_t DATA[] = {0x0A};
  char buf[16];
  std::size_t len = formatBytesHex(DATA, 1, buf, sizeof(buf));

  EXPECT_STREQ(buf, "0A");
  EXPECT_EQ(len, 2u);
}

/** @test Verifies formatBytesHex with zero-value bytes. */
TEST(ByteTraceTest, FormatBytesHexZeroBytes) {
  const std::uint8_t DATA[] = {0x00, 0x00, 0x00};
  char buf[32];
  std::size_t len = formatBytesHex(DATA, 3, buf, sizeof(buf));

  EXPECT_STREQ(buf, "00 00 00");
  EXPECT_EQ(len, 8u);
}

/** @test Verifies formatBytesHex with max-value bytes. */
TEST(ByteTraceTest, FormatBytesHexMaxBytes) {
  const std::uint8_t DATA[] = {0xFF, 0xFF};
  char buf[16];
  std::size_t len = formatBytesHex(DATA, 2, buf, sizeof(buf));

  EXPECT_STREQ(buf, "FF FF");
  EXPECT_EQ(len, 5u);
}

/** @test Verifies formatBytesHex truncation with ellipsis. */
TEST(ByteTraceTest, FormatBytesHexTruncation) {
  // 8 bytes but maxBytes=4
  const std::uint8_t DATA[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  char buf[64];
  std::size_t len = formatBytesHex(DATA, 8, buf, sizeof(buf), 4);

  EXPECT_STREQ(buf, "01 02 03 04 ...");
  EXPECT_EQ(len, 15u);
}

/** @test Verifies formatBytesHex with empty data. */
TEST(ByteTraceTest, FormatBytesHexEmpty) {
  char buf[16];
  std::size_t len = formatBytesHex(nullptr, 0, buf, sizeof(buf));
  EXPECT_EQ(len, 0u);
  EXPECT_EQ(buf[0], '\0');
}

/** @test Verifies formatBytesHex with null output buffer. */
TEST(ByteTraceTest, FormatBytesHexNullOut) {
  const std::uint8_t DATA[] = {0x01};
  EXPECT_EQ(formatBytesHex(DATA, 1, nullptr, 0), 0u);
}

/** @test Verifies formatBytesHex with zero output size. */
TEST(ByteTraceTest, FormatBytesHexZeroOutSize) {
  const std::uint8_t DATA[] = {0x01};
  char buf[1];
  EXPECT_EQ(formatBytesHex(DATA, 1, buf, 0), 0u);
}

/** @test Verifies formatBytesHex with tiny output buffer. */
TEST(ByteTraceTest, FormatBytesHexTinyBuffer) {
  const std::uint8_t DATA[] = {0xAB, 0xCD, 0xEF};
  char buf[4]; // Room for "AB" + null but not "AB CD"
  std::size_t len = formatBytesHex(DATA, 3, buf, sizeof(buf));

  // Should write as many complete hex pairs as fit
  EXPECT_GT(len, 0u);
  EXPECT_LE(len, 3u);
}

/** @test Verifies formatBytesHex exact maxBytes boundary (no truncation). */
TEST(ByteTraceTest, FormatBytesHexExactMaxBytes) {
  const std::uint8_t DATA[] = {0xAA, 0xBB, 0xCC};
  char buf[64];
  std::size_t len = formatBytesHex(DATA, 3, buf, sizeof(buf), 3);

  EXPECT_STREQ(buf, "AA BB CC");
  EXPECT_EQ(len, 8u); // No ellipsis when len == maxBytes
}

/* ----------------------------- formatTraceMessage Tests ----------------------------- */

/** @test Verifies formatTraceMessage output format. */
TEST(ByteTraceTest, FormatTraceMessageTypical) {
  const std::uint8_t DATA[] = {0xDE, 0xAD, 0xBE, 0xEF};
  char buf[128];
  std::size_t len = formatTraceMessage(TraceDirection::TX, DATA, 4, buf, sizeof(buf), "TCP");

  EXPECT_STREQ(buf, "[TCP] TX (4 bytes): DE AD BE EF");
  EXPECT_EQ(len, std::strlen(buf));
}

/** @test Verifies formatTraceMessage with RX direction. */
TEST(ByteTraceTest, FormatTraceMessageRx) {
  const std::uint8_t DATA[] = {0x01, 0x02};
  char buf[128];
  formatTraceMessage(TraceDirection::RX, DATA, 2, buf, sizeof(buf), "UDP");

  EXPECT_STREQ(buf, "[UDP] RX (2 bytes): 01 02");
}

/** @test Verifies formatTraceMessage with single byte. */
TEST(ByteTraceTest, FormatTraceMessageSingleByte) {
  const std::uint8_t DATA[] = {0xFF};
  char buf[128];
  formatTraceMessage(TraceDirection::TX, DATA, 1, buf, sizeof(buf), "CAN");

  EXPECT_STREQ(buf, "[CAN] TX (1 bytes): FF");
}

/** @test Verifies formatTraceMessage with zero bytes. */
TEST(ByteTraceTest, FormatTraceMessageZeroLen) {
  char buf[128];
  formatTraceMessage(TraceDirection::RX, nullptr, 0, buf, sizeof(buf), "SPI");

  EXPECT_STREQ(buf, "[SPI] RX (0 bytes): ");
}

/** @test Verifies formatTraceMessage with null output buffer. */
TEST(ByteTraceTest, FormatTraceMessageNullOut) {
  const std::uint8_t DATA[] = {0x01};
  EXPECT_EQ(formatTraceMessage(TraceDirection::TX, DATA, 1, nullptr, 0, "X"), 0u);
}

/** @test Verifies formatTraceMessage with large byte count formats length correctly. */
TEST(ByteTraceTest, FormatTraceMessageLargeLen) {
  const std::uint8_t DATA[] = {0xAA};
  char buf[128];
  // Pass len=1024 but only 1 byte of actual data pointer (formatBytesHex handles this)
  formatTraceMessage(TraceDirection::TX, DATA, 1024, buf, sizeof(buf), "NET", 1);

  // Should show "1024" in the length field and truncate hex to maxBytes=1
  std::string result(buf);
  EXPECT_NE(result.find("1024 bytes"), std::string::npos);
  EXPECT_NE(result.find("AA"), std::string::npos);
}
