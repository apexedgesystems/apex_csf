/**
 * @file ShmRingBridge_uTest.cpp
 * @brief Unit tests for the ShmRingBridge SUPPORT component.
 *
 * Validates the apex-side Side A (owner) implementation against
 * horizon's BRIDGE_FORMAT.md spec by attaching a hand-rolled Side B
 * reader (POSIX shm + sem only; does NOT link horizon) and round-
 * tripping bytes through real shm.
 *
 * The Side B helper here is a minimal spec implementation -- enough to
 * prove that another consumer (e.g. UE5's URoverRingComponent, or a
 * recording tool) attaching to the same shm region sees the bytes the
 * apex bridge wrote.
 */

#include "src/system/core/support/shm_ring_bridge/inc/ShmRingBridge.hpp"
#include "src/system/core/support/shm_ring_bridge/inc/ShmRingBridgeData.hpp"

#include "src/system/core/infrastructure/system_component/posix/inc/DataCategory.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/IInternalBus.hpp"

#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using system_core::data::DataCategory;
using system_core::support::BRIDGE_FRAMEWORK_MAGIC;
using system_core::support::BRIDGE_FRAMEWORK_VERSION;
using system_core::support::BRIDGE_RING_HEADER_BYTES;
using system_core::support::BRIDGE_RING_PRELUDE_BYTES;
using system_core::support::BridgeResolveDelegate;
using system_core::support::ResolvedSource;
using system_core::support::ShmRingBridge;
using system_core::support::ShmRingBridgeTunables;

namespace {

/* ----------------------------- Spec mirror (Side B reader) ----------------------------- */

// Minimal Side B reader implementing BRIDGE_FORMAT.md sections 5.2 + 4.3
// + 6.5 just enough to verify that what the apex bridge writes is what
// any spec-conformant consumer reads. Open paths come from the same
// tunables the apex bridge used; the reader does its own shm_open /
// mmap / sem_open.
struct SideBReader {
  void* mapping = nullptr;
  std::size_t map_size = 0;
  void* sem = nullptr;
  std::atomic<std::uint64_t>* prod = nullptr;
  std::atomic<std::uint64_t>* cons = nullptr;
  std::uint8_t* slots = nullptr;
  std::uint32_t capacity = 0;
  std::uint32_t payload_size = 0;

  bool open(const std::string& shm_path, const std::string& sem_path, std::uint32_t cap,
            std::uint32_t payload_sz) {
    capacity = cap;
    payload_size = payload_sz;
    const std::size_t REGION =
        BRIDGE_RING_PRELUDE_BYTES + static_cast<std::size_t>(cap) * payload_sz;
    map_size = REGION * 2; // bridge mmaps both directions
    int fd = shm_open(shm_path.c_str(), O_RDWR, 0);
    if (fd < 0)
      return false;
    struct stat st{};
    if (fstat(fd, &st) != 0 || static_cast<std::size_t>(st.st_size) != map_size) {
      ::close(fd);
      return false;
    }
    mapping = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    if (mapping == MAP_FAILED) {
      mapping = nullptr;
      return false;
    }
    auto* base = static_cast<std::uint8_t*>(mapping);
    prod = reinterpret_cast<std::atomic<std::uint64_t>*>(base + BRIDGE_RING_HEADER_BYTES);
    cons = reinterpret_cast<std::atomic<std::uint64_t>*>(base + BRIDGE_RING_HEADER_BYTES + 64);
    slots = base + BRIDGE_RING_PRELUDE_BYTES;

    sem_t* s = sem_open(sem_path.c_str(), 0);
    if (s == SEM_FAILED)
      return false;
    sem = s;
    return true;
  }

  // Validate the framework + app identity in Region A's header. Returns
  // true if every check passes.
  bool validateHeader(std::uint32_t expect_app_magic, std::uint16_t expect_app_version) const {
    auto* base = static_cast<const std::uint8_t*>(mapping);
    std::uint32_t fmagic;
    std::uint16_t fver, fres;
    std::uint32_t amagic;
    std::uint16_t aver, ares;
    std::uint32_t paysz, cap;
    std::memcpy(&fmagic, base + 0, 4);
    std::memcpy(&fver, base + 4, 2);
    std::memcpy(&fres, base + 6, 2);
    std::memcpy(&amagic, base + 8, 4);
    std::memcpy(&aver, base + 12, 2);
    std::memcpy(&ares, base + 14, 2);
    std::memcpy(&paysz, base + 16, 4);
    std::memcpy(&cap, base + 20, 4);
    return fmagic == BRIDGE_FRAMEWORK_MAGIC && fver == BRIDGE_FRAMEWORK_VERSION &&
           amagic == expect_app_magic && aver == expect_app_version && paysz == payload_size &&
           cap == capacity;
  }

