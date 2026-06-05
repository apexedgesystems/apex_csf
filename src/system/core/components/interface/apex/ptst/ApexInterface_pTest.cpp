/**
 * @file ApexInterface_pTest.cpp
 * @brief Performance tests for the ApexInterface inter-component bus.
 *
 * Measures:
 *  - BufferPool acquire / release, payload copy, refcount paths
 *  - SPSC and MPMC queue push/pop; QueueManager lookup
 *  - Internal bus command / telemetry post, multicast, broadcast
 *  - APROTO codec header encode / decode, round-trip, payload sweep
 *  - SLIP framing encode / decode of APROTO packets
 *  - End-to-end RX / TX pipelines and internal-bus round-trip
 *
 * Usage:
 *   ./ApexInterface_PTEST --csv results.csv
 *   ./ApexInterface_PTEST --quick
 *   ./ApexInterface_PTEST --profile gperf --cycles 100000
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/components/interface/apex/inc/ApexInterface.hpp"
#include "src/system/core/components/interface/apex/inc/BufferPool.hpp"
#include "src/system/core/components/interface/apex/inc/ComponentQueues.hpp"
#include "src/system/core/components/interface/apex/inc/MessageBuffer.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoCodec.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoTypes.hpp"
#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"
#include "src/utilities/concurrency/inc/LockFreeQueue.hpp"
#include "src/utilities/concurrency/inc/SPSCQueue.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ub = vernier::bench;
namespace aproto = system_core::protocols::aproto;

using system_core::interface::ApexInterface;
using system_core::interface::BufferPool;
using system_core::interface::ComponentQueues;
using system_core::interface::MessageBuffer;
using system_core::interface::QueueManager;

/* ----------------------------- Helpers ----------------------------- */

namespace {

/// Build a valid APROTO packet with payload into buf. Returns bytes written.
inline std::size_t buildTestPacket(std::uint8_t* buf, std::size_t bufSize,
                                   const std::uint8_t* payload, std::size_t payloadLen) {
  aproto::AprotoHeader hdr =
      aproto::buildHeader(0x006500, 0x0001, 0, static_cast<std::uint16_t>(payloadLen));
  std::size_t written = 0;
  (void)aproto::encodePacket(hdr, {payload, payloadLen}, {buf, bufSize}, written);
  return written;
}

/// Build SLIP-framed APROTO packet. Returns frame length.
inline std::size_t buildFramedPacket(std::uint8_t* frameBuf, std::size_t frameBufSize,
                                     const std::uint8_t* payload, std::size_t payloadLen) {
  std::array<std::uint8_t, 4096> packet{};
  const std::size_t PACKET_LEN = buildTestPacket(packet.data(), packet.size(), payload, payloadLen);
  auto r = apex::protocols::slip::encode({packet.data(), PACKET_LEN}, frameBuf, frameBufSize);
  return static_cast<std::size_t>(r.bytesProduced);
}

} // namespace

/* ----------------------------- BufferPool Benchmarks ----------------------------- */

/** @brief Measure BufferPool acquire/release cycle (no payload copy). */
PERF_TEST(BufferPoolPerf, AcquireReleaseCycle) {
  UB_PERF_GUARD(perf);

  BufferPool pool(128, 4096);

  auto fn = [&]() {
    MessageBuffer* buf = pool.acquire(64);
    pool.release(buf);
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "BufferPool_AcquireRelease");
}

/** @brief Measure acquire + memcpy + release (simulates real payload fill). */
PERF_TEST(BufferPoolPerf, AcquireCopyRelease) {
  UB_PERF_GUARD(perf);

  BufferPool pool(128, 4096);
  std::array<std::uint8_t, 256> payload{};
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>(i);
  }

  auto fn = [&]() {
    MessageBuffer* buf = pool.acquire(payload.size());
    std::memcpy(buf->data, payload.data(), payload.size());
    buf->length = payload.size();
    buf->fullUid = 0x006500;
    buf->opcode = 0x0001;
    buf->sequence = 42;
    pool.release(buf);
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "BufferPool_AcquireCopyRelease_256B");
}

