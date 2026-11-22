/**
 * @file ShmRingBridge_pTest.cpp
 * @brief Throughput benchmark for the bridge per-tick push path.
 *
 * bridgeStep is the RT path: memcpy the resolved source into the next ring slot,
 * release-store the producer cursor, sem_post. A minimal in-harness consumer
 * maps the Ring A prelude and advances the consumer cursor each tick, keeping the
 * ring drained so the measured path is the push -- not the FULL-refusal path.
 *
 * The Throughput case resolves its source through a constant-time resolver. The
 * RegistryResolver case resolves it the way the applications do, through
 * ApexRegistry::getData on a frozen registry of an application's size with the
 * source registered last, so the lookup scans every data entry.
 */

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/components/registry/apex/inc/ApexRegistry.hpp"
#include "src/system/core/support/shm_ring_bridge/inc/ShmRingBridge.hpp"

namespace {

using system_core::data::DataCategory;
using system_core::support::ResolvedSource;
using system_core::support::ShmRingBridge;
using system_core::support::ShmRingBridgeTunables;

struct ResolverCtx {
  std::array<std::uint8_t, 64> block{};
  std::uint32_t uid = 0xDE00u;
};

ResolvedSource benchResolver(void* ctx, std::uint32_t uid, DataCategory cat) noexcept {
  auto* r = static_cast<ResolverCtx*>(ctx);
  if (uid != r->uid || cat != DataCategory::OUTPUT) {
    return {};
  }
  return {r->block.data(), r->block.size()};
}

// Maps the Ring A prelude (producer cursor at +64, consumer cursor at +128) and
// advances the consumer cursor to the producer cursor, keeping the ring drained.
struct Drainer {
  int fd = -1;
  void* map = nullptr;
  std::atomic<std::uint64_t>* head = nullptr;
  std::atomic<std::uint64_t>* tail = nullptr;

  bool open(const std::string& path) noexcept {
    fd = ::shm_open(path.c_str(), O_RDWR, 0600);
    if (fd < 0) {
      return false;
    }
    map = ::mmap(nullptr, 192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
      return false;
    }
    auto* base = static_cast<std::uint8_t*>(map);
    head = reinterpret_cast<std::atomic<std::uint64_t>*>(base + 64);
    tail = reinterpret_cast<std::atomic<std::uint64_t>*>(base + 128);
    return true;
  }

  void drain() noexcept {
    tail->store(head->load(std::memory_order_acquire), std::memory_order_release);
  }

  ~Drainer() {
    if (map != nullptr && map != MAP_FAILED) {
      ::munmap(map, 192);
    }
    if (fd >= 0) {
      ::close(fd);
    }
  }
};

} // namespace

PERF_TEST(BridgeStepPush, Throughput) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  ShmRingBridge b;
  b.setResolver(benchResolver, &ctx);

  const std::string shm = std::string("/shm_ring_bridge_bench_") + std::to_string(::getpid());
  ShmRingBridgeTunables& t = b.tunables().get();
  t.app_magic = 0x54455354u; // "TEST"
  t.app_version = 1;
  t.capacity = 1024;
  t.payload_size = 64;
  t.source_uid = ctx.uid;
  t.source_category = static_cast<std::uint8_t>(DataCategory::OUTPUT);
  t.source_byte_len = 64;
  std::snprintf(t.shm_path, sizeof(t.shm_path), "%s", shm.c_str());
  b.onBusReady();

  Drainer drainer;
  const bool drained = drainer.open(shm);

  volatile std::uint8_t sink = 0;
  auto result = perf.throughputLoop(
      [&] {
        sink = static_cast<std::uint8_t>(sink + b.bridgeStep());
        if (drained) {
          drainer.drain();
        }
      },
      "bridge_step_push");

  std::printf("\n[ShmRingBridge] bridgeStep: %.0f ticks/s (%.4f us/tick), channel_open=%u\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond, b.bridgeState().channel_open);
}

/* ----------------------------- Registry resolver ----------------------------- */

