/**
 * @file MnaAllocation_uTest.cpp
 * @brief The solves documented as allocation-free allocate nothing, from the
 *        first one after their setup.
 *
 * The global operator new of this binary counts while a test has it armed.
 * These tests are a binary of their own for that reason.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>

using sim::electronics::algorithms::mna::MnaSolveWorkspace;
using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::mna::MnaSystemSparse;

namespace {

bool armed = false;
std::size_t allocations = 0;

/** @brief Counts the allocations made while it is in scope. */
class AllocationCount {
public:
  AllocationCount() {
    allocations = 0;
    armed = true;
  }
  ~AllocationCount() { armed = false; }
  AllocationCount(const AllocationCount&) = delete;
  AllocationCount& operator=(const AllocationCount&) = delete;

  /** @brief Stop counting and return the count. */
  static std::size_t stop() {
    armed = false;
    return allocations;
  }
};

/** @brief A ladder of `nets` nets: 1 kohm between neighbours and 2 kohm to ground, a source on
 * net 1. */
template <class System> void stampLadder(System& mna, std::size_t nets, double volts) {
  for (unsigned net = 1; net < nets; ++net) {
    if (net + 1 < nets) {
      mna.addConductance(net, net + 1, 1e-3);
    }
    mna.addConductance(net, 0, 0.5e-3);
  }
  mna.addVoltageSource(1, 0, volts);
}

constexpr std::size_t NETS = 40;

} // namespace

/* ----------------------------- Counted Allocation ----------------------------- */

// Every form of the global allocation functions is replaced, so that what
// one of them allocates another of them frees.

namespace {

void* counted(std::size_t size) noexcept {
  if (armed) {
    ++allocations;
  }
  return std::malloc(size != 0 ? size : 1);
}

void* counted(std::size_t size, std::align_val_t alignment) noexcept {
  if (armed) {
    ++allocations;
  }
  // aligned_alloc takes a size that is a multiple of the alignment.
  const auto ALIGN = static_cast<std::size_t>(alignment);
  return std::aligned_alloc(ALIGN, (size + ALIGN - 1) / ALIGN * ALIGN);
}

void* orThrow(void* memory) {
  if (memory == nullptr) {
    throw std::bad_alloc();
  }
  return memory;
}

} // namespace

void* operator new(std::size_t size) { return orThrow(counted(size)); }
void* operator new[](std::size_t size) { return orThrow(counted(size)); }
void* operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
  return counted(size);
}
void* operator new[](std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
  return counted(size);
}
void* operator new(std::size_t size, std::align_val_t alignment) {
  return orThrow(counted(size, alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
  return orThrow(counted(size, alignment));
}
void* operator new(std::size_t size, std::align_val_t alignment,
                   const std::nothrow_t& /*tag*/) noexcept {
  return counted(size, alignment);
}
void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t& /*tag*/) noexcept {
  return counted(size, alignment);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t /*size*/) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t /*size*/) noexcept { std::free(memory); }
void operator delete(void* memory, const std::nothrow_t& /*tag*/) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t& /*tag*/) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t /*alignment*/) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t /*alignment*/) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
  std::free(memory);
}
void operator delete[](void* memory, std::size_t /*size*/,
                       std::align_val_t /*alignment*/) noexcept {
  std::free(memory);
}
void operator delete(void* memory, std::align_val_t /*alignment*/,
                     const std::nothrow_t& /*tag*/) noexcept {
  std::free(memory);
}
void operator delete[](void* memory, std::align_val_t /*alignment*/,
                       const std::nothrow_t& /*tag*/) noexcept {
  std::free(memory);
}

/* ----------------------------- Tests ----------------------------- */

/**
 * @test After one factorize(), restamping the same matrix with another
 *       supply, matching it, rebuilding the right-hand side and solving
 *       allocate nothing, the first time as the second.
 */
TEST(MnaAllocationTest, SparseRightHandSidePathAllocatesNothingFromTheFirstSolve) {
  MnaSystemSparse mna(NETS);
  stampLadder(mna, NETS, 10.0);
  ASSERT_TRUE(mna.factorize());
  std::vector<double> voltages(NETS);
  std::vector<double> currents(1);

  for (const double VOLTS : {20.0, 5.0}) {
    AllocationCount count;
    mna.clearStamps();
    stampLadder(mna, NETS, VOLTS);
    const bool MATCHED = mna.tripletsMatch();
    if (MATCHED) {
      mna.updateRhs();
    }
    const bool SOLVED = MATCHED && mna.solveInto(voltages.data(), currents.data());
    const std::size_t COUNT = AllocationCount::stop();

    ASSERT_TRUE(MATCHED);
    ASSERT_TRUE(SOLVED);
    EXPECT_EQ(COUNT, 0U) << "supply " << VOLTS;
    EXPECT_NEAR(voltages[1], VOLTS, 1e-9);
  }
}

#if COMPAT_HAVE_LAPACKE
/**
 * @test With LAPACKE, clearing, stamping and solving a dense system into a
 *       prepared workspace allocate nothing, the first time as the second.
 */
TEST(MnaAllocationTest, DenseSolveIntoPreparedWorkspaceAllocatesNothing) {
  MnaSystem mna(NETS);
  MnaSolveWorkspace workspace;
  workspace.prepare(NETS + 1);
  std::vector<double> voltages(NETS);
  std::vector<double> currents(1);
  // Stamped once in setup, so that the list of sources has its storage.
  stampLadder(mna, NETS, 10.0);

  for (const double VOLTS : {20.0, 5.0}) {
    AllocationCount count;
    mna.clear();
    stampLadder(mna, NETS, VOLTS);
    const bool SOLVED = mna.solveInto(workspace, voltages.data(), currents.data());
    const std::size_t COUNT = AllocationCount::stop();

    ASSERT_TRUE(SOLVED);
    EXPECT_EQ(COUNT, 0U) << "supply " << VOLTS;
    EXPECT_NEAR(voltages[1], VOLTS, 1e-9);
  }
}
#endif
