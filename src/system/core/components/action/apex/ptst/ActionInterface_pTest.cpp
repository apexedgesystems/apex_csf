/**
 * @file ActionInterface_pTest.cpp
 * @brief Performance tests for the ActionInterface processCycle pipeline.
 *
 * Measures:
 *  - processCycle with empty tables (baseline overhead)
 *  - processCycle with armed watchpoints (evaluation cost, fire vs steady-state)
 *  - processCycle with running sequences (tick cost)
 *  - processCycle with queued COMMAND actions (processing + re-queue cost)
 *  - Full pipeline cascades (watchpoints -> events -> sequences -> actions)
 *  - Catalog lookup and WatchFunction per-evaluation costs
 *
 * Usage:
 *   ./ActionInterface_PTEST --csv results.csv
 *   ./ActionInterface_PTEST --quick
 *   ./ActionInterface_PTEST --profile gperf --cycles 100000
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/components/action/apex/inc/ActionInterface.hpp"
#include "src/system/core/components/action/apex/inc/ResourceCatalog.hpp"
#include "src/system/core/components/action/apex/inc/SequenceCatalog.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ub = vernier::bench;
using namespace system_core::data;

/* ----------------------------- Test Helpers ----------------------------- */

namespace {

/// Backing data block for resolver: 256 bytes, shared across all targets.
struct ResolverCtx {
  std::array<std::uint8_t, 256> block{};
};

/// Resolver delegate implementation: returns the shared block for any target.
ResolvedData resolverFn(void* ctx, std::uint32_t /*fullUid*/, DataCategory /*category*/) noexcept {
  auto* rctx = static_cast<ResolverCtx*>(ctx);
  return {rctx->block.data(), rctx->block.size()};
}

/// Command delegate: no-op sink for command routing.
void commandSink(void* /*ctx*/, std::uint32_t /*fullUid*/, std::uint16_t /*opcode*/,
                 const std::uint8_t* /*payload*/, std::uint8_t /*len*/) noexcept {
  // intentionally empty
}

/// Configure a watchpoint to compare a float at offset 0 against a threshold.
void armWatchpoint(DataWatchpoint& wp, std::uint16_t watchpointId, std::uint16_t eventId,
                   WatchPredicate pred, float threshold) noexcept {
  wp.watchpointId = watchpointId;
  wp.target = {0x007800, DataCategory::OUTPUT, 0, 4};
  wp.predicate = pred;
  wp.dataType = WatchDataType::FLOAT32;
  wp.eventId = eventId;
  std::memcpy(wp.threshold.data(), &threshold, sizeof(float));
  wp.armed = true;
}

/// Set a float value in the resolver backing store at offset 0.
void setFloat(ResolverCtx& ctx, float value) noexcept {
  std::memcpy(ctx.block.data(), &value, sizeof(float));
}

/// Configure a simple COMMAND action for benchmarking.
void armCommandAction(DataAction& action) noexcept {
  action.target.fullUid = 0x007800;
  action.actionType = ActionType::COMMAND;
  action.trigger = ActionTrigger::IMMEDIATE;
  action.status = ActionStatus::PENDING;
  action.commandOpcode = 0x0001;
  action.commandPayloadLen = 0;
}

/// Build a fresh ActionInterface with resolver + command handler wired up.
ActionInterface makeEngine(ResolverCtx& ctx) noexcept {
  ActionInterface iface{};
  iface.resolver = {resolverFn, &ctx};
  iface.commandHandler = {commandSink, nullptr};
  return iface;
}

} // namespace

/* ----------------------------- Baseline ----------------------------- */

/** @brief Baseline: processCycle with all tables empty. */
PERF_TEST(ActionInterfacePerf, EmptyBaseline) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  ActionInterface iface = makeEngine(ctx);

  std::printf("\n=== ActionInterface Empty Baseline ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      processCycle(iface, static_cast<std::uint32_t>(i));
    }
  });

  auto result =
      perf.throughputLoop([&] { processCycle(iface, iface.stats.totalCycles); }, "empty_cycle");

  std::printf("Empty cycle: %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
}

/* ----------------------------- Watchpoint Evaluation ----------------------------- */

/** @brief processCycle with armed watchpoints that do not fire (steady-state). */
PERF_TEST(ActionInterfacePerf, WatchpointsSteadyState) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  setFloat(ctx, 50.0F); // Below all thresholds

  ActionInterface iface = makeEngine(ctx);

  // Arm all 8 watchpoints with GT thresholds that won't fire
  for (std::size_t i = 0; i < WATCHPOINT_TABLE_SIZE; ++i) {
    armWatchpoint(iface.watchpoints[i], static_cast<std::uint16_t>(i + 1),
                  static_cast<std::uint16_t>(i + 1), WatchPredicate::GT,
                  100.0F + static_cast<float>(i) * 10.0F);
  }

  std::printf("\n=== ActionInterface Watchpoints (steady-state, 8 armed) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      processCycle(iface, iface.stats.totalCycles);
    }
  });

  auto result =
      perf.throughputLoop([&] { processCycle(iface, iface.stats.totalCycles); }, "wp_steady_8");

  std::printf("8 watchpoints (no fire): %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
}

/** @brief processCycle with armed watchpoints that fire every cycle. */
PERF_TEST(ActionInterfacePerf, WatchpointsFiring) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  setFloat(ctx, 200.0F); // Above all thresholds

  ActionInterface iface = makeEngine(ctx);

  // Arm 4 watchpoints that will fire every cycle (edge-detect resets needed)
  for (std::size_t i = 0; i < 4; ++i) {
    armWatchpoint(iface.watchpoints[i], static_cast<std::uint16_t>(i + 1),
                  static_cast<std::uint16_t>(i + 1), WatchPredicate::GT, 100.0F);
  }

  // Prime edge detection: first cycle establishes "last" state
  processCycle(iface, 0);

  std::printf("\n=== ActionInterface Watchpoints (firing, 4 armed) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      processCycle(iface, iface.stats.totalCycles);
    }
  });

  auto result =
      perf.throughputLoop([&] { processCycle(iface, iface.stats.totalCycles); }, "wp_firing_4");

  std::printf("4 watchpoints (firing): %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
}

/* ----------------------------- Sequence Tick ----------------------------- */