namespace {

// The resolver the applications wire: (fullUid, category) looked up in the
// registry, a linear scan of its data entries.
ResolvedSource registryResolver(void* ctx, std::uint32_t uid, DataCategory cat) noexcept {
  auto* registry = static_cast<system_core::registry::ApexRegistry*>(ctx);
  auto* entry = registry->getData(uid, cat);
  if (entry == nullptr || !entry->isValid()) {
    return {};
  }
  return {reinterpret_cast<const std::uint8_t*>(entry->dataPtr), entry->size};
}

} // namespace

// The push path with the production resolver. The registry has the size of the
// ops demo's (12 components, 22 data entries) and the bridge's source is the last
// data entry registered, the far end of the scan. Every step must publish: the
// counts printed at the end say whether one was refused or found no source.
PERF_TEST(BridgeStepPush, RegistryResolver) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t COMPONENTS = 12;
  constexpr std::size_t DATA_ENTRIES = 22;
  constexpr std::uint32_t SOURCE_UID = 0xDE00u;
  using system_core::registry::Status;

  system_core::registry::ApexRegistry registry;
  std::array<std::array<std::uint8_t, 64>, DATA_ENTRIES> blocks{};
  bool registered = true;
  for (std::size_t c = 0; c + 1 < COMPONENTS; ++c) {
    registered =
        registered && registry.registerComponent(static_cast<std::uint32_t>(0x6400u + c * 0x100u),
                                                 "Filler") == Status::SUCCESS;
  }
  registered = registered && registry.registerComponent(SOURCE_UID, "Source") == Status::SUCCESS;
  // Two blocks per filler component (tunables and outputs, the mix the demo
  // registers), then the source's output block last.
  for (std::size_t d = 0; d + 1 < DATA_ENTRIES; ++d) {
    const auto UID = static_cast<std::uint32_t>(0x6400u + (d % (COMPONENTS - 1)) * 0x100u);
    const DataCategory CAT =
        (d < COMPONENTS - 1) ? DataCategory::TUNABLE_PARAM : DataCategory::OUTPUT;
    registered =
        registered && registry.registerData(UID, CAT, (d < COMPONENTS - 1) ? "tunables" : "output",
                                            blocks[d].data(), blocks[d].size()) == Status::SUCCESS;
  }
  registered =
      registered && registry.registerData(SOURCE_UID, DataCategory::OUTPUT, "output",
                                          blocks[DATA_ENTRIES - 1].data(),
                                          blocks[DATA_ENTRIES - 1].size()) == Status::SUCCESS;
  registered = registered && registry.freeze() == Status::SUCCESS;

  ShmRingBridge b;
  b.setResolver(registryResolver, &registry);

  const std::string shm = std::string("/shm_ring_bridge_bench_reg_") + std::to_string(::getpid());
  ShmRingBridgeTunables& t = b.tunables().get();
  t.app_magic = 0x54455354u; // "TEST"
  t.app_version = 1;
  t.capacity = 1024;
  t.payload_size = 64;
  t.source_uid = SOURCE_UID;
  t.source_category = static_cast<std::uint8_t>(DataCategory::OUTPUT);
  t.source_byte_len = 64;
  std::snprintf(t.shm_path, sizeof(t.shm_path), "%s", shm.c_str());
  b.onBusReady();

  Drainer drainer;
  const bool drained = drainer.open(shm);

  volatile std::uint8_t sink = 0;
  auto result = perf.throughputLoop(
      [&] {
        sink = static_cast<std::uint8_t>(sink + b.bridgeStep());
        if (drained) {
          drainer.drain();
        }
      },
      "bridge_step_push_registry");

  const auto& STATE = b.bridgeState();
  std::printf("\n[ShmRingBridge] bridgeStep, registry resolver (%zu components, %zu data entries, "
              "source at entry %zu): %.0f ticks/s (%.4f us/tick); registered=%d channel_open=%u "
              "source_resolved=%u steps=%llu published=%llu refused_full=%llu\n",
              COMPONENTS, DATA_ENTRIES, DATA_ENTRIES, result.callsPerSecond,
              1.0e6 / result.callsPerSecond, registered ? 1 : 0, STATE.channel_open,
              STATE.source_resolved, static_cast<unsigned long long>(STATE.tick_count),
              static_cast<unsigned long long>(STATE.frames_published),
              static_cast<unsigned long long>(STATE.pushes_failed_full));
}

