/**
 * @file DataTransformHold_pTest.cpp
 * @brief Matched cost of the DataTransform's hold path against the block owner's write.
 *
 * The fixture is the action demo's shape: a frozen 19-entry registry-like
 * table the resolver scans linearly (as the registry's data lookup does), one
 * block owner rewriting its 64-byte output each tick, and the DataTransform
 * holding what a command applied. Every scenario does the same owner write
 * and the same bookkeeping; what differs is the hold work after it:
 *
 *   owner_write      the owner's rewrite alone (a schedule without a hold task)
 *   hold_inactive    plus a hold tick with nothing held
 *   hold_one_4B      plus a hold tick with the demo's one 4-byte ZERO mask
 *   hold_eight_32B   plus a hold tick with eight 32-byte masks on eight blocks
 *   apply_entry      the APPLY_ENTRY command alone (the commanded write)
 *
 * On a tree whose DataTransform has no hold() the hold scenarios are skipped,
 * so the same source measures the owner write and the command on both sides
 * of a comparison. Each scenario counts its calls and checks the bytes the
 * writes must have left, so the measured work cannot be removed.
 *
 * Usage:
 *   ./DataTransformHold_PTEST
 *   ./DataTransformHold_PTEST --quick
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/support/data_transform/inc/DataTransform.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace ub = vernier::bench;

using system_core::data::DataCategory;
using system_core::support::DataTransform;
using system_core::support::DataTransformOpcode;
using system_core::support::ResolvedData;
using system_core::support::TransformEntry;

namespace {

/* ----------------------------- Hold detection ----------------------------- */

template <typename T, typename = void> struct HasHold : std::false_type {};
template <typename T>
struct HasHold<T, std::void_t<decltype(std::declval<T&>().hold())>> : std::true_type {};

constexpr bool HAS_HOLD = HasHold<DataTransform>::value;

/// One hold tick where the component has one; nothing where it does not.
template <typename T> std::uint8_t holdTick(T& dt) {
  if constexpr (HasHold<T>::value) {
    return dt.hold();
  } else {
    return 0;
  }
}

/// Masks written by hold ticks so far, where the component counts them.
template <typename T> std::uint32_t holdApplies(const T& dt) {
  if constexpr (HasHold<T>::value) {
    return dt.holdApplies();
  } else {
    return 0;
  }
}

/* ----------------------------- Frozen registry ----------------------------- */

constexpr std::size_t REGISTRY_ENTRIES = 19; ///< The action demo's data entry count.
constexpr std::size_t BLOCK_BYTES = 64;
constexpr std::size_t SENSOR_INDEX = 12; ///< Where the owner's block sits in the scan.

struct RegistryEntry {
  std::uint32_t fullUid{0};
  DataCategory category{};
  std::uint8_t* data{nullptr};
  std::size_t size{0};
};

struct Registry {
  std::array<std::array<std::uint8_t, BLOCK_BYTES>, REGISTRY_ENTRIES> blocks{};
  std::array<RegistryEntry, REGISTRY_ENTRIES> entries{};

  Registry() {
    for (std::size_t i = 0; i < REGISTRY_ENTRIES; ++i) {
      blocks[i].fill(0xAA);
      entries[i] = {uidOf(i), DataCategory::OUTPUT, blocks[i].data(), BLOCK_BYTES};
    }
  }
  static constexpr std::uint32_t uidOf(std::size_t i) {
    return 0x00C000U + static_cast<std::uint32_t>(i) * 0x100U;
  }
};

/// The registry's lookup shape: a linear scan on (fullUid, category).
ResolvedData scanResolver(void* ctx, std::uint32_t fullUid, DataCategory category) noexcept {
  auto* r = static_cast<Registry*>(ctx);
  for (const auto& e : r->entries) {
    if (e.fullUid == fullUid && e.category == category) {
      return {e.data, e.size};
    }
  }
  return {};
}

/* ----------------------------- Owner ----------------------------- */

/// The block owner's step: a fresh 64-byte value every tick, the tick in its first bytes.
struct Owner {
  std::uint8_t* block{nullptr};
  std::uint32_t tick{0};
  std::array<std::uint8_t, BLOCK_BYTES> fresh{};

  void write() noexcept {
    ++tick;
    std::memcpy(fresh.data(), &tick, sizeof(tick));
    std::memcpy(block, fresh.data(), BLOCK_BYTES);
  }
};

/* ----------------------------- Commands ----------------------------- */