/** @brief processCycle with running sequences (WAITING state, counting down). */
PERF_TEST(ActionInterfacePerf, SequencesTicking) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  ActionInterface iface = makeEngine(ctx);

  // Set up 4 sequences in WAITING state with long delays
  for (std::size_t i = 0; i < SEQUENCE_TABLE_SIZE; ++i) {
    auto& seq = iface.sequences[i];
    seq.type = SequenceType::RTS;
    seq.eventId = static_cast<std::uint16_t>(100 + i); // won't match any event
    seq.armed = true;
    seq.stepCount = 2;
    seq.steps[0].delayCycles = 0xFFFFFFFF; // Very long delay
    armCommandAction(seq.steps[0].action);
    seq.steps[1].delayCycles = 0xFFFFFFFF;
    armCommandAction(seq.steps[1].action);

    // Start the sequence so it's in WAITING state
    startSequence(seq);
  }

  std::printf("\n=== ActionInterface Sequences (4 waiting) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      processCycle(iface, iface.stats.totalCycles);
    }
  });

  auto result =
      perf.throughputLoop([&] { processCycle(iface, iface.stats.totalCycles); }, "seq_waiting_4");

  std::printf("4 sequences (waiting): %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
}

/* ----------------------------- Action Processing ----------------------------- */

/** @brief processCycle with queued DATA_WRITE actions (one-shot, re-queued). */
PERF_TEST(ActionInterfacePerf, ActionProcessing) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  ActionInterface iface = makeEngine(ctx);

  std::printf("\n=== ActionInterface Action Processing ===\n");

  // Queue 8 one-shot COMMAND actions and measure processing + re-queue
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      iface.actionCount = 0;
      for (std::uint8_t a = 0; a < 8; ++a) {
        armCommandAction(iface.actions[a]);
      }
      iface.actionCount = 8;
      processCycle(iface, iface.stats.totalCycles);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        // Re-queue 8 actions before each cycle
        iface.actionCount = 0;
        for (std::uint8_t a = 0; a < 8; ++a) {
          armCommandAction(iface.actions[a]);
        }
        iface.actionCount = 8;
        processCycle(iface, iface.stats.totalCycles);
      },
      "actions_8");

  std::printf("8 COMMAND actions: %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
}

/* ----------------------------- Full Pipeline ----------------------------- */

/** @brief Full pipeline: watchpoints + sequences + actions all active. */
PERF_TEST(ActionInterfacePerf, FullPipeline) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  setFloat(ctx, 50.0F); // Below thresholds (steady-state)

  ActionInterface iface = makeEngine(ctx);

  // 8 armed watchpoints (not firing)
  for (std::size_t i = 0; i < WATCHPOINT_TABLE_SIZE; ++i) {
    armWatchpoint(iface.watchpoints[i], static_cast<std::uint16_t>(i + 1),
                  static_cast<std::uint16_t>(i + 1), WatchPredicate::GT, 100.0F);
  }

  // 2 armed groups (ref by watchpoint ID)
  for (std::size_t i = 0; i < 2; ++i) {
    auto& g = iface.groups[i];
    g.groupId = static_cast<std::uint16_t>(i + 1);
    g.armed = true;
    g.eventId = static_cast<std::uint16_t>(50 + i);
    g.count = 2;
    g.refs[0] = static_cast<std::uint16_t>(i * 2 + 1);
    g.refs[1] = static_cast<std::uint16_t>(i * 2 + 2);
  }

  // 4 sequences in WAITING state
  for (std::size_t i = 0; i < SEQUENCE_TABLE_SIZE; ++i) {
    auto& seq = iface.sequences[i];
    seq.type = SequenceType::RTS;
    seq.eventId = static_cast<std::uint16_t>(100 + i);
    seq.armed = true;
    seq.stepCount = 2;
    seq.steps[0].delayCycles = 0xFFFFFFFF;
    armCommandAction(seq.steps[0].action);
    seq.steps[1].delayCycles = 0xFFFFFFFF;
    armCommandAction(seq.steps[1].action);
    startSequence(seq);
  }

  // 4 queued COMMAND actions (re-armed each cycle for sustained load)
  for (std::uint8_t a = 0; a < 4; ++a) {
    auto& action = iface.actions[a];
    action.target.fullUid = 0x007800;
    action.actionType = ActionType::COMMAND;
    action.trigger = ActionTrigger::IMMEDIATE;
    action.status = ActionStatus::PENDING;
    action.commandOpcode = 0x0001;
    action.commandPayloadLen = 0;
  }
  iface.actionCount = 4;

  rebuildWatchpointIndex(iface);
  rebuildEventIndex(iface);
  processCycle(iface, 0); // Prime

  std::printf("\n=== ActionInterface Full Pipeline ===\n");
  std::printf("  8 watchpoints + 2 groups + 4 sequences + 4 actions\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      processCycle(iface, iface.stats.totalCycles);
    }
  });

  auto result = perf.throughputLoop(
      [&] { processCycle(iface, iface.stats.totalCycles); }, "full_pipeline",
      ub::MemoryProfile{.bytesRead = sizeof(ActionInterface),
                        .bytesWritten = sizeof(EngineStats) + 4 * sizeof(DataAction),
                        .bytesAllocated = 0});

  std::printf("Full pipeline: %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
  std::printf(
      "  Bandwidth: %.1f MB/s\n",
      ub::MemoryProfile{sizeof(ActionInterface), sizeof(EngineStats) + 4 * sizeof(DataAction), 0}
          .bandwidthMBs(result.stats.median));
}

/* ----------------------------- Individual Stages ----------------------------- */

/** @brief Isolate evaluateWatchpoints cost separately from full cycle. */
PERF_TEST(ActionInterfacePerf, IsolatedEvaluateWatchpoints) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  setFloat(ctx, 50.0F);

  ActionInterface iface = makeEngine(ctx);

  for (std::size_t i = 0; i < WATCHPOINT_TABLE_SIZE; ++i) {
    armWatchpoint(iface.watchpoints[i], static_cast<std::uint16_t>(i + 1),
                  static_cast<std::uint16_t>(i + 1), WatchPredicate::GT, 100.0F);
  }

  // Prime edge detection
  constexpr std::size_t MAX_EV = WATCHPOINT_TABLE_SIZE + WATCHPOINT_GROUP_TABLE_SIZE;
  std::array<std::uint16_t, MAX_EV> events{};
  (void)evaluateWatchpoints(iface, events.data(), MAX_EV);

  std::printf("\n=== Isolated: evaluateWatchpoints (8 armed) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)evaluateWatchpoints(iface, events.data(), MAX_EV);
    }
  });

  auto result = perf.throughputLoop(
      [&] { (void)evaluateWatchpoints(iface, events.data(), MAX_EV); }, "eval_wp_8");

  std::printf("evaluateWatchpoints: %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
}