/** @brief Measure acquire/release under contention (exhaust and refill cycle). */
PERF_TEST(BufferPoolPerf, BatchAcquireRelease) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t BATCH_SIZE = 32;
  BufferPool pool(BATCH_SIZE, 512);
  std::array<MessageBuffer*, BATCH_SIZE> acquired{};

  auto fn = [&]() {
    // Acquire all buffers.
    for (std::size_t i = 0; i < BATCH_SIZE; ++i) {
      acquired[i] = pool.acquire(64);
    }
    // Release all buffers.
    for (std::size_t i = 0; i < BATCH_SIZE; ++i) {
      pool.release(acquired[i]);
    }
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "BufferPool_Batch32_AcquireRelease");
}

/** @brief Measure refcount decrement path (multicast release). */
PERF_TEST(BufferPoolPerf, RefcountRelease) {
  UB_PERF_GUARD(perf);

  BufferPool pool(128, 512);

  auto fn = [&]() {
    MessageBuffer* buf = pool.acquire(64);
    // Simulate multicast: set refcount to 4, then release 4 times.
    buf->setRefCount(4);
    pool.release(buf); // refcount 4->3
    pool.release(buf); // refcount 3->2
    pool.release(buf); // refcount 2->1
    pool.release(buf); // refcount 1->0, returns to pool
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "BufferPool_RefcountRelease_4x");
}

/* ----------------------------- Queue Benchmarks ----------------------------- */

/** @brief Measure SPSCQueue push/pop throughput with MessageBuffer pointers. */
PERF_TEST(QueuePerf, SPSCPushPop) {
  UB_PERF_GUARD(perf);

  apex::concurrency::SPSCQueue<MessageBuffer*> queue(128);
  BufferPool pool(128, 512);

  auto fn = [&]() {
    MessageBuffer* buf = pool.acquire(32);
    (void)queue.tryPush(buf);
    MessageBuffer* out = nullptr;
    (void)queue.tryPop(out);
    pool.release(out);
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "SPSCQueue_PushPop_Pointer");
}

/** @brief Measure LockFreeQueue (MPMC) push/pop with MessageBuffer pointers. */
PERF_TEST(QueuePerf, MPMCPushPop) {
  UB_PERF_GUARD(perf);

  apex::concurrency::LockFreeQueue<MessageBuffer*> queue(128);
  BufferPool pool(128, 512);

  auto fn = [&]() {
    MessageBuffer* buf = pool.acquire(32);
    (void)queue.tryPush(buf);
    MessageBuffer* out = nullptr;
    (void)queue.tryPop(out);
    pool.release(out);
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "MPMCQueue_PushPop_Pointer");
}

/** @brief Measure QueueManager lookup by fullUid (hash map access). */
PERF_TEST(QueuePerf, QueueManagerLookup) {
  UB_PERF_GUARD(perf);

  QueueManager mgr;
  // Register 12 components (realistic executive setup).
  for (std::uint32_t uid = 0; uid < 12; ++uid) {
    (void)mgr.allocate(uid);
  }
  mgr.freeze();

  std::uint32_t idx = 0;
  auto fn = [&]() {
    const std::uint32_t UID = idx % 12;
    volatile auto* q = mgr.get(UID);
    (void)q;
    ++idx;
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "QueueManager_Lookup_12Components");
}

/* ----------------------------- Internal Bus Benchmarks ----------------------------- */

/** @brief Measure postInternalCommand throughput (acquire + copy + push). */
PERF_TEST(InternalBusPerf, PostInternalCommand) {
  UB_PERF_GUARD(perf);

  ApexInterface iface;
  // Allocate queues for target component (no socket config needed for internal bus).
  constexpr std::uint32_t DST_UID = 0x006500;
  (void)iface.allocateQueues(DST_UID);

  std::array<std::uint8_t, 64> payload{};
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>(i);
  }

  auto fn = [&]() {
    (void)iface.postInternalCommand(0x000100, DST_UID, 0x0001, {payload.data(), payload.size()});
    // Drain to prevent queue full.
    MessageBuffer* out = nullptr;
    auto* q = iface.getQueues(DST_UID);
    if (q != nullptr && q->cmdInbox.tryPop(out)) {
      // Simulate consuming the buffer: read metadata + release.
      volatile auto uid = out->fullUid;
      volatile auto op = out->opcode;
      (void)uid;
      (void)op;
      // Release via pool (need to use release path).
      // Since we popped from queue, decRef will return to pool.
      (void)out->decRef(); // refcount 1->0
    }
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "InternalBus_PostCommand_64B");
}