  // Pop one frame (BRIDGE_FORMAT.md section 4.3). Returns true if a
  // frame was available + copied into `out`.
  bool pop(std::uint8_t* out) {
    const std::uint64_t TAIL = cons->load(std::memory_order_relaxed);
    const std::uint64_t HEAD = prod->load(std::memory_order_acquire);
    if (TAIL == HEAD)
      return false;
    std::memcpy(out, slots + (TAIL & (capacity - 1u)) * payload_size, payload_size);
    cons->store(TAIL + 1u, std::memory_order_release);
    return true;
  }

  // Wait for a wakeup signal (BRIDGE_FORMAT.md section 6.5). Returns
  // true on wake, false on timeout.
  bool wait(int timeout_ms) {
    auto* s = static_cast<sem_t*>(sem);
    if (timeout_ms < 0) {
      while (sem_wait(s) != 0) {
        if (errno != EINTR)
          return false;
      }
      return true;
    }
    struct timespec abs{};
    clock_gettime(CLOCK_REALTIME, &abs);
    abs.tv_sec += timeout_ms / 1000;
    abs.tv_nsec += static_cast<long>(timeout_ms % 1000) * 1'000'000L;
    if (abs.tv_nsec >= 1'000'000'000L) {
      abs.tv_sec += 1;
      abs.tv_nsec -= 1'000'000'000L;
    }
    while (sem_timedwait(s, &abs) != 0) {
      if (errno == ETIMEDOUT)
        return false;
      if (errno != EINTR)
        return false;
    }
    return true;
  }

  void close() {
    if (mapping != nullptr) {
      munmap(mapping, map_size);
      mapping = nullptr;
    }
    if (sem != nullptr) {
      sem_close(static_cast<sem_t*>(sem));
      sem = nullptr;
    }
  }

  ~SideBReader() { close(); }
};

/* ----------------------------- Test resolver ----------------------------- */

struct ResolverCtx {
  std::array<std::uint8_t, 64> block{};
  std::uint32_t expectedUid = 0xDE00;
  DataCategory expectedCat = DataCategory::OUTPUT;
};

ResolvedSource testResolver(void* ctx, std::uint32_t uid, DataCategory cat) noexcept {
  auto* r = static_cast<ResolverCtx*>(ctx);
  if (uid != r->expectedUid || cat != r->expectedCat)
    return {};
  return {r->block.data(), r->block.size()};
}

/* ----------------------------- Helpers ----------------------------- */

std::string uniqueShmPath(const char* tag) {
  return std::string("/shm_ring_bridge_test_") + tag + "_" + std::to_string(::getpid());
}

void fillTunables(ShmRingBridgeTunables& t, const std::string& shm, std::uint32_t cap,
                  std::uint32_t payload_size, std::uint32_t source_uid) {
  t.app_magic = 0x54455354u; // "TEST"
  t.app_version = 1;
  t.capacity = cap;
  t.payload_size = payload_size;
  t.source_uid = source_uid;
  t.source_category = static_cast<std::uint8_t>(DataCategory::OUTPUT);
  t.source_byte_offset = 0;
  t.source_byte_len = static_cast<std::uint16_t>(payload_size);
  std::snprintf(t.shm_path, sizeof(t.shm_path), "%s", shm.c_str());
  // wakeup_path stays empty -> bridge derives shm + "_wake"
}

} // namespace

/* ----------------------------- Component identity ----------------------------- */

TEST(ShmRingBridge, identityFieldsMatchSpec) {
  ShmRingBridge b;
  EXPECT_EQ(b.componentId(), 203);
  EXPECT_STREQ(b.componentName(), "ShmRingBridge");
  EXPECT_STREQ(b.label(), "SHM_RING_BRIDGE");
}

/* ----------------------------- doInit + tunable defaults ----------------------------- */

TEST(ShmRingBridge, defaultStateIsIdle) {
  ShmRingBridge b;
  EXPECT_EQ(b.bridgeState().tick_count, 0u);
  EXPECT_EQ(b.bridgeState().frames_published, 0u);
  EXPECT_EQ(b.bridgeState().channel_open, 0u);
  EXPECT_EQ(b.bridgeState().source_resolved, 0u);
}