/** @brief Isolate tickSequences cost separately from full cycle. */
PERF_TEST(ActionInterfacePerf, IsolatedTickSequences) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  ActionInterface iface = makeEngine(ctx);

  for (std::size_t i = 0; i < SEQUENCE_TABLE_SIZE; ++i) {
    auto& seq = iface.sequences[i];
    seq.type = SequenceType::RTS;
    seq.eventId = static_cast<std::uint16_t>(100 + i);
    seq.armed = true;
    seq.stepCount = 2;
    seq.steps[0].delayCycles = 0xFFFFFFFF;
    armCommandAction(seq.steps[0].action);
    seq.steps[1].delayCycles = 0xFFFFFFFF;
    armCommandAction(seq.steps[1].action);
    startSequence(seq);
  }

  std::printf("\n=== Isolated: tickSequences (4 waiting) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      tickSequences(iface);
    }
  });

  auto result = perf.throughputLoop([&] { tickSequences(iface); }, "tick_seq_4");

  std::printf("tickSequences: %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
}

/** @brief Isolate processActions cost separately from full cycle. */
PERF_TEST(ActionInterfacePerf, IsolatedProcessActions) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  ActionInterface iface = makeEngine(ctx);

  // Set up 8 COMMAND actions (re-queued each iteration for sustained load)
  auto rearm = [&] {
    for (std::uint8_t a = 0; a < 8; ++a) {
      auto& action = iface.actions[a];
      action.target.fullUid = 0x007800;
      action.actionType = ActionType::COMMAND;
      action.trigger = ActionTrigger::IMMEDIATE;
      action.status = ActionStatus::PENDING;
      action.commandOpcode = 0x0001;
      action.commandPayloadLen = 0;
    }
    iface.actionCount = 8;
  };
  rearm();

  std::printf("\n=== Isolated: processActions (8 COMMAND) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      rearm();
      processActions(iface, static_cast<std::uint32_t>(i));
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        rearm();
        processActions(iface, iface.stats.totalCycles);
      },
      "proc_actions_8");

  std::printf("processActions: %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
}

/* ----------------------------- Scale Tests ----------------------------- */

/** @brief Full pipeline at scale: 32 WPs + 16 groups + 16 sequences + 8 actions. */
PERF_TEST(ActionInterfacePerf, ScaleFullPipeline) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  setFloat(ctx, 50.0F);

  ActionInterface iface = makeEngine(ctx);

  // 32 armed watchpoints
  for (std::size_t i = 0; i < WATCHPOINT_TABLE_SIZE; ++i) {
    armWatchpoint(iface.watchpoints[i], static_cast<std::uint16_t>(i + 1),
                  static_cast<std::uint16_t>(i + 1), WatchPredicate::GT,
                  100.0F + static_cast<float>(i));
  }

  // 16 armed groups (2 refs each, by watchpoint ID)
  for (std::size_t i = 0; i < WATCHPOINT_GROUP_TABLE_SIZE; ++i) {
    auto& g = iface.groups[i];
    g.groupId = static_cast<std::uint16_t>(i + 1);
    g.armed = true;
    g.eventId = static_cast<std::uint16_t>(200 + i);
    g.count = 2;
    g.refs[0] = static_cast<std::uint16_t>((i * 2) % WATCHPOINT_TABLE_SIZE + 1);
    g.refs[1] = static_cast<std::uint16_t>((i * 2 + 1) % WATCHPOINT_TABLE_SIZE + 1);
  }

  // 16 sequences in WAITING state (8 RTS + 8 ATS)
  for (std::size_t i = 0; i < SEQUENCE_TABLE_SIZE; ++i) {
    auto& seq = iface.sequences[i];
    seq.type = (i < Config::RTS_SLOT_COUNT) ? SequenceType::RTS : SequenceType::ATS;
    seq.sequenceId = static_cast<std::uint16_t>(300 + i);
    seq.eventId = static_cast<std::uint16_t>(300 + i);
    seq.armed = true;
    seq.stepCount = 4;
    for (std::size_t s = 0; s < 4; ++s) {
      seq.steps[s].delayCycles = 0xFFFFFFFF;
      armCommandAction(seq.steps[s].action);
    }
    startSequence(seq);
  }

  // 8 queued COMMAND actions
  for (std::uint8_t a = 0; a < 8; ++a) {
    armCommandAction(iface.actions[a]);
  }
  iface.actionCount = 8;

  rebuildWatchpointIndex(iface);
  rebuildEventIndex(iface);
  processCycle(iface, 0); // Prime

  std::printf("\n=== Scale: 32 WPs + 16 groups + 16 sequences + 8 actions ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      processCycle(iface, iface.stats.totalCycles);
    }
  });

  auto result =
      perf.throughputLoop([&] { processCycle(iface, iface.stats.totalCycles); }, "scale_full");

  std::printf("Scale pipeline: %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
}

/** @brief Catalog lookup latency at 64 and 200 entries. */
PERF_TEST(ActionInterfacePerf, CatalogLookup) {
  UB_PERF_GUARD(perf);

  // --- 64 entries ---
  SequenceCatalog catalog64;
  for (std::uint16_t i = 1; i <= 64; ++i) {
    CatalogEntry entry{};
    entry.sequenceId = i * 10;
    entry.type = SequenceType::RTS;
    entry.stepCount = 4;
    entry.priority = static_cast<std::uint8_t>(i % 10);
    std::snprintf(entry.filename, CATALOG_FILENAME_MAX, "rts_%03u.rts", i);
    catalog64.add(entry);
  }

  std::printf("\n=== Catalog Lookup ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)catalog64.findById(320);
    }
  });
  auto hit64 = perf.throughputLoop([&] { (void)catalog64.findById(320); }, "catalog_hit_64");
  std::printf("Hit  (64 entries): %10.0f ops/s  (%.1f ns/call)\n", hit64.callsPerSecond,
              hit64.stats.median * 1000.0);

  auto miss64 = perf.throughputLoop([&] { (void)catalog64.findById(999); }, "catalog_miss_64");
  std::printf("Miss (64 entries): %10.0f ops/s  (%.1f ns/call)\n", miss64.callsPerSecond,
              miss64.stats.median * 1000.0);
}