/** @brief Measure postInternalTelemetry throughput. */
PERF_TEST(InternalBusPerf, PostInternalTelemetry) {
  UB_PERF_GUARD(perf);

  ApexInterface iface;
  constexpr std::uint32_t SRC_UID = 0x006500;
  (void)iface.allocateQueues(SRC_UID);

  std::array<std::uint8_t, 128> payload{};
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>(i);
  }

  auto fn = [&]() {
    (void)iface.postInternalTelemetry(SRC_UID, 0x0001, {payload.data(), payload.size()});
    // Drain to prevent queue full.
    MessageBuffer* out = nullptr;
    auto* q = iface.getQueues(SRC_UID);
    if (q != nullptr && q->tlmOutbox.tryPop(out)) {
      volatile auto uid = out->fullUid;
      (void)uid;
      (void)out->decRef();
    }
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "InternalBus_PostTelemetry_128B");
}

/** @brief Measure multicast command to 4 recipients. */
PERF_TEST(InternalBusPerf, MulticastCommand4) {
  UB_PERF_GUARD(perf);

  ApexInterface iface;
  std::array<std::uint32_t, 4> targets = {0x006500, 0x006600, 0x006700, 0x006800};
  for (auto uid : targets) {
    (void)iface.allocateQueues(uid);
  }

  std::array<std::uint8_t, 32> payload{};
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>(i);
  }

  auto fn = [&]() {
    (void)iface.postMulticastCommand(0x000100, {targets.data(), targets.size()}, 0x0001,
                                     {payload.data(), payload.size()});
    // Drain all queues to prevent full.
    for (auto uid : targets) {
      auto* q = iface.getQueues(uid);
      MessageBuffer* out = nullptr;
      if (q != nullptr && q->cmdInbox.tryPop(out)) {
        (void)out->decRef();
      }
    }
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "InternalBus_Multicast_4Targets_32B");
}

/** @brief Measure broadcast command to 8 recipients. */
PERF_TEST(InternalBusPerf, BroadcastCommand8) {
  UB_PERF_GUARD(perf);

  ApexInterface iface;
  std::array<std::uint32_t, 9> uids = {0x000100, 0x006500, 0x006600, 0x006601, 0x006700,
                                       0x006800, 0x006900, 0x006A00, 0x006B00};
  for (auto uid : uids) {
    (void)iface.allocateQueues(uid);
  }

  std::array<std::uint8_t, 16> payload{};

  auto fn = [&]() {
    // Broadcast from uid[0], should reach 8 others.
    (void)iface.postBroadcastCommand(uids[0], 0x0001, {payload.data(), payload.size()});
    // Drain all queues.
    for (std::size_t i = 1; i < uids.size(); ++i) {
      auto* q = iface.getQueues(uids[i]);
      MessageBuffer* out = nullptr;
      if (q != nullptr && q->cmdInbox.tryPop(out)) {
        (void)out->decRef();
      }
    }
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "InternalBus_Broadcast_8Targets_16B");
}

/* ----------------------------- APROTO Codec Benchmarks ----------------------------- */

/** @brief Benchmark APROTO header-only encoding. */
PERF_TEST(AprotoCodecPerf, EncodeHeaderOnly) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 64> buf{};
  aproto::AprotoHeader hdr = aproto::buildHeader(0x006500, 0x0001, 0, 0);

  auto fn = [&]() {
    std::size_t written = 0;
    (void)aproto::encodePacket(hdr, {}, {buf.data(), buf.size()}, written);
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "APROTO_EncodeHeaderOnly");
}