/* ----------------------------- Payload scaling ----------------------------- */

namespace {

// Resolver over an arbitrary-size source block (the fixed 64-byte ResolverCtx
// stays for the canonical case above).
struct SizedResolverCtx {
  std::vector<std::uint8_t> block;
  std::uint32_t uid = 0xDE00u;
};

ResolvedSource sizedResolver(void* ctx, std::uint32_t uid, DataCategory cat) noexcept {
  auto* r = static_cast<SizedResolverCtx*>(ctx);
  if (uid != r->uid || cat != DataCategory::OUTPUT) {
    return {};
  }
  return {r->block.data(), r->block.size()};
}

} // namespace

// Characterizes the memcpy-bound region of the push path across the payload
// sizes the demo apps use (48 B lidar_box frame, 256 B vehicle frames) and
// beyond (KB-scale future sensors). Excluded from the profiler passes by the
// harness filter (-*PayloadScaling*); runs in the normal --quick sweep.
PERF_TEST(BridgeStepPush, PayloadScaling) {
  UB_PERF_GUARD(perf);

  constexpr std::uint32_t kSizes[] = {48u, 256u, 1024u, 4096u, 65536u};
  std::printf("\n[ShmRingBridge] payload scaling (capacity 64, drained):\n");

  for (const std::uint32_t SIZE : kSizes) {
    SizedResolverCtx ctx;
    ctx.block.assign(SIZE, 0xA5u);

    ShmRingBridge b;
    b.setResolver(sizedResolver, &ctx);

    const std::string shm = std::string("/shm_ring_bridge_scale_") + std::to_string(SIZE) + "_" +
                            std::to_string(::getpid());
    ShmRingBridgeTunables& t = b.tunables().get();
    t.app_magic = 0x54455354u; // "TEST"
    t.app_version = 1;
    t.capacity = 64;
    t.payload_size = SIZE;
    t.source_uid = ctx.uid;
    t.source_category = static_cast<std::uint8_t>(DataCategory::OUTPUT);
    t.source_byte_len = 0; // whole block
    std::snprintf(t.shm_path, sizeof(t.shm_path), "%s", shm.c_str());
    b.onBusReady();

    Drainer drainer;
    const bool drained = drainer.open(shm);

    volatile std::uint8_t sink = 0;
    auto result = perf.throughputLoop(
        [&] {
          sink = static_cast<std::uint8_t>(sink + b.bridgeStep());
          if (drained) {
            drainer.drain();
          }
        },
        (std::string("bridge_step_") + std::to_string(SIZE) + "B").c_str());

    const double US = 1.0e6 / result.callsPerSecond;
    std::printf("  %6u B: %8.0f ticks/s  %.4f us/tick  %7.1f MB/s\n", SIZE, result.callsPerSecond,
                US, (result.callsPerSecond * SIZE) / 1.0e6);
  }
}

/* ----------------------------- Orphan-probe cost ----------------------------- */

// The 1 Hz telemetry task now probes the shm path (shm_open + fstat + close)
// to detect an external unlink. Measures the per-call probe cost -- non-RT
// path, but recorded so the telemetry budget is known.
PERF_TEST(TelemetryOrphanProbe, Cost) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  ShmRingBridge b;
  b.setResolver(benchResolver, &ctx);

  const std::string shm = std::string("/shm_ring_bridge_probe_") + std::to_string(::getpid());
  ShmRingBridgeTunables& t = b.tunables().get();
  t.app_magic = 0x54455354u;
  t.app_version = 1;
  t.capacity = 1024;
  t.payload_size = 64;
  t.source_uid = ctx.uid;
  t.source_category = static_cast<std::uint8_t>(DataCategory::OUTPUT);
  t.source_byte_len = 64;
  std::snprintf(t.shm_path, sizeof(t.shm_path), "%s", shm.c_str());
  b.onBusReady();

  volatile std::uint8_t sink = 0;
  auto result = perf.throughputLoop(
      [&] { sink = static_cast<std::uint8_t>(sink + b.telemetryTick()); }, "telemetry_probe");

  std::printf("\n[ShmRingBridge] telemetryTick (incl. orphan probe): %.0f calls/s (%.3f us/call)\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond);
}

PERF_MAIN()