/** @brief Catalog lookup at 200 entries (production scale). */
PERF_TEST(ActionInterfacePerf, CatalogLookup200) {
  UB_PERF_GUARD(perf);

  // Build a 200-entry catalog with variable-length cached binaries
  SequenceCatalog catalog;
  for (std::uint16_t i = 1; i <= 200; ++i) {
    CatalogEntry entry{};
    entry.sequenceId = i * 5; // IDs: 5, 10, 15, ... 1000
    entry.type = (i % 4 == 0) ? SequenceType::ATS : SequenceType::RTS;
    entry.stepCount = static_cast<std::uint8_t>((i % 8) + 1); // 1-8 steps
    entry.priority = static_cast<std::uint8_t>(i % 10);
    entry.armed = true;
    entry.blockCount = 1;
    entry.blocks[0] = static_cast<std::uint16_t>((i + 1) * 5);
    entry.exclusionGroup = static_cast<std::uint8_t>((i % 8) + 1);
    entry.abortEventId = static_cast<std::uint16_t>(2000 + i);

    // Cached binary: header + steps
    const std::size_t PAYLOAD = CatalogEntry::HEADER_SIZE +
                                static_cast<std::size_t>(entry.stepCount) * CatalogEntry::STEP_SIZE;
    entry.binary.resize(PAYLOAD, 0);
    entry.binary[0] = static_cast<std::uint8_t>(entry.sequenceId & 0xFF);
    entry.binary[1] = static_cast<std::uint8_t>(entry.sequenceId >> 8);
    entry.binary[4] = entry.stepCount;
    entry.binary[7] = 1;
    entry.binaryLoaded = true;

    std::snprintf(entry.filename, CATALOG_FILENAME_MAX, "rts_%03u.rts", i);
    catalog.add(entry);
  }

  std::printf("\n=== Catalog at Production Scale (200 entries) ===\n");

  // Lookup hit (middle of range)
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)catalog.findById(500);
    }
  });
  auto hit = perf.throughputLoop([&] { (void)catalog.findById(500); }, "catalog_hit_200");
  std::printf("Hit  (200 entries): %10.0f ops/s  (%.1f ns/call)\n", hit.callsPerSecond,
              hit.stats.median * 1000.0);

  // Lookup miss
  auto miss = perf.throughputLoop([&] { (void)catalog.findById(9999); }, "catalog_miss_200");
  std::printf("Miss (200 entries): %10.0f ops/s  (%.1f ns/call)\n", miss.callsPerSecond,
              miss.stats.median * 1000.0);

  // ForEachByEvent scan (linear scan for event-triggered sequences)
  std::uint16_t matchCount = 0;
  auto eventScan = perf.throughputLoop(
      [&] {
        matchCount = 0;
        catalog.forEachByEvent(2050, [&](const CatalogEntry&) { ++matchCount; });
      },
      "catalog_event_scan_200");
  std::printf("Event scan (200):   %10.0f ops/s  (%.1f ns/call)\n", eventScan.callsPerSecond,
              eventScan.stats.median * 1000.0);
}

/** @brief WatchpointCatalog activate/deactivate cycle at scale. */
PERF_TEST(ActionInterfacePerf, WatchpointActivateDeactivate) {
  UB_PERF_GUARD(perf);

  // Build a 100-entry watchpoint catalog
  WatchpointCatalog wpCatalog;
  for (std::uint16_t i = 1; i <= 100; ++i) {
    WatchpointDef def{};
    def.watchpointId = i;
    def.target = {0x007800, DataCategory::OUTPUT, static_cast<std::uint16_t>((i * 4) % 256), 4};
    def.predicate = WatchPredicate::GT;
    def.dataType = WatchDataType::FLOAT32;
    def.eventId = i;
    float threshold = static_cast<float>(i) * 10.0F;
    std::memcpy(def.threshold.data(), &threshold, sizeof(float));
    wpCatalog.add(def);
  }

  DataWatchpoint table[WATCHPOINT_TABLE_SIZE]{};

  std::printf("\n=== Watchpoint Activate/Deactivate (100 defs, 32 slots) ===\n");

  // Benchmark activate (fill table to capacity)
  auto activate = perf.throughputLoop(
      [&] {
        // Clear table
        for (auto& wp : table) {
          wp = DataWatchpoint{};
        }
        // Activate 32 watchpoints
        for (std::uint16_t id = 1; id <= 32; ++id) {
          const auto* def = wpCatalog.findById(id);
          if (def != nullptr) {
            activateWatchpoint(*def, table, WATCHPOINT_TABLE_SIZE);
          }
        }
      },
      "wp_activate_32");
  std::printf("Activate 32 WPs:   %10.0f ops/s  (%.1f ns/call)\n", activate.callsPerSecond,
              activate.stats.median * 1000.0);

  // Benchmark deactivate + reactivate cycle (single entry)
  // First fill to 32
  for (auto& wp : table) {
    wp = DataWatchpoint{};
  }
  for (std::uint16_t id = 1; id <= 32; ++id) {
    const auto* def = wpCatalog.findById(id);
    if (def != nullptr) {
      activateWatchpoint(*def, table, WATCHPOINT_TABLE_SIZE);
    }
  }

  auto cycle = perf.throughputLoop(
      [&] {
        deactivateWatchpoint(16, table, WATCHPOINT_TABLE_SIZE);
        const auto* def = wpCatalog.findById(16);
        if (def != nullptr) {
          activateWatchpoint(*def, table, WATCHPOINT_TABLE_SIZE);
        }
      },
      "wp_deact_react_single");
  std::printf("Deact+React (1 WP): %10.0f ops/s  (%.1f ns/call)\n", cycle.callsPerSecond,
              cycle.stats.median * 1000.0);
}

/** @brief rebuildWatchpointIndex cost at various fill levels. */
PERF_TEST(ActionInterfacePerf, RebuildWatchpointIndex) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  ActionInterface iface = makeEngine(ctx);

  // Fill all 32 watchpoint slots
  for (std::size_t i = 0; i < WATCHPOINT_TABLE_SIZE; ++i) {
    armWatchpoint(iface.watchpoints[i], static_cast<std::uint16_t>(i + 1),
                  static_cast<std::uint16_t>(i + 1), WatchPredicate::GT, 100.0F);
  }

  std::printf("\n=== rebuildWatchpointIndex (32 active WPs) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      rebuildWatchpointIndex(iface);
    }
  });

  auto result = perf.throughputLoop([&] { rebuildWatchpointIndex(iface); }, "rebuild_index_32");
  std::printf("Rebuild index (32 WPs): %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
}

/* ----------------------------- Cascading Fire ----------------------------- */