TEST(ShmRingBridge, bridgeStepNoOpsWhenChannelClosed) {
  ShmRingBridge b;
  // Without onBusReady() being called, channel_open == 0; bridgeStep
  // must succeed (sim must not be blocked) but publish nothing.
  EXPECT_EQ(b.bridgeStep(), 0u);
  EXPECT_EQ(b.bridgeState().tick_count, 1u);
  EXPECT_EQ(b.bridgeState().frames_published, 0u);
}

/* ----------------------------- onBusReady + round-trip ----------------------------- */

TEST(ShmRingBridge, openValidatesAbsoluteShmPath) {
  ShmRingBridge b;
  ResolverCtx ctx;
  b.setResolver(testResolver, &ctx);

  // Configure with a NON-absolute path; onBusReady should fail to open
  // the channel and leave the bridge idle.
  ShmRingBridgeTunables& t = b.tunables().get();
  fillTunables(t, "no_leading_slash", 16, 64, ctx.expectedUid);

  b.setInstanceIndex(0);
  ASSERT_EQ(b.init(), 0u);
  b.onBusReady();
  EXPECT_EQ(b.bridgeState().channel_open, 0u);
}

TEST(ShmRingBridge, openValidatesPowerOfTwoCapacity) {
  ShmRingBridge b;
  ResolverCtx ctx;
  b.setResolver(testResolver, &ctx);

  ShmRingBridgeTunables& t = b.tunables().get();
  fillTunables(t, uniqueShmPath("badcap"), /*cap=*/13, /*payload_size=*/64, ctx.expectedUid);

  b.setInstanceIndex(0);
  ASSERT_EQ(b.init(), 0u);
  b.onBusReady();
  EXPECT_EQ(b.bridgeState().channel_open, 0u);
}

TEST(ShmRingBridge, sourceLengthMustMatchPayloadSize) {
  ShmRingBridge b;
  ResolverCtx ctx; // block.size() == 64
  b.setResolver(testResolver, &ctx);

  ShmRingBridgeTunables& t = b.tunables().get();
  // Configure payload_size that doesn't match source size.
  fillTunables(t, uniqueShmPath("sizemismatch"), 16, /*payload_size=*/32, ctx.expectedUid);
  // Override source_byte_len to 64 (the actual block) so the mismatch
  // is between source_byte_len (64) and payload_size (32).
  t.source_byte_len = 64;

  b.setInstanceIndex(0);
  ASSERT_EQ(b.init(), 0u);
  b.onBusReady();
  // openChannel succeeds; resolveSource fails on the length mismatch
  // and onBusReady tears the channel back down.
  EXPECT_EQ(b.bridgeState().channel_open, 0u);
}

TEST(ShmRingBridge, fullEndToEndPublish) {
  // Configure the bridge to publish a 64-byte source block, attach a
  // hand-rolled Side B reader, run a few bridgeStep() ticks, and
  // verify the consumer reads exactly what the producer wrote.
  ShmRingBridge b;
  ResolverCtx ctx;
  // Prime the source block with a recognizable pattern we'll later
  // mutate per-frame.
  for (std::size_t i = 0; i < ctx.block.size(); ++i) {
    ctx.block[i] = static_cast<std::uint8_t>(0xA0 + i);
  }
  b.setResolver(testResolver, &ctx);

  const std::string SHM = uniqueShmPath("e2e");
  ShmRingBridgeTunables& t = b.tunables().get();
  fillTunables(t, SHM, /*cap=*/16, /*payload_size=*/64, ctx.expectedUid);

  b.setInstanceIndex(0);
  ASSERT_EQ(b.init(), 0u);
  b.onBusReady();
  ASSERT_EQ(b.bridgeState().channel_open, 1u);
  ASSERT_EQ(b.bridgeState().source_resolved, 1u);

  // Attach Side B (this is the spec-only path that any independent
  // consumer would walk).
  SideBReader reader;
  ASSERT_TRUE(reader.open(SHM, SHM + "_wake", t.capacity, t.payload_size));
  EXPECT_TRUE(reader.validateHeader(t.app_magic, t.app_version));

  // Push N frames; mutate the source block between pushes to confirm
  // each slot carries distinct bytes.
  constexpr int N = 5;
  for (int i = 0; i < N; ++i) {
    ctx.block[0] = static_cast<std::uint8_t>(0xE0 + i); // distinguishing byte
    EXPECT_EQ(b.bridgeStep(), 0u);
  }
  EXPECT_EQ(b.bridgeState().frames_published, N);

  // Consumer should see N wakeups + N frames in sequence.
  std::array<std::uint8_t, 64> rx{};
  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(reader.wait(1000)) << "missed wakeup " << i;
    ASSERT_TRUE(reader.pop(rx.data())) << "missed frame " << i;
    EXPECT_EQ(rx[0], static_cast<std::uint8_t>(0xE0 + i));
    // Bytes 1.. carry the original 0xA0 + j pattern unchanged.
    for (std::size_t j = 1; j < rx.size(); ++j) {
      EXPECT_EQ(rx[j], static_cast<std::uint8_t>(0xA0 + j)) << "byte " << j << " of frame " << i;
    }
  }
  EXPECT_FALSE(reader.pop(rx.data())) << "spurious extra frame";

  reader.close();
  // Bridge dtor cleans up shm + sem.
}

