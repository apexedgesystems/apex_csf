/**
 * @file main.cpp
 * @brief PPS time-distribution demo (standalone).
 *
 * Runs through the four scenarios called out in the ticket Phase 6.3:
 *
 *   1. Boot convergence:    NONE -> FINE -> PRECISE
 *   2. TNT distribution:    every PPS edge produces a broadcast
 *   3. ATS UTC triggers:    a sequence step fires when current UTC
 *                           crosses its target
 *   4. Degradation/recovery: PPS loss -> STALE -> FREERUN, then
 *                           resetCorrelation + fresh ref/edge -> VALID
 *
 * Uses MockPps and a synthetic steady clock so the demo is fully
 * deterministic and runs to completion in milliseconds. No real GPS,
 * no /dev/pps[N] needed.
 */

#include "src/system/core/components/action/apex/inc/ActionInterface.hpp"
#include "src/system/core/components/action/apex/inc/DataAction.hpp"
#include "src/system/core/components/action/apex/inc/DataSequence.hpp"
#include "src/system/core/components/time_server/apex/inc/TimeServer.hpp"
#include "src/system/core/components/time_server/apex/inc/TimeServerData.hpp"
#include "src/system/core/hal/mock/inc/MockPps.hpp"

#include <fmt/core.h>

#include <cstdint>

namespace {

constexpr std::int64_t NS_PER_SEC = 1'000'000'000LL;

/// Demo harness mirroring how ApexExecutive wires TimeServer and
/// ActionComponent.iface().timeProvider in production.
struct DemoHarness {
  apex::hal::MockPps pps;
  system_core::time_server::TimeServer timeServer;
  system_core::data::ActionInterface action{};
  std::int64_t steadyNs = 0;
  std::uint32_t cycle = 0;

  DemoHarness() {
    (void)pps.init({});
    timeServer.setPpsSource(&pps);
    timeServer.setSteadyClock({&DemoHarness::steadyTrampoline, this});
    timeServer.setBroadcastDelegate({&DemoHarness::broadcastTrampoline, this});

    system_core::time_server::TimeServerTunableParams tprm;
    tprm.driftFilterTaps = 4;
    tprm.maxStalenessUs = 1'500'000;
    tprm.holdoverLimitS = 5; // tighten for visibility
    timeServer.loadTprm(tprm);

    action.timeProvider = timeServer.utcTimeProvider();
  }

  void frame() {
    timeServer.tick(cycle);
    system_core::data::tickSequences(action, cycle);
    ++cycle;
  }

  void edgeAt(std::int64_t edgeNs) {
    steadyNs = edgeNs;
    pps.injectEdge(edgeNs);
    frame();
  }

  void advanceTo(std::int64_t ns) {
    steadyNs = ns;
    frame();
  }

  static std::int64_t steadyTrampoline(void* ctx) noexcept {
    return static_cast<DemoHarness*>(ctx)->steadyNs;
  }
  static void broadcastTrampoline(
      void* /*ctx*/, const system_core::time_server::TimeAtNextTone& tnt) noexcept {
    fmt::print("    [TNT broadcast] valid={} quality={} epochNs={} ppsCount={}\n",
               tnt.valid, tnt.quality, tnt.epochNs, tnt.ppsCount);
  }
};

void scenarioBootConvergence(DemoHarness& h) {
  fmt::print("=== Scenario 1: Boot convergence (NONE -> FINE -> PRECISE) ===\n");
  fmt::print("  Boot tick:\n");
  h.frame();

  fmt::print("  Send SET_REFERENCE_TIME (UTC = 1700000000s):\n");
  system_core::time_server::SetReferenceTime ref{};
  ref.epochNs = 1'700'000'000LL * NS_PER_SEC;
  ref.source = static_cast<std::uint8_t>(system_core::time_server::TimeSource::GPS);
  h.timeServer.handleSetReferenceTime(ref);

  fmt::print("  First PPS edge -> VALID/FINE:\n");
  h.edgeAt(NS_PER_SEC);

  fmt::print("  4 more edges -> drift filter fills -> PRECISE:\n");
  for (int i = 2; i <= 5; ++i) {
    h.edgeAt(static_cast<std::int64_t>(i) * NS_PER_SEC);
  }
  fmt::print("\n");
}

void scenarioAtsAtTime(DemoHarness& h) {
  fmt::print("=== Scenario 2: ATS sequence with AT_TIME trigger ===\n");
  // Build a 1-step ATS in slot 0 firing at UTC = epoch + 7s.
  h.action.sequences[0] = system_core::data::DataSequence{};
  auto& seq = h.action.sequences[0];
  seq.type = system_core::data::SequenceType::ATS;
  seq.armed = true;
  seq.stepCount = 1;
  seq.sequenceId = 0xCAFE;
  seq.steps[0].delayCycles = 7'000'000U; // 7 s in microseconds
  seq.steps[0].action.actionType = system_core::data::ActionType::COMMAND;
  seq.steps[0].action.commandOpcode = 0xBEEF;
  system_core::data::startSequence(seq, h.action.timeProvider());
  fmt::print("  Sequence armed; current UTC us = {}\n", h.action.timeProvider());
  fmt::print("  Target UTC us = {}\n", h.action.timeProvider() + 7'000'000U);

  // Advance steady time past target. Each frame advances 1 s.
  for (int i = 6; i <= 13; ++i) {
    h.edgeAt(static_cast<std::int64_t>(i) * NS_PER_SEC);
    fmt::print("    cycle {} status {} actionCount {}\n", h.cycle - 1,
               static_cast<int>(seq.status), h.action.actionCount);
    if (h.action.actionCount > 0) {
      fmt::print("  -> Step fired with opcode 0x{:04X}\n",
                 h.action.actions[0].commandOpcode);
      break;
    }
  }
  fmt::print("\n");
}

void scenarioDegradationRecovery(DemoHarness& h) {
  fmt::print("=== Scenario 3: PPS loss -> STALE -> FREERUN, then recovery ===\n");
  fmt::print("  No edges for 2.5 s -> STALE:\n");
  h.advanceTo(h.steadyNs + 2'500'000'000LL);
  fmt::print("    valid = {} (STALE = 2)\n", h.timeServer.currentTnt().valid);

  fmt::print("  No edges for another 6 s (past holdoverLimitS=5) -> FREERUN:\n");
  h.advanceTo(h.steadyNs + 6'000'000'000LL);
  fmt::print("    valid = {} (FREERUN = 3)\n", h.timeServer.currentTnt().valid);

  fmt::print("  resetCorrelation + fresh reference + edge -> VALID/FINE:\n");
  h.timeServer.resetCorrelation();
  system_core::time_server::SetReferenceTime ref{};
  ref.epochNs = 1'700'000'100LL * NS_PER_SEC;
  h.timeServer.handleSetReferenceTime(ref);
  h.edgeAt(h.steadyNs + NS_PER_SEC);
  fmt::print("    valid = {} quality = {}\n", h.timeServer.currentTnt().valid,
             h.timeServer.currentTnt().quality);
  fmt::print("\n");
}

} // namespace

int main() {
  fmt::print("ApexTimeDemo: PPS time-distribution end-to-end\n\n");
  DemoHarness h;
  scenarioBootConvergence(h);
  scenarioAtsAtTime(h);
  scenarioDegradationRecovery(h);
  fmt::print("Demo complete.\n");
  return 0;
}