/** @brief Benchmark APROTO header-only decoding. */
PERF_TEST(AprotoCodecPerf, DecodeHeaderOnly) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 64> buf{};
  std::size_t written = 0;
  (void)aproto::encodePacket(aproto::buildHeader(0x006500, 0x0001, 42, 0), {},
                             {buf.data(), buf.size()}, written);
  apex::compat::rospan<std::uint8_t> inSpan{buf.data(), written};

  auto fn = [&]() {
    aproto::PacketView view{};
    (void)aproto::createPacketView(inSpan, view);
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "APROTO_DecodeHeaderOnly");
}

/** @brief Benchmark APROTO encode + decode round-trip with 64B payload. */
PERF_TEST(AprotoCodecPerf, RoundTrip64B) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 128> buf{};
  std::array<std::uint8_t, 64> payload{};
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>(i);
  }

  auto fn = [&]() {
    aproto::AprotoHeader hdr = aproto::buildHeader(0x006500, 0x0001, 0, 64);
    std::size_t written = 0;
    (void)aproto::encodePacket(hdr, {payload.data(), payload.size()}, {buf.data(), buf.size()},
                               written);
    aproto::PacketView view{};
    (void)aproto::createPacketView({buf.data(), written}, view);
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "APROTO_RoundTrip_64B");
}

/** @brief Benchmark APROTO encode with 1KB payload (data copy dominated). */
PERF_TEST(AprotoCodecPerf, Encode1KB) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 2048> buf{};
  std::array<std::uint8_t, 1024> payload{};
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>(i);
  }

  aproto::AprotoHeader hdr = aproto::buildHeader(0x006500, 0x0001, 0, 1024);

  auto fn = [&]() {
    std::size_t written = 0;
    (void)aproto::encodePacket(hdr, {payload.data(), payload.size()}, {buf.data(), buf.size()},
                               written);
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "APROTO_Encode_1KB");
}

/* ----------------------------- SLIP Framing Benchmarks ----------------------------- */

/** @brief Benchmark SLIP encoding of 22-byte APROTO packet (header + 8B payload). */
PERF_TEST(SlipFramingPerf, EncodeSmallPacket) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 64> packet{};
  std::array<std::uint8_t, 8> payload{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  const std::size_t PACKET_LEN =
      buildTestPacket(packet.data(), packet.size(), payload.data(), payload.size());
  std::array<std::uint8_t, 256> frameBuf{};

  auto fn = [&]() {
    (void)apex::protocols::slip::encode({packet.data(), PACKET_LEN}, frameBuf.data(),
                                        frameBuf.size());
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "SLIP_Encode_22B_Packet");
}

/** @brief Benchmark SLIP decode of framed APROTO packet. */
PERF_TEST(SlipFramingPerf, DecodeSmallPacket) {
  UB_PERF_GUARD(perf);

  std::array<std::uint8_t, 8> payload{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  std::array<std::uint8_t, 256> frameBuf{};
  const std::size_t FRAME_LEN =
      buildFramedPacket(frameBuf.data(), frameBuf.size(), payload.data(), payload.size());
  std::array<std::uint8_t, 128> decodeBuf{};

  auto fn = [&]() {
    apex::protocols::slip::DecodeState st;
    apex::protocols::slip::DecodeConfig cfg;
    (void)apex::protocols::slip::decodeChunk(st, cfg, {frameBuf.data(), FRAME_LEN},
                                             decodeBuf.data(), decodeBuf.size());
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "SLIP_Decode_22B_Packet");
}

/* ----------------------------- End-to-End Benchmarks ----------------------------- */

/** @brief Full RX pipeline: SLIP decode -> APROTO parse -> buffer acquire + copy. */
PERF_TEST(InterfacePerf, RxPipeline) {
  UB_PERF_GUARD(perf);

  BufferPool pool(128, 4096);

  // Pre-build a SLIP-framed APROTO command packet.
  std::array<std::uint8_t, 64> payload{};
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>(i);
  }
  std::array<std::uint8_t, 512> frameBuf{};
  const std::size_t FRAME_LEN =
      buildFramedPacket(frameBuf.data(), frameBuf.size(), payload.data(), payload.size());

  std::array<std::uint8_t, 256> decodeBuf{};

  auto fn = [&]() {
    // 1. SLIP decode
    apex::protocols::slip::DecodeState st;
    apex::protocols::slip::DecodeConfig cfg;
    auto slipR = apex::protocols::slip::decodeChunk(st, cfg, {frameBuf.data(), FRAME_LEN},
                                                    decodeBuf.data(), decodeBuf.size());

    // 2. APROTO parse
    aproto::PacketView view{};
    const std::size_t DECODED_LEN = static_cast<std::size_t>(slipR.bytesProduced);
    (void)aproto::createPacketView({decodeBuf.data(), DECODED_LEN}, view);

    // 3. Buffer acquire + payload copy (what routeToComponent does)
    MessageBuffer* buf = pool.acquire(view.payload.size());
    if (buf != nullptr) {
      if (!view.payload.empty()) {
        std::memcpy(buf->data, view.payload.data(), view.payload.size());
      }
      buf->length = view.payload.size();
      buf->fullUid = view.header.fullUid;
      buf->opcode = view.header.opcode;
      buf->sequence = view.header.sequence;
      pool.release(buf);
    }
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "Interface_RxPipeline_64B");
}