/* ================================================================== */
/* Ring B command-sink (UE5 -> apex via APROTO over SHM) tests         */
/* ================================================================== */

namespace {

/// Spec-compatible Side B *writer* for Ring B (consumer -> apex direction).
/// Side B owns Ring B's producer cursor; apex owns the consumer cursor.
/// Mirror of SideBReader's structure but operates on Region B (offset by
/// Region A bytes) and writes instead of reads.
struct SideBWriter {
  void* mapping = nullptr;
  std::size_t map_size = 0;
  std::atomic<std::uint64_t>* prod = nullptr;
  std::atomic<std::uint64_t>* cons = nullptr;
  std::uint8_t* slots = nullptr;
  std::uint32_t capacity = 0;
  std::uint32_t slot_size = 0;

  bool open(const std::string& shm_path, std::uint32_t cap, std::uint32_t fwd_payload,
            std::uint32_t rev_payload) {
    capacity = cap;
    slot_size = rev_payload;
    const std::size_t REGION_A =
        BRIDGE_RING_PRELUDE_BYTES + static_cast<std::size_t>(cap) * fwd_payload;
    const std::size_t REGION_B =
        BRIDGE_RING_PRELUDE_BYTES + static_cast<std::size_t>(cap) * rev_payload;
    map_size = REGION_A + REGION_B;
    int fd = shm_open(shm_path.c_str(), O_RDWR, 0);
    if (fd < 0)
      return false;
    mapping = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    if (mapping == MAP_FAILED) {
      mapping = nullptr;
      return false;
    }
    auto* base_b = static_cast<std::uint8_t*>(mapping) + REGION_A;
    prod = reinterpret_cast<std::atomic<std::uint64_t>*>(base_b + BRIDGE_RING_HEADER_BYTES);
    cons = reinterpret_cast<std::atomic<std::uint64_t>*>(base_b + BRIDGE_RING_HEADER_BYTES + 64);
    slots = base_b + BRIDGE_RING_PRELUDE_BYTES;
    return true;
  }

  // Push raw slot bytes into Ring B. Returns false if ring is full.
  bool push(const std::uint8_t* data, std::size_t n) {
    const std::uint64_t HEAD = prod->load(std::memory_order_relaxed);
    const std::uint64_t TAIL = cons->load(std::memory_order_acquire);
    if (HEAD - TAIL >= capacity)
      return false;
    std::uint8_t* slot = slots + (HEAD & (capacity - 1u)) * slot_size;
    // Zero whole slot first to keep payload bytes deterministic.
    std::memset(slot, 0, slot_size);
    std::memcpy(slot, data, std::min<std::size_t>(n, slot_size));
    prod->store(HEAD + 1u, std::memory_order_release);
    return true;
  }