std::uint8_t command(DataTransform& dt, DataTransformOpcode op, std::uint8_t index) {
  std::vector<std::uint8_t> response;
  std::array<std::uint8_t, 1> payload = {index};
  return dt.handleCommand(static_cast<std::uint16_t>(op), {payload.data(), payload.size()},
                          response);
}

/// Arm entry `index` on registry entry `target` with a ZERO mask of `len` bytes and apply it.
void armApplyZero(DataTransform& dt, std::uint8_t index, std::size_t target, std::uint8_t len) {
  dt.entry(index)->target = {Registry::uidOf(target), DataCategory::OUTPUT, 0, len};
  ASSERT_EQ(command(dt, DataTransformOpcode::ARM_ENTRY, index), 0u);
  std::vector<std::uint8_t> response;
  std::array<std::uint8_t, 4> payload = {index, 0, 0, len};
  ASSERT_EQ(dt.handleCommand(static_cast<std::uint16_t>(DataTransformOpcode::PUSH_ZERO_MASK),
                             {payload.data(), payload.size()}, response),
            0u);
  ASSERT_EQ(command(dt, DataTransformOpcode::APPLY_ENTRY, index), 0u);
}

/// The first `len` bytes of a block are zero.
bool zeroed(const std::uint8_t* block, std::size_t len) {
  for (std::size_t i = 0; i < len; ++i) {
    if (block[i] != 0) {
      return false;
    }
  }
  return true;
}

void report(const char* label, double callsPerSecond, double medianUs, std::uint64_t calls) {
  std::printf("%-16s %12.0f calls/s %10.1f ns/call %10llu calls\n", label, callsPerSecond,
              medianUs * 1000.0, static_cast<unsigned long long>(calls));
}

} // namespace

/* ----------------------------- Storage ----------------------------- */

/** @brief The component's static footprint on this tree. */
PERF_TEST(DataTransformHold, Storage) {
  std::printf("\n=== DataTransform storage ===\n");
  std::printf("  sizeof(DataTransform)  %zu bytes\n", sizeof(DataTransform));
  std::printf("  sizeof(TransformEntry) %zu bytes\n", sizeof(TransformEntry));
  std::printf("  hold path present      %s\n", HAS_HOLD ? "yes" : "no");
}

/* ----------------------------- Owner write alone ----------------------------- */

/** @brief The per-tick work of a schedule without a hold task. */
PERF_TEST(DataTransformHold, OwnerWriteAlone) {
  UB_PERF_GUARD(perf);
  Registry reg;
  Owner owner{reg.blocks[SENSOR_INDEX].data()};
  std::uint64_t calls = 0;

  std::printf("\n=== Owner write alone ===\n");
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      owner.write();
    }
  });
  owner.tick = 0;
  auto result = perf.throughputLoop(
      [&] {
        owner.write();
        ++calls;
      },
      "owner_write");
  report("owner_write", result.callsPerSecond, result.stats.median, calls);

  std::uint32_t last = 0;
  std::memcpy(&last, owner.block, sizeof(last));
  EXPECT_EQ(last, owner.tick) << "the last owner write is in the block";
  EXPECT_EQ(owner.tick, static_cast<std::uint32_t>(calls));
}

/* ----------------------------- Owner write plus hold ----------------------------- */

/** @brief A hold tick with nothing held, after each owner write. */
PERF_TEST(DataTransformHold, OwnerWritePlusInactiveHold) {
  if (!HAS_HOLD) {
    GTEST_SKIP() << "no hold path on this tree";
  }
  UB_PERF_GUARD(perf);
  Registry reg;
  Owner owner{reg.blocks[SENSOR_INDEX].data()};
  DataTransform dt;
  dt.setResolver(scanResolver, &reg);
  ASSERT_EQ(dt.init(), 0u);
  std::uint64_t calls = 0;

  std::printf("\n=== Owner write + inactive hold ===\n");
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      owner.write();
      (void)holdTick(dt);
    }
  });
  owner.tick = 0;
  auto result = perf.throughputLoop(
      [&] {
        owner.write();
        (void)holdTick(dt);
        ++calls;
      },
      "hold_inactive");
  report("hold_inactive", result.callsPerSecond, result.stats.median, calls);

  std::uint32_t last = 0;
  std::memcpy(&last, owner.block, sizeof(last));
  EXPECT_EQ(last, owner.tick) << "nothing held: the owner's write stands";
  EXPECT_EQ(holdApplies(dt), 0u);
}