/** @brief Watchpoints fire -> events -> sequences start + execute -> actions route. */
PERF_TEST(ActionInterfacePerf, CascadingFire) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  ActionInterface iface = makeEngine(ctx);

  // 8 watchpoints armed at different byte offsets, all GT threshold.
  // Each fires a unique eventId (1-8).
  for (std::size_t i = 0; i < 8; ++i) {
    auto& wp = iface.watchpoints[i];
    wp.watchpointId = static_cast<std::uint16_t>(i + 1);
    wp.target = {0x007800, DataCategory::OUTPUT, static_cast<std::uint16_t>(i), 1};
    wp.predicate = WatchPredicate::GT;
    wp.dataType = WatchDataType::UINT8;
    wp.threshold[0] = 100;
    wp.eventId = static_cast<std::uint16_t>(i + 1);
    wp.armed = true;
  }

  // 4 groups: each combines 2 watchpoints (AND). Fire eventIds 50-53.
  for (std::size_t i = 0; i < 4; ++i) {
    auto& g = iface.groups[i];
    g.groupId = static_cast<std::uint16_t>(i + 1);
    g.refs[0] = static_cast<std::uint16_t>(i * 2 + 1);
    g.refs[1] = static_cast<std::uint16_t>(i * 2 + 2);
    g.count = 2;
    g.logic = GroupLogic::AND;
    g.eventId = static_cast<std::uint16_t>(50 + i);
    g.armed = true;
  }

  // 8 sequences bound to WP events (eventId 1-8), each with 2 COMMAND steps.
  // Step 0 fires immediately (delayCycles=0), step 1 has 1-cycle delay.
  for (std::size_t i = 0; i < 8; ++i) {
    auto& seq = iface.sequences[i];
    seq.sequenceId = static_cast<std::uint16_t>(i + 1);
    seq.eventId = static_cast<std::uint16_t>(i + 1);
    seq.armed = true;
    seq.stepCount = 2;
    seq.steps[0].delayCycles = 0;
    armCommandAction(seq.steps[0].action);
    seq.steps[1].delayCycles = 0;
    armCommandAction(seq.steps[1].action);
  }

  // 4 notifications on group events (50-53)
  for (std::size_t i = 0; i < 4; ++i) {
    auto& note = iface.notifications[i];
    note.notificationId = static_cast<std::uint16_t>(i + 1);
    note.eventId = static_cast<std::uint16_t>(50 + i);
    note.armed = true;
    std::snprintf(note.logMessage, sizeof(note.logMessage), "group-%zu", i);
  }

  rebuildWatchpointIndex(iface);

  std::printf("\n=== Cascading Fire: 8 WPs -> 4 groups -> 8 seqs -> COMMAND actions ===\n");

  // Set all bytes above threshold so all 8 WPs + 4 groups fire
  for (std::size_t i = 0; i < 8; ++i) {
    ctx.block[i] = 200;
  }

  rebuildWatchpointIndex(iface);
  rebuildEventIndex(iface);
  processCycle(iface, 0); // Prime edges

  // Now toggle: drop below threshold then raise again each iteration
  // to generate fresh edges every 2 cycles
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      // Odd: below threshold (resets edge)
      for (std::size_t b = 0; b < 8; ++b) {
        ctx.block[b] = 50;
      }
      processCycle(iface, iface.stats.totalCycles);

      // Even: above threshold (fires all)
      for (std::size_t b = 0; b < 8; ++b) {
        ctx.block[b] = 200;
      }
      processCycle(iface, iface.stats.totalCycles);
    }
  });

  // Measure the firing cycle only (above threshold, fresh edge)
  auto result = perf.throughputLoop(
      [&] {
        // Reset edges
        for (std::size_t b = 0; b < 8; ++b) {
          ctx.block[b] = 50;
        }
        processCycle(iface, iface.stats.totalCycles);

        // Fire all
        for (std::size_t b = 0; b < 8; ++b) {
          ctx.block[b] = 200;
        }
        processCycle(iface, iface.stats.totalCycles);
      },
      "cascade_fire_8wp_4grp_8seq");

  // Result is for the pair (reset + fire). Divide by 2 for per-cycle.
  std::printf("Cascade (reset+fire pair): %10.0f ops/s  (%.1f ns/pair, ~%.0f ns/fire-cycle)\n",
              result.callsPerSecond, result.stats.median * 1000.0,
              result.stats.median * 1000.0 / 2.0);

  std::printf("  Stats: WP=%u grp=%u steps=%u cmds=%u notifs=%u\n", iface.stats.watchpointsFired,
              iface.stats.groupsFired, iface.stats.sequenceSteps, iface.stats.commandsRouted,
              iface.stats.notificationsInvoked);
}