  void close() {
    if (mapping != nullptr) {
      munmap(mapping, map_size);
      mapping = nullptr;
    }
  }
  ~SideBWriter() { close(); }
};

/// Build an APROTO application-layer frame (no SLIP/COBS framing -- Ring B
/// slot boundary provides framing per the BRIDGE_FORMAT.md spec).
/// Returns the encoded 14-byte header + payload as a vector.
std::vector<std::uint8_t> buildAprotoFrame(std::uint32_t full_uid, std::uint16_t opcode,
                                           const std::vector<std::uint8_t>& payload,
                                           std::uint16_t magic = 0x5041u, std::uint8_t version = 1u,
                                           std::uint8_t flags = 0u, std::uint16_t sequence = 1u) {
  std::vector<std::uint8_t> frame(14 + payload.size(), 0u);
  const std::uint16_t payload_len = static_cast<std::uint16_t>(payload.size());
  std::memcpy(frame.data() + 0, &magic, sizeof(magic));
  std::memcpy(frame.data() + 2, &version, sizeof(version));
  std::memcpy(frame.data() + 3, &flags, sizeof(flags));
  std::memcpy(frame.data() + 4, &full_uid, sizeof(full_uid));
  std::memcpy(frame.data() + 8, &opcode, sizeof(opcode));
  std::memcpy(frame.data() + 10, &sequence, sizeof(sequence));
  std::memcpy(frame.data() + 12, &payload_len, sizeof(payload_len));
  if (!payload.empty()) {
    std::memcpy(frame.data() + 14, payload.data(), payload.size());
  }
  return frame;
}

/// Mock IInternalBus that captures the most recent postInternalCommand
/// call so tests can assert routing + payload were correct.
struct MockInternalBus : public system_core::system_component::IInternalBus {
  // Recorded args from postInternalCommand. last_payload is copied so
  // it survives after the rospan's underlying buffer is reused.
  std::uint32_t last_src_uid = 0;
  std::uint32_t last_dst_uid = 0;
  std::uint16_t last_opcode = 0;
  std::vector<std::uint8_t> last_payload;
  std::size_t cmd_count = 0;
  bool next_post_should_fail = false; // set to test dispatch_errors path.

  bool postInternalCommand(std::uint32_t srcFullUid, std::uint32_t dstFullUid, std::uint16_t opcode,
                           apex::compat::rospan<std::uint8_t> payload) noexcept override {
    last_src_uid = srcFullUid;
    last_dst_uid = dstFullUid;
    last_opcode = opcode;
    last_payload.assign(payload.data(), payload.data() + payload.size());
    ++cmd_count;
    return !next_post_should_fail;
  }
  bool postInternalTelemetry(std::uint32_t, std::uint16_t,
                             apex::compat::rospan<std::uint8_t>) noexcept override {
    return true;
  }
  std::size_t postMulticastCommand(std::uint32_t, apex::compat::rospan<std::uint32_t>,
                                   std::uint16_t,
                                   apex::compat::rospan<std::uint8_t>) noexcept override {
    return 0;
  }
  std::size_t postBroadcastCommand(std::uint32_t, std::uint16_t,
                                   apex::compat::rospan<std::uint8_t>) noexcept override {
    return 0;
  }
};

/// Common setup: bring a bridge up with Ring B sink enabled, mock bus
/// wired, and return SideBWriter ready to push APROTO frames.
struct SinkFixture {
  ShmRingBridge bridge;
  ResolverCtx ctx;
  MockInternalBus bus;
  SideBWriter writer;

  // payload_size for Ring A; rev_payload for Ring B (carries APROTO frames).
  bool setup(const std::string& shm, std::uint32_t cap, std::uint32_t fwd_payload,
             std::uint32_t rev_payload) {
    bridge.setResolver(testResolver, &ctx);
    bridge.setInternalBus(&bus);
    auto& t = bridge.tunables().get();
    fillTunables(t, shm, cap, fwd_payload, ctx.expectedUid);
    t.reverse_payload_size = rev_payload;
    t.sink_enabled = 1u;
    bridge.setInstanceIndex(0);
    if (bridge.init() != 0u)
      return false;
    bridge.onBusReady();
    if (bridge.bridgeState().channel_open == 0u)
      return false;
    return writer.open(shm, cap, fwd_payload, rev_payload);
  }
};

} // namespace

TEST(ShmRingBridge, ringB_sinkDisabledIgnoresFrames) {
  ShmRingBridge b;
  ResolverCtx ctx;
  MockInternalBus bus;
  b.setResolver(testResolver, &ctx);
  b.setInternalBus(&bus);

  auto& t = b.tunables().get();
  fillTunables(t, uniqueShmPath("sink_off"), 16, 64, ctx.expectedUid);
  t.reverse_payload_size = 64;
  t.sink_enabled = 0u; // explicitly off

  b.setInstanceIndex(0);
  ASSERT_EQ(b.init(), 0u);
  b.onBusReady();
  ASSERT_EQ(b.bridgeState().channel_open, 1u);

  // Push a valid frame via Side B and run bridgeStep -- should NOT dispatch.
  SideBWriter w;
  ASSERT_TRUE(w.open(t.shm_path, 16, 64, 64));
  auto frame = buildAprotoFrame(0xE000u, 0x0100u, {0x01});
  ASSERT_TRUE(w.push(frame.data(), frame.size()));

  EXPECT_EQ(b.bridgeStep(), 0u);
  EXPECT_EQ(bus.cmd_count, 0u);
  EXPECT_EQ(b.bridgeState().cmds_received, 0u);
}