/** @brief Full TX pipeline: buffer read -> APROTO encode -> SLIP frame. */
PERF_TEST(InterfacePerf, TxPipeline) {
  UB_PERF_GUARD(perf);

  BufferPool pool(128, 4096);

  // Pre-fill a telemetry buffer.
  MessageBuffer* tlmBuf = pool.acquire(128);
  ASSERT_NE(tlmBuf, nullptr);
  for (std::size_t i = 0; i < 128; ++i) {
    tlmBuf->data[i] = static_cast<std::uint8_t>(i);
  }
  tlmBuf->length = 128;
  tlmBuf->fullUid = 0x006500;
  tlmBuf->opcode = 0x0001;
  tlmBuf->sequence = 42;

  std::array<std::uint8_t, 4096> responseBuf{};
  std::array<std::uint8_t, 4096> frameBuf{};

  auto fn = [&]() {
    // 1. APROTO encode (what drainTelemetryOutboxes does)
    const aproto::AprotoHeader HDR =
        aproto::buildHeader(tlmBuf->fullUid, tlmBuf->opcode, tlmBuf->sequence,
                            static_cast<std::uint16_t>(tlmBuf->length), true, false, false);
    std::size_t bytesWritten = 0;
    (void)aproto::encodePacket(HDR, {tlmBuf->data, tlmBuf->length},
                               {responseBuf.data(), responseBuf.size()}, bytesWritten);

    // 2. SLIP frame
    (void)apex::protocols::slip::encode({responseBuf.data(), bytesWritten}, frameBuf.data(),
                                        frameBuf.size());
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "Interface_TxPipeline_128B");

  pool.release(tlmBuf);
}

/** @brief Full internal bus round-trip: post command -> queue -> drain -> dispatch. */
PERF_TEST(InterfacePerf, InternalBusRoundTrip) {
  UB_PERF_GUARD(perf);

  // Simulate the full path using raw pool + queue (without socket overhead).
  BufferPool pool(128, 4096);
  apex::concurrency::LockFreeQueue<MessageBuffer*> cmdInbox(64);

  std::array<std::uint8_t, 64> payload{};
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>(i);
  }

  auto fn = [&]() {
    // 1. Post: acquire + copy + push (what postInternalCommand does)
    MessageBuffer* buf = pool.acquire(payload.size());
    std::memcpy(buf->data, payload.data(), payload.size());
    buf->length = payload.size();
    buf->fullUid = 0x006500;
    buf->opcode = 0x0001;
    buf->sequence = 0;
    buf->internalOrigin = true;
    (void)cmdInbox.tryPush(buf);

    // 2. Drain: pop + read metadata + release (what drainCommandsToComponents does)
    MessageBuffer* out = nullptr;
    if (cmdInbox.tryPop(out)) {
      // Simulate handleCommand: read opcode + payload
      volatile auto op = out->opcode;
      volatile auto len = out->length;
      (void)op;
      (void)len;
      pool.release(out);
    }
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "InternalBus_FullRoundTrip_64B");
}

PERF_MAIN()