/** @brief The demo's one 4-byte ZERO mask held after each owner write. */
PERF_TEST(DataTransformHold, OwnerWritePlusOneMask) {
  if (!HAS_HOLD) {
    GTEST_SKIP() << "no hold path on this tree";
  }
  UB_PERF_GUARD(perf);
  Registry reg;
  Owner owner{reg.blocks[SENSOR_INDEX].data()};
  DataTransform dt;
  dt.setResolver(scanResolver, &reg);
  ASSERT_EQ(dt.init(), 0u);
  armApplyZero(dt, 0, SENSOR_INDEX, 4);
  std::uint64_t calls = 0;

  std::printf("\n=== Owner write + one 4-byte mask held ===\n");
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      owner.write();
      (void)holdTick(dt);
    }
  });
  const std::uint32_t HELD_BEFORE = holdApplies(dt);
  owner.tick = 0;
  auto result = perf.throughputLoop(
      [&] {
        owner.write();
        (void)holdTick(dt);
        ++calls;
      },
      "hold_one_4B");
  report("hold_one_4B", result.callsPerSecond, result.stats.median, calls);

  EXPECT_TRUE(zeroed(owner.block, 4)) << "the held mask covers the owner's last write";
  EXPECT_EQ(owner.block[4], owner.fresh[4]) << "bytes outside the mask are the owner's";
  EXPECT_EQ(holdApplies(dt) - HELD_BEFORE, static_cast<std::uint32_t>(calls))
      << "every tick wrote the one held mask";
}

/** @brief Eight 32-byte masks on eight blocks held after each owner write. */
PERF_TEST(DataTransformHold, OwnerWritePlusEightMasks) {
  if (!HAS_HOLD) {
    GTEST_SKIP() << "no hold path on this tree";
  }
  UB_PERF_GUARD(perf);
  Registry reg;
  Owner owner{reg.blocks[SENSOR_INDEX].data()};
  DataTransform dt;
  dt.setResolver(scanResolver, &reg);
  ASSERT_EQ(dt.init(), 0u);
  const std::array<std::size_t, 8> TARGETS = {1, 4, 7, 9, SENSOR_INDEX, 14, 16, 18};
  for (std::uint8_t i = 0; i < 8; ++i) {
    armApplyZero(dt, i, TARGETS[i], 32);
  }
  std::uint64_t calls = 0;

  std::printf("\n=== Owner write + eight 32-byte masks held ===\n");
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      owner.write();
      (void)holdTick(dt);
    }
  });
  const std::uint32_t HELD_BEFORE = holdApplies(dt);
  owner.tick = 0;
  auto result = perf.throughputLoop(
      [&] {
        owner.write();
        (void)holdTick(dt);
        ++calls;
      },
      "hold_eight_32B");
  report("hold_eight_32B", result.callsPerSecond, result.stats.median, calls);

  for (const std::size_t T : TARGETS) {
    EXPECT_TRUE(zeroed(reg.blocks[T].data(), 32)) << "block " << T;
  }
  EXPECT_EQ(owner.block[32], owner.fresh[32]) << "bytes outside the mask are the owner's";
  EXPECT_EQ(holdApplies(dt) - HELD_BEFORE, static_cast<std::uint32_t>(calls) * 8U)
      << "every tick wrote all eight held masks";
}

/* ----------------------------- The command ----------------------------- */

/** @brief APPLY_ENTRY alone: the commanded write of the demo's 4-byte mask. */
PERF_TEST(DataTransformHold, ApplyEntryCommand) {
  UB_PERF_GUARD(perf);
  Registry reg;
  DataTransform dt;
  dt.setResolver(scanResolver, &reg);
  ASSERT_EQ(dt.init(), 0u);
  armApplyZero(dt, 0, SENSOR_INDEX, 4);
  std::uint64_t calls = 0;

  std::printf("\n=== APPLY_ENTRY alone ===\n");
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)command(dt, DataTransformOpcode::APPLY_ENTRY, 0);
    }
  });
  const std::uint32_t APPLIED_BEFORE = dt.stats().masksApplied;
  auto result = perf.throughputLoop(
      [&] {
        (void)command(dt, DataTransformOpcode::APPLY_ENTRY, 0);
        ++calls;
      },
      "apply_entry");
  report("apply_entry", result.callsPerSecond, result.stats.median, calls);

  EXPECT_TRUE(zeroed(reg.blocks[SENSOR_INDEX].data(), 4));
  EXPECT_EQ(dt.stats().masksApplied - APPLIED_BEFORE, static_cast<std::uint32_t>(calls));
}

PERF_MAIN()