TEST(ShmRingBridge, ringB_emptyDrainIsNoOp) {
  SinkFixture f;
  ASSERT_TRUE(f.setup(uniqueShmPath("empty_drain"), 16, 64, 64));

  // No frame pushed; bridgeStep should still publish on Ring A but not
  // increment any rx counters.
  EXPECT_EQ(f.bridge.bridgeStep(), 0u);
  EXPECT_EQ(f.bridge.bridgeState().cmds_received, 0u);
  EXPECT_EQ(f.bridge.bridgeState().cmds_decode_errors, 0u);
  EXPECT_EQ(f.bus.cmd_count, 0u);
}

TEST(ShmRingBridge, ringB_dispatchesValidAprotoFrame) {
  SinkFixture f;
  ASSERT_TRUE(f.setup(uniqueShmPath("valid"), 16, 64, 256));

  const std::vector<std::uint8_t> payload = {0x01}; // SET enable=ON
  auto frame = buildAprotoFrame(/*full_uid=*/0xE000u, /*opcode=*/0x0100u, payload);
  ASSERT_TRUE(f.writer.push(frame.data(), frame.size()));

  EXPECT_EQ(f.bridge.bridgeStep(), 0u);

  EXPECT_EQ(f.bridge.bridgeState().cmds_received, 1u);
  EXPECT_EQ(f.bridge.bridgeState().cmds_decode_errors, 0u);
  EXPECT_EQ(f.bridge.bridgeState().cmds_dispatch_errors, 0u);

  EXPECT_EQ(f.bus.cmd_count, 1u);
  EXPECT_EQ(f.bus.last_dst_uid, 0xE000u);
  EXPECT_EQ(f.bus.last_opcode, 0x0100u);
  ASSERT_EQ(f.bus.last_payload.size(), 1u);
  EXPECT_EQ(f.bus.last_payload[0], 0x01);
}

TEST(ShmRingBridge, ringB_rejectsBadMagic) {
  SinkFixture f;
  ASSERT_TRUE(f.setup(uniqueShmPath("bad_magic"), 16, 64, 256));

  auto frame = buildAprotoFrame(0xE000u, 0x0100u, {0x01},
                                /*magic=*/0xDEADu); // wrong magic
  ASSERT_TRUE(f.writer.push(frame.data(), frame.size()));

  EXPECT_EQ(f.bridge.bridgeStep(), 0u);
  EXPECT_EQ(f.bridge.bridgeState().cmds_decode_errors, 1u);
  EXPECT_EQ(f.bridge.bridgeState().cmds_received, 0u);
  EXPECT_EQ(f.bus.cmd_count, 0u);
}

TEST(ShmRingBridge, ringB_rejectsBadVersion) {
  SinkFixture f;
  ASSERT_TRUE(f.setup(uniqueShmPath("bad_ver"), 16, 64, 256));

  auto frame = buildAprotoFrame(0xE000u, 0x0100u, {0x01},
                                /*magic=*/0x5041u, /*version=*/99u);
  ASSERT_TRUE(f.writer.push(frame.data(), frame.size()));

  EXPECT_EQ(f.bridge.bridgeStep(), 0u);
  EXPECT_EQ(f.bridge.bridgeState().cmds_decode_errors, 1u);
  EXPECT_EQ(f.bus.cmd_count, 0u);
}

TEST(ShmRingBridge, ringB_rejectsOversizedPayload) {
  // Slot is 32 bytes; APROTO header is 14; max payload that fits = 18.
  // Push a frame claiming payload_len = 100 (would overflow the slot).
  SinkFixture f;
  ASSERT_TRUE(f.setup(uniqueShmPath("oversz"), 16, 64, /*rev_payload=*/32));

  // Manually craft frame with bogus payload_len (don't actually include
  // the data -- slot is too small anyway).
  std::vector<std::uint8_t> frame(14, 0u);
  const std::uint16_t magic = 0x5041u;
  const std::uint8_t version = 1u;
  const std::uint8_t flags = 0u;
  const std::uint32_t uid = 0xE000u;
  const std::uint16_t op = 0x0100u;
  const std::uint16_t seq = 0u;
  const std::uint16_t bad_len = 100u; // > rev_payload - 14
  std::memcpy(frame.data() + 0, &magic, 2);
  std::memcpy(frame.data() + 2, &version, 1);
  std::memcpy(frame.data() + 3, &flags, 1);
  std::memcpy(frame.data() + 4, &uid, 4);
  std::memcpy(frame.data() + 8, &op, 2);
  std::memcpy(frame.data() + 10, &seq, 2);
  std::memcpy(frame.data() + 12, &bad_len, 2);
  ASSERT_TRUE(f.writer.push(frame.data(), frame.size()));

  EXPECT_EQ(f.bridge.bridgeStep(), 0u);
  EXPECT_EQ(f.bridge.bridgeState().cmds_decode_errors, 1u);
  EXPECT_EQ(f.bus.cmd_count, 0u);
}

