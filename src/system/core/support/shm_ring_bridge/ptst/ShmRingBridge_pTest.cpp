/**
 * @file ShmRingBridge_pTest.cpp
 * @brief Throughput benchmark for the bridge per-tick push path.
 *
 * bridgeStep is the RT path: memcpy the resolved source into the next ring slot,
 * release-store the producer cursor, sem_post. A minimal in-harness consumer
 * maps the Ring A prelude and advances the consumer cursor each tick, keeping the
 * ring drained so the measured path is the push -- not the FULL-refusal path.
 */

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "src/bench/inc/Perf.hpp"
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

PERF_MAIN()