/** @brief Scale cascade: 32 WPs fire + 16 groups + 16 sequences + notifications. */
PERF_TEST(ActionInterfacePerf, ScaleCascadingFire) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  ActionInterface iface = makeEngine(ctx);

  // 32 WPs monitoring consecutive bytes, all GT 100
  for (std::size_t i = 0; i < WATCHPOINT_TABLE_SIZE; ++i) {
    auto& wp = iface.watchpoints[i];
    wp.watchpointId = static_cast<std::uint16_t>(i + 1);
    wp.target = {0x007800, DataCategory::OUTPUT, static_cast<std::uint16_t>(i % 64), 1};
    wp.predicate = WatchPredicate::GT;
    wp.dataType = WatchDataType::UINT8;
    wp.threshold[0] = 100;
    wp.eventId = static_cast<std::uint16_t>(i + 1);
    wp.armed = true;
  }

  // 16 groups, each AND of 2 consecutive WPs
  for (std::size_t i = 0; i < WATCHPOINT_GROUP_TABLE_SIZE; ++i) {
    auto& g = iface.groups[i];
    g.groupId = static_cast<std::uint16_t>(i + 1);
    g.refs[0] = static_cast<std::uint16_t>(i * 2 + 1);
    g.refs[1] = static_cast<std::uint16_t>(i * 2 + 2);
    g.count = 2;
    g.logic = GroupLogic::AND;
    g.eventId = static_cast<std::uint16_t>(200 + i);
    g.armed = true;
  }

  // 16 sequences bound to first 16 WP events, 2 COMMAND steps each
  for (std::size_t i = 0; i < SEQUENCE_TABLE_SIZE; ++i) {
    auto& seq = iface.sequences[i];
    seq.sequenceId = static_cast<std::uint16_t>(i + 1);
    seq.eventId = static_cast<std::uint16_t>(i + 1);
    seq.armed = true;
    seq.stepCount = 2;
    seq.steps[0].delayCycles = 0;
    armCommandAction(seq.steps[0].action);
    seq.steps[1].delayCycles = 0;
    armCommandAction(seq.steps[1].action);
  }

  // 16 notifications on group events
  for (std::size_t i = 0; i < 16; ++i) {
    auto& note = iface.notifications[i];
    note.notificationId = static_cast<std::uint16_t>(i + 1);
    note.eventId = static_cast<std::uint16_t>(200 + i);
    note.armed = true;
    std::snprintf(note.logMessage, sizeof(note.logMessage), "grp-%zu", i);
  }

  rebuildWatchpointIndex(iface);

  std::printf("\n=== Scale Cascade: 32 WPs -> 16 groups -> 16 seqs -> COMMAND ===\n");

  // All bytes above threshold
  for (std::size_t i = 0; i < 64; ++i) {
    ctx.block[i] = 200;
  }

  rebuildWatchpointIndex(iface);
  rebuildEventIndex(iface);
  processCycle(iface, 0); // Prime edges

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (std::size_t b = 0; b < 64; ++b) {
        ctx.block[b] = 50;
      }
      processCycle(iface, iface.stats.totalCycles);
      for (std::size_t b = 0; b < 64; ++b) {
        ctx.block[b] = 200;
      }
      processCycle(iface, iface.stats.totalCycles);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        for (std::size_t b = 0; b < 64; ++b) {
          ctx.block[b] = 50;
        }
        processCycle(iface, iface.stats.totalCycles);
        for (std::size_t b = 0; b < 64; ++b) {
          ctx.block[b] = 200;
        }
        processCycle(iface, iface.stats.totalCycles);
      },
      "cascade_scale_32wp_16grp_16seq");

  std::printf("Scale cascade (reset+fire): %10.0f ops/s  (%.1f ns/pair, ~%.0f ns/fire-cycle)\n",
              result.callsPerSecond, result.stats.median * 1000.0,
              result.stats.median * 1000.0 / 2.0);

  std::printf("  Stats: WP=%u grp=%u steps=%u cmds=%u notifs=%u\n", iface.stats.watchpointsFired,
              iface.stats.groupsFired, iface.stats.sequenceSteps, iface.stats.commandsRouted,
              iface.stats.notificationsInvoked);
}

/* ----------------------------- RTS Trigger Path ----------------------------- */

/** @brief Simulate the full startRtsById hot path (catalog + blocking + deserialize + start). */
PERF_TEST(ActionInterfacePerf, RtsTriggerPath) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  ActionInterface iface = makeEngine(ctx);

  // Build a catalog with 200 entries, each with a 4-step cached binary
  SequenceCatalog catalog;
  for (std::uint16_t i = 1; i <= 200; ++i) {
    CatalogEntry entry{};
    entry.sequenceId = i * 10;
    entry.type = SequenceType::RTS;
    entry.stepCount = 4;
    entry.priority = static_cast<std::uint8_t>(i % 10);
    entry.armed = true;
    entry.blockCount = 1;
    entry.blocks[0] = static_cast<std::uint16_t>((i + 1) * 10); // Blocked by next entry
    entry.exclusionGroup = static_cast<std::uint8_t>((i % 4) + 1);
    entry.abortEventId = static_cast<std::uint16_t>(500 + i);

    // Build cached binary: 8-byte header + 4 * 84-byte steps
    const std::size_t PAYLOAD_SIZE =
        CatalogEntry::HEADER_SIZE +
        static_cast<std::size_t>(entry.stepCount) * CatalogEntry::STEP_SIZE;
    entry.binary.resize(PAYLOAD_SIZE, 0);
    entry.binary[0] = static_cast<std::uint8_t>(entry.sequenceId & 0xFF);
    entry.binary[1] = static_cast<std::uint8_t>(entry.sequenceId >> 8);
    entry.binary[4] = entry.stepCount;
    entry.binary[7] = 1; // armed
    entry.binaryLoaded = true;

    catalog.add(entry);
  }

  // Fill all execution slots with running sequences so blocking/exclusion
  // checks have real work (scan 16 entries per check)
  for (std::size_t s = 0; s < SEQUENCE_TABLE_SIZE; ++s) {
    auto& seq = iface.sequences[s];
    seq.sequenceId = static_cast<std::uint16_t>(s * 10 + 5); // Non-matching IDs
    seq.armed = true;
    seq.stepCount = 1;
    seq.steps[0].delayCycles = 0xFFFFFFFF;
    startSequence(seq);
  }

  std::printf("\n=== RTS Trigger Path (200 catalog, 16 running, blocking + exclusion) ===\n");

  // Benchmark: catalog lookup + blocking check (scan 16 slots) + exclusion (scan 16 slots)
  const std::uint16_t TARGET_ID = 500; // Middle of 200-entry catalog

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      // Step 1: Catalog lookup
      const auto* entry = catalog.findById(TARGET_ID);

      // Step 2: Blocking check (scan all slots for blockers)
      bool blocked = false;
      if (entry != nullptr) {
        for (std::uint8_t b = 0; b < entry->blockCount; ++b) {
          for (std::size_t s = 0; s < SEQUENCE_TABLE_SIZE; ++s) {
            if (isRunning(iface.sequences[s]) &&
                iface.sequences[s].sequenceId == entry->blocks[b]) {
              blocked = true;
            }
          }
        }
      }

      // Step 3: Exclusion check (scan all slots for same group)
      if (!blocked && entry != nullptr && entry->exclusionGroup != 0) {
        for (std::size_t s = 0; s < SEQUENCE_TABLE_SIZE; ++s) {
          if (isRunning(iface.sequences[s])) {
            const auto* running = catalog.findById(iface.sequences[s].sequenceId);
            if (running != nullptr && running->exclusionGroup == entry->exclusionGroup) {
              (void)running; // Would abort in real code
            }
          }
        }
      }

      (void)blocked;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        const auto* entry = catalog.findById(TARGET_ID);
        bool blocked = false;
        if (entry != nullptr) {
          for (std::uint8_t b = 0; b < entry->blockCount; ++b) {
            for (std::size_t s = 0; s < SEQUENCE_TABLE_SIZE; ++s) {
              if (isRunning(iface.sequences[s]) &&
                  iface.sequences[s].sequenceId == entry->blocks[b]) {
                blocked = true;
              }
            }
          }
        }
        if (!blocked && entry != nullptr && entry->exclusionGroup != 0) {
          for (std::size_t s = 0; s < SEQUENCE_TABLE_SIZE; ++s) {
            if (isRunning(iface.sequences[s])) {
              const auto* running = catalog.findById(iface.sequences[s].sequenceId);
              (void)running;
            }
          }
        }
        (void)blocked;
      },
      "rts_trigger_checks");

  std::printf("Trigger checks (lookup+block+excl): %10.0f ops/s  (%.1f ns/call)\n",
              result.callsPerSecond, result.stats.median * 1000.0);
}