TEST(ShmRingBridge, ringB_countsDispatchFailures) {
  SinkFixture f;
  ASSERT_TRUE(f.setup(uniqueShmPath("dispatch_fail"), 16, 64, 256));

  f.bus.next_post_should_fail = true; // bus rejects next call.
  auto frame = buildAprotoFrame(0xE000u, 0x0100u, {0x01});
  ASSERT_TRUE(f.writer.push(frame.data(), frame.size()));

  EXPECT_EQ(f.bridge.bridgeStep(), 0u);
  EXPECT_EQ(f.bridge.bridgeState().cmds_dispatch_errors, 1u);
  EXPECT_EQ(f.bridge.bridgeState().cmds_received, 0u); // not counted as received on failure
  EXPECT_EQ(f.bus.cmd_count, 1u);                      // attempt was made
}

TEST(ShmRingBridge, ringB_drainsAtMostOneFramePerTick) {
  SinkFixture f;
  ASSERT_TRUE(f.setup(uniqueShmPath("rate_limit"), 16, 64, 256));

  // Push 3 distinct frames into Ring B.
  for (std::uint16_t i = 0; i < 3; ++i) {
    auto frame = buildAprotoFrame(0xE000u, 0x0100u, {static_cast<std::uint8_t>(i)});
    ASSERT_TRUE(f.writer.push(frame.data(), frame.size()));
  }

  // Each bridgeStep should drain exactly one -- RT-bounded by design.
  EXPECT_EQ(f.bridge.bridgeStep(), 0u);
  EXPECT_EQ(f.bridge.bridgeState().cmds_received, 1u);
  EXPECT_EQ(f.bus.cmd_count, 1u);
  EXPECT_EQ(f.bus.last_payload[0], 0u);

  EXPECT_EQ(f.bridge.bridgeStep(), 0u);
  EXPECT_EQ(f.bridge.bridgeState().cmds_received, 2u);
  EXPECT_EQ(f.bus.last_payload[0], 1u);

  EXPECT_EQ(f.bridge.bridgeStep(), 0u);
  EXPECT_EQ(f.bridge.bridgeState().cmds_received, 3u);
  EXPECT_EQ(f.bus.last_payload[0], 2u);

  // Fourth tick: no more frames; cmds_received stays at 3.
  EXPECT_EQ(f.bridge.bridgeStep(), 0u);
  EXPECT_EQ(f.bridge.bridgeState().cmds_received, 3u);
}

/* ----------------------------- end ring-B tests ----------------------------- */

TEST(ShmRingBridge, fullRingDropsAdditionalPushes) {
  // Producer with cap=4 and no consumer draining: after 4 pushes,
  // subsequent pushes should be counted as `pushes_failed_full` but
  // must NOT block or fail the tick.
  ShmRingBridge b;
  ResolverCtx ctx;
  b.setResolver(testResolver, &ctx);

  ShmRingBridgeTunables& t = b.tunables().get();
  fillTunables(t, uniqueShmPath("full"), /*cap=*/4, /*payload_size=*/64, ctx.expectedUid);
  b.setInstanceIndex(0);
  ASSERT_EQ(b.init(), 0u);
  b.onBusReady();
  ASSERT_EQ(b.bridgeState().channel_open, 1u);

  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(b.bridgeStep(), 0u); // never errors; sim never blocks
  }
  EXPECT_EQ(b.bridgeState().frames_published, 4u);
  EXPECT_EQ(b.bridgeState().pushes_failed_full, 4u);
}

/* ----------------------------- Orphan detection ----------------------------- */

/** @test An external unlink of the live path sets the orphan flag; the bridge
 *  keeps ticking non-fatally and mirrors the flag into telemetry. */