/* ----------------------------- Abort Event Dispatch ----------------------------- */

/** @brief dispatchAbortEvents scan cost at scale. */
PERF_TEST(ActionInterfacePerf, AbortEventDispatch) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;
  ActionInterface iface = makeEngine(ctx);

  // Set up 8 sequences with abort events pending (worst case)
  for (std::size_t i = 0; i < 8; ++i) {
    auto& seq = iface.sequences[i];
    seq.sequenceId = static_cast<std::uint16_t>(100 + i);
    seq.abortEventId = static_cast<std::uint16_t>(500 + i);
    seq.abortEventPending = true;
    seq.status = SequenceStatus::ABORTED;
  }

  // Notification to receive abort events
  for (std::size_t i = 0; i < 8; ++i) {
    auto& note = iface.notifications[i];
    note.notificationId = static_cast<std::uint16_t>(i + 1);
    note.eventId = static_cast<std::uint16_t>(500 + i);
    note.armed = true;
    std::snprintf(note.logMessage, sizeof(note.logMessage), "abort-%zu", i);
  }

  std::printf("\n=== Abort Event Dispatch (8 pending, 8 notifications) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      // Re-arm pending flags
      for (std::size_t s = 0; s < 8; ++s) {
        iface.sequences[s].abortEventPending = true;
      }
      dispatchAbortEvents(iface);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        for (std::size_t s = 0; s < 8; ++s) {
          iface.sequences[s].abortEventPending = true;
        }
        dispatchAbortEvents(iface);
      },
      "abort_dispatch_8");

  std::printf("Abort dispatch (8 pending): %10.0f ops/s  (%.1f ns/call)\n", result.callsPerSecond,
              result.stats.median * 1000.0);
}

/* ----------------------------- WatchFunction Cost ----------------------------- */

/** @brief Compare per-watchpoint cost across all WatchFunction types. */
PERF_TEST(ActionInterfacePerf, WatchFunctionCost) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;

  // Set up a 3D float vector in the data block: (3.0, 4.0, 5.0)
  float vec[3] = {3.0F, 4.0F, 5.0F};
  std::memcpy(ctx.block.data(), vec, sizeof(vec));

  std::printf("\n=== WatchFunction Per-Evaluation Cost ===\n");

  // Helper: build a single-WP engine, benchmark processCycle
  auto benchFunction = [&](const char* name, WatchFunction fn, std::uint8_t magFields = 1) {
    ActionInterface iface = makeEngine(ctx);
    auto& wp = iface.watchpoints[0];
    wp.watchpointId = 1;
    wp.target = {0x007800, DataCategory::OUTPUT, 0, 4};
    wp.predicate = WatchPredicate::GT;
    wp.dataType = WatchDataType::FLOAT32;
    wp.function = fn;
    wp.magnitudeFields = magFields;
    wp.sampleWindow = 8;
    wp.armed = true;
    double threshold = 999999.0; // Never fires (steady-state cost)
    std::memcpy(wp.threshold.data(), &threshold, sizeof(double));

    rebuildWatchpointIndex(iface);
    rebuildEventIndex(iface);
    processCycle(iface, 0); // Prime

    auto result = perf.throughputLoop([&] { processCycle(iface, iface.stats.totalCycles); }, name);
    std::printf("  %-12s: %10.0f ops/s  (%.1f ns/call)\n", name, result.callsPerSecond,
                result.stats.median * 1000.0);
  };

  benchFunction("NONE", WatchFunction::NONE);
  benchFunction("DELTA", WatchFunction::DELTA);
  benchFunction("RATE", WatchFunction::RATE);
  benchFunction("MEAN", WatchFunction::MEAN);
  benchFunction("STALE", WatchFunction::STALE);
  benchFunction("MAGNITUDE_1", WatchFunction::MAGNITUDE, 1);
  benchFunction("MAGNITUDE_3", WatchFunction::MAGNITUDE, 3);

  // CUSTOM: square function
  auto squareFn = [](void*, const std::uint8_t* data, std::size_t) noexcept -> double {
    float v = 0.0F;
    std::memcpy(&v, data, 4);
    return static_cast<double>(v) * static_cast<double>(v);
  };
  {
    ActionInterface iface = makeEngine(ctx);
    auto& wp = iface.watchpoints[0];
    wp.watchpointId = 1;
    wp.target = {0x007800, DataCategory::OUTPUT, 0, 4};
    wp.predicate = WatchPredicate::GT;
    wp.dataType = WatchDataType::FLOAT32;
    wp.function = WatchFunction::CUSTOM;
    wp.customCompute = {squareFn, nullptr};
    wp.armed = true;
    double threshold = 999999.0;
    std::memcpy(wp.threshold.data(), &threshold, sizeof(double));

    rebuildWatchpointIndex(iface);
    rebuildEventIndex(iface);
    processCycle(iface, 0);

    auto result =
        perf.throughputLoop([&] { processCycle(iface, iface.stats.totalCycles); }, "CUSTOM");
    std::printf("  %-12s: %10.0f ops/s  (%.1f ns/call)\n", "CUSTOM", result.callsPerSecond,
                result.stats.median * 1000.0);
  }
}

/* ----------------------------- Computed Function Cascade ----------------------------- */

/** @brief Realistic cascade: RATE + MAGNITUDE WPs -> groups -> sequences -> commands.
 *
 * Simulates an aircraft scenario:
 *   - 16 RATE watchpoints monitoring telemetry rate-of-change
 *   - 8 MAGNITUDE watchpoints monitoring 3D vectors (accel, gyro, etc.)
 *   - 8 DELTA watchpoints monitoring absolute change
 *   - 16 groups combining WPs with AND logic
 *   - 16 sequences triggered by group events
 *   - Commands routed on every fire
 */