TEST(ShmRingBridge, externalUnlinkSetsOrphanFlag) {
  ResolverCtx ctx;
  ShmRingBridge b;
  b.setResolver(testResolver, &ctx);
  const std::string SHM = uniqueShmPath("orphan");
  fillTunables(b.tunables().get(), SHM, /*cap=*/16, /*payload_size=*/64, ctx.expectedUid);
  b.onBusReady();
  ASSERT_EQ(b.bridgeState().channel_open, 1u);

  (void)b.telemetryTick(); // healthy: no flag
  EXPECT_EQ(b.bridgeState().region_orphaned, 0u);

  ::shm_unlink(SHM.c_str()); // external unlink (another Side A, an operator, ...)
  (void)b.telemetryTick();
  EXPECT_EQ(b.bridgeState().region_orphaned, 1u);
  EXPECT_EQ(b.telemetry().region_orphaned, 1u);
  EXPECT_EQ(b.bridgeState().channel_open, 1u); // mapping itself is still live
  EXPECT_EQ(b.bridgeStep(), 0u);               // pushes stay non-fatal
}

/** @test With orphan_reclaim set, a vanished path is reopened and the flag
 *  clears; the reclaimed region carries a valid header. */
TEST(ShmRingBridge, orphanReclaimReopensFreedPath) {
  ResolverCtx ctx;
  ShmRingBridge b;
  b.setResolver(testResolver, &ctx);
  const std::string SHM = uniqueShmPath("reclaim");
  ShmRingBridgeTunables& t = b.tunables().get();
  fillTunables(t, SHM, /*cap=*/16, /*payload_size=*/64, ctx.expectedUid);
  t.orphan_reclaim = 1;
  b.onBusReady();
  ASSERT_EQ(b.bridgeState().channel_open, 1u);

  ::shm_unlink(SHM.c_str());
  (void)b.telemetryTick(); // detect + reclaim in one pass
  EXPECT_EQ(b.bridgeState().region_orphaned, 0u);
  EXPECT_EQ(b.bridgeState().channel_open, 1u);

  // The path exists again with our framework stamp.
  const int FD = ::shm_open(SHM.c_str(), O_RDONLY, 0);
  ASSERT_GE(FD, 0);
  std::uint32_t magic = 0;
  ASSERT_EQ(::pread(FD, &magic, sizeof(magic), 0), static_cast<ssize_t>(sizeof(magic)));
  EXPECT_EQ(magic, BRIDGE_FRAMEWORK_MAGIC);
  ::close(FD);
}

/** @test A path re-owned by another process is flagged but never fought over:
 *  no reclaim, and shutdown does not unlink the foreign region. */
TEST(ShmRingBridge, foreignOwnerFlaggedNotClobbered) {
  ResolverCtx ctx;
  const std::string SHM = uniqueShmPath("foreign");
  struct stat FOREIGN_BEFORE{};

  {
    ShmRingBridge b;
    b.setResolver(testResolver, &ctx);
    ShmRingBridgeTunables& t = b.tunables().get();
    fillTunables(t, SHM, /*cap=*/16, /*payload_size=*/64, ctx.expectedUid);
    t.orphan_reclaim = 1; // reclaim armed -- must still refuse to fight
    b.onBusReady();
    ASSERT_EQ(b.bridgeState().channel_open, 1u);

    // Another process takes the name: unlink + create its own region.
    ::shm_unlink(SHM.c_str());
    const int FOREIGN = ::shm_open(SHM.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    ASSERT_GE(FOREIGN, 0);
    ASSERT_EQ(::ftruncate(FOREIGN, 128), 0);
    ASSERT_EQ(::fstat(FOREIGN, &FOREIGN_BEFORE), 0);
    ::close(FOREIGN);

    (void)b.telemetryTick();
    EXPECT_EQ(b.bridgeState().region_orphaned, 1u); // flagged
  } // bridge destructs here -> closeChannel must NOT unlink the foreign region

  const int CHECK = ::shm_open(SHM.c_str(), O_RDONLY, 0);
  ASSERT_GE(CHECK, 0) << "foreign region was clobbered on shutdown";
  struct stat AFTER{};
  ASSERT_EQ(::fstat(CHECK, &AFTER), 0);
  EXPECT_EQ(AFTER.st_ino, FOREIGN_BEFORE.st_ino); // same object, untouched
  EXPECT_EQ(AFTER.st_size, 128);
  ::close(CHECK);
  ::shm_unlink(SHM.c_str()); // test cleanup
}