PERF_TEST(ActionInterfacePerf, ComputedCascade) {
  UB_PERF_GUARD(perf);

  ResolverCtx ctx;

  // Fill data block with float values that will trigger RATE/DELTA
  for (std::size_t i = 0; i < 64; ++i) {
    float v = static_cast<float>(i) * 10.0F;
    if (i * 4 + 4 <= ctx.block.size()) {
      std::memcpy(ctx.block.data() + i * 4, &v, 4);
    }
  }

  ActionInterface iface = makeEngine(ctx);

  // 16 RATE watchpoints (offsets 0-60, each monitoring one float)
  for (std::size_t i = 0; i < 16; ++i) {
    auto& wp = iface.watchpoints[i];
    wp.watchpointId = static_cast<std::uint16_t>(i + 1);
    wp.target = {0x007800, DataCategory::OUTPUT, static_cast<std::uint16_t>(i * 4), 4};
    wp.predicate = WatchPredicate::GT;
    wp.dataType = WatchDataType::FLOAT32;
    wp.function = WatchFunction::RATE;
    wp.eventId = static_cast<std::uint16_t>(i + 1);
    wp.armed = true;
    double threshold = 0.1; // Low threshold so rate fires on any change
    std::memcpy(wp.threshold.data(), &threshold, sizeof(double));
  }

  // 8 MAGNITUDE watchpoints (3-field vectors at offsets 0, 12, 24, ...)
  for (std::size_t i = 0; i < 8; ++i) {
    auto& wp = iface.watchpoints[16 + i];
    wp.watchpointId = static_cast<std::uint16_t>(17 + i);
    wp.target = {0x007800, DataCategory::OUTPUT, static_cast<std::uint16_t>(i * 12), 12};
    wp.predicate = WatchPredicate::GT;
    wp.dataType = WatchDataType::FLOAT32;
    wp.function = WatchFunction::MAGNITUDE;
    wp.magnitudeFields = 3;
    wp.eventId = static_cast<std::uint16_t>(17 + i);
    wp.armed = true;
    double threshold = 0.1;
    std::memcpy(wp.threshold.data(), &threshold, sizeof(double));
  }

  // 8 DELTA watchpoints
  for (std::size_t i = 0; i < 8; ++i) {
    auto& wp = iface.watchpoints[24 + i];
    wp.watchpointId = static_cast<std::uint16_t>(25 + i);
    wp.target = {0x007800, DataCategory::OUTPUT, static_cast<std::uint16_t>(i * 4), 4};
    wp.predicate = WatchPredicate::GT;
    wp.dataType = WatchDataType::FLOAT32;
    wp.function = WatchFunction::DELTA;
    wp.eventId = static_cast<std::uint16_t>(25 + i);
    wp.armed = true;
    double threshold = 0.1;
    std::memcpy(wp.threshold.data(), &threshold, sizeof(double));
  }

  // 16 groups: each AND of 2 watchpoints (RATE + MAGNITUDE pairs)
  for (std::size_t i = 0; i < 16; ++i) {
    auto& g = iface.groups[i];
    g.groupId = static_cast<std::uint16_t>(i + 1);
    g.refs[0] = static_cast<std::uint16_t>((i % 16) + 1); // RATE WP
    g.refs[1] = static_cast<std::uint16_t>((i % 8) + 17); // MAGNITUDE WP
    g.count = 2;
    g.logic = GroupLogic::AND;
    g.eventId = static_cast<std::uint16_t>(100 + i);
    g.armed = true;
  }

  // 16 sequences triggered by group events, 2 COMMAND steps each
  for (std::size_t i = 0; i < 16; ++i) {
    auto& seq = iface.sequences[i];
    seq.sequenceId = static_cast<std::uint16_t>(i + 1);
    seq.eventId = static_cast<std::uint16_t>(100 + i);
    seq.armed = true;
    seq.stepCount = 2;
    seq.steps[0].delayCycles = 0;
    armCommandAction(seq.steps[0].action);
    seq.steps[1].delayCycles = 0;
    armCommandAction(seq.steps[1].action);
  }

  // 8 notifications on group events
  for (std::size_t i = 0; i < 8; ++i) {
    auto& note = iface.notifications[i];
    note.notificationId = static_cast<std::uint16_t>(i + 1);
    note.eventId = static_cast<std::uint16_t>(100 + i);
    note.armed = true;
    std::snprintf(note.logMessage, sizeof(note.logMessage), "grp-%zu", i);
  }

  rebuildWatchpointIndex(iface);
  rebuildEventIndex(iface);
  processCycle(iface, 0); // Prime

  std::printf("\n=== Computed Cascade: 16 RATE + 8 MAG + 8 DELTA -> 16 groups -> 16 seqs ===\n");

  // Toggle data to generate edges (change values each pair of cycles)
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      // Low values
      for (std::size_t b = 0; b < 64; ++b) {
        float v = 1.0F;
        if (b * 4 + 4 <= ctx.block.size()) {
          std::memcpy(ctx.block.data() + b * 4, &v, 4);
        }
      }
      processCycle(iface, iface.stats.totalCycles);

      // High values
      for (std::size_t b = 0; b < 64; ++b) {
        float v = static_cast<float>(b + 1) * 100.0F;
        if (b * 4 + 4 <= ctx.block.size()) {
          std::memcpy(ctx.block.data() + b * 4, &v, 4);
        }
      }
      processCycle(iface, iface.stats.totalCycles);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        for (std::size_t b = 0; b < 64; ++b) {
          float v = 1.0F;
          if (b * 4 + 4 <= ctx.block.size()) {
            std::memcpy(ctx.block.data() + b * 4, &v, 4);
          }
        }
        processCycle(iface, iface.stats.totalCycles);

        for (std::size_t b = 0; b < 64; ++b) {
          float v = static_cast<float>(b + 1) * 100.0F;
          if (b * 4 + 4 <= ctx.block.size()) {
            std::memcpy(ctx.block.data() + b * 4, &v, 4);
          }
        }
        processCycle(iface, iface.stats.totalCycles);
      },
      "computed_cascade_32wp_16grp_16seq");

  std::printf("Computed cascade (reset+fire): %10.0f ops/s  (%.1f ns/pair, ~%.0f ns/fire)\n",
              result.callsPerSecond, result.stats.median * 1000.0,
              result.stats.median * 1000.0 / 2.0);
  std::printf("  Stats: WP=%u grp=%u steps=%u cmds=%u notifs=%u\n", iface.stats.watchpointsFired,
              iface.stats.groupsFired, iface.stats.sequenceSteps, iface.stats.commandsRouted,
              iface.stats.notificationsInvoked);
}

PERF_MAIN()
