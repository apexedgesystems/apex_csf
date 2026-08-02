/**
 * @file ParamBank_uTest.cpp
 * @brief Unit tests for system_core::system_component::ParamBank<TParams>.
 *
 * Notes:
 *  - Tests the load/publishInitial/apply/rollback lifecycle, validation
 *    gating, rollback forfeiture, and file loading.
 *  - Composition test proves a schedulable component can own a bank
 *    (params + tasks in one component).
 *  - File-based tests use temporary files.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/ParamBank.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SchedulableComponentBase.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <type_traits>
#include <vector>

using system_core::system_component::ParamBank;
using system_core::system_component::Status;
using system_core::system_component::TPRM_PAYLOAD_MAGIC;
using system_core::system_component::TPRM_PAYLOAD_VERSION;
using system_core::system_component::tprmCrc32;
using system_core::system_component::TprmPayloadCheck;
using system_core::system_component::TprmPayloadHeader;

/** @brief Minimal params POD used for tests. */
struct TestParams {
  std::int32_t value{0};
  std::uint32_t flags{0};
};
static_assert(sizeof(TestParams) == 8, "TestParams should be 8 bytes");

namespace {

/** @brief Validator shared by most tests: negative values are invalid. */
bool nonNegative(const TestParams& p) noexcept { return p.value >= 0; }

/// fullUid the file-load tests stamp and expect.
constexpr std::uint32_t TEST_UID = 0x00AB01;

/** @brief Helper to create a temporary v3-stamped payload file. */
class TempParamsFile {
public:
  explicit TempParamsFile(const TestParams& p, std::uint32_t fullUid = TEST_UID) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    path_ = std::filesystem::temp_directory_path() /
            ("test_params_" + std::to_string(dist(gen)) + ".tprm");

    TprmPayloadHeader hdr{};
    hdr.magic = TPRM_PAYLOAD_MAGIC;
    hdr.version = TPRM_PAYLOAD_VERSION;
    hdr.payloadSize = sizeof(p);
    hdr.fullUid = fullUid;
    hdr.payloadCrc = tprmCrc32(reinterpret_cast<const std::uint8_t*>(&p), sizeof(p));

    std::ofstream file(path_, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    file.write(reinterpret_cast<const char*>(&p), sizeof(p));
    file.close();
  }

  ~TempParamsFile() { std::filesystem::remove(path_); }

  const std::filesystem::path& path() const { return path_; }

private:
  std::filesystem::path path_;
};

} // namespace

/* ----------------------------- Initial State ----------------------------- */

/** @test Initial state: nothing staged, nothing published, no rollback. */
TEST(ParamBankTest, InitialState) {
  ParamBank<TestParams> bank{};
  EXPECT_FALSE(bank.isLoaded());
  EXPECT_FALSE(bank.canRollback());
  EXPECT_EQ(bank.activeGeneration(), 0u);
  EXPECT_EQ(bank.stagedGeneration(), 0u);
  EXPECT_EQ(bank.active().value, 0);
}

/** @test publishInitial() fails before any successful load. */
TEST(ParamBankTest, PublishInitialFailsWithoutLoad) {
  ParamBank<TestParams> bank{};
  EXPECT_EQ(bank.publishInitial(), Status::ERROR_NOT_CONFIGURED);
  EXPECT_EQ(bank.activeGeneration(), 0u);
}

/* ----------------------------- Staging ----------------------------- */

/** @test load(struct): validation failure rejects the set and counts the attempt. */
TEST(ParamBankTest, LoadStructValidationFailure) {
  ParamBank<TestParams> bank{};
  const TestParams BAD{-1, 0};

  EXPECT_EQ(bank.load(BAD, nonNegative), Status::ERROR_LOAD_INVALID);
  EXPECT_EQ(bank.stagedGeneration(), 1u);
  EXPECT_FALSE(bank.isLoaded());
}

/** @test load(struct): success stages the set and marks it loaded. */
TEST(ParamBankTest, LoadStructSuccess) {
  ParamBank<TestParams> bank{};
  const TestParams OK{42, 0xFF};

  EXPECT_EQ(bank.load(OK, nonNegative), Status::SUCCESS);
  EXPECT_EQ(bank.stagedGeneration(), 1u);
  EXPECT_TRUE(bank.isLoaded());
  EXPECT_EQ(bank.staged().value, 42);
  EXPECT_EQ(bank.staged().flags, 0xFFu);
}

/** @test load(path): success reads the binary image into the staged bank. */
TEST(ParamBankTest, LoadFileSuccess) {
  ParamBank<TestParams> bank{};
  const TestParams EXPECTED{100, 0xDEAD};
  TempParamsFile tmpFile(EXPECTED);

  EXPECT_EQ(bank.load(tmpFile.path(), TEST_UID, nonNegative), Status::SUCCESS);
  EXPECT_TRUE(bank.isLoaded());
  EXPECT_EQ(bank.staged().value, 100);
  EXPECT_EQ(bank.staged().flags, 0xDEADu);
}

/** @test load(path): nonexistent file is rejected. */
TEST(ParamBankTest, LoadFileNotFound) {
  ParamBank<TestParams> bank{};
  const std::filesystem::path BAD_PATH{"/nonexistent/path/to/file.bin"};

  EXPECT_EQ(bank.load(BAD_PATH, TEST_UID, nonNegative), Status::ERROR_LOAD_INVALID);
  EXPECT_FALSE(bank.isLoaded());
}

/** @test load(path): validation failure after a successful file read. */
TEST(ParamBankTest, LoadFileValidationFailure) {
  ParamBank<TestParams> bank{};
  const TestParams BAD{-5, 0};
  TempParamsFile tmpFile(BAD);

  EXPECT_EQ(bank.load(tmpFile.path(), TEST_UID, nonNegative), Status::ERROR_LOAD_INVALID);
  EXPECT_FALSE(bank.isLoaded());
}

/** @test load(path): a payload stamped for another fullUid is rejected as such. */
TEST(ParamBankTest, LoadFileWrongTarget) {
  ParamBank<TestParams> bank{};
  TempParamsFile tmpFile(TestParams{1, 0}, 0x00CD02);

  EXPECT_EQ(bank.load(tmpFile.path(), TEST_UID, nonNegative), Status::ERROR_LOAD_INVALID);
  EXPECT_EQ(bank.lastCheck(), TprmPayloadCheck::UID_MISMATCH);
  EXPECT_FALSE(bank.isLoaded());
}

/** @test load(path): a corrupted body fails the CRC check specifically. */
TEST(ParamBankTest, LoadFileCorruptBody) {
  ParamBank<TestParams> bank{};
  TempParamsFile tmpFile(TestParams{7, 0});
  {
    std::fstream f(tmpFile.path(), std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(-1, std::ios::end);
    const char FLIP = '\xFF';
    f.write(&FLIP, 1);
  }

  EXPECT_EQ(bank.load(tmpFile.path(), TEST_UID, nonNegative), Status::ERROR_LOAD_INVALID);
  EXPECT_EQ(bank.lastCheck(), TprmPayloadCheck::CRC_MISMATCH);
}

/** @test load(path): a bare struct image with no prelude is not a v3 payload. */
TEST(ParamBankTest, LoadFileRejectsUnstampedImage) {
  ParamBank<TestParams> bank{};
  const TestParams P{3, 0};
  const auto PATH = std::filesystem::temp_directory_path() / "unstamped_params.tprm";
  {
    std::ofstream f(PATH, std::ios::binary);
    f.write(reinterpret_cast<const char*>(&P), sizeof(P));
  }

  EXPECT_EQ(bank.load(PATH, TEST_UID, nonNegative), Status::ERROR_LOAD_INVALID);
  EXPECT_EQ(bank.lastCheck(), TprmPayloadCheck::TOO_SMALL);
  std::filesystem::remove(PATH);
}

/* ----------------------------- Publishing ----------------------------- */

/** @test publishInitial() publishes the staged set exactly once. */
TEST(ParamBankTest, PublishInitialOnce) {
  ParamBank<TestParams> bank{};
  ASSERT_EQ(bank.load(TestParams{55, 0xCAFE}, nonNegative), Status::SUCCESS);

  EXPECT_EQ(bank.active().value, 0);
  ASSERT_EQ(bank.publishInitial(), Status::SUCCESS);
  EXPECT_EQ(bank.active().value, 55);
  EXPECT_EQ(bank.activeGeneration(), 1u);
  EXPECT_FALSE(bank.canRollback());

  EXPECT_EQ(bank.publishInitial(), Status::WARN_NOOP);
  EXPECT_EQ(bank.activeGeneration(), 1u);
}

/** @test apply() fails before any successful load. */
TEST(ParamBankTest, ApplyFailsWithoutLoad) {
  ParamBank<TestParams> bank{};
  EXPECT_EQ(bank.apply(), Status::ERROR_NOT_CONFIGURED);
}

/** @test A rejected load() cannot be published by a subsequent apply(). */
TEST(ParamBankTest, ApplyRefusesRejectedStagedSet) {
  ParamBank<TestParams> bank{};
  ASSERT_EQ(bank.load(TestParams{10, 0}, nonNegative), Status::SUCCESS);
  ASSERT_EQ(bank.publishInitial(), Status::SUCCESS);

  ASSERT_EQ(bank.load(TestParams{-3, 0}, nonNegative), Status::ERROR_LOAD_INVALID);
  EXPECT_EQ(bank.apply(), Status::ERROR_NOT_CONFIGURED);
  EXPECT_EQ(bank.active().value, 10);

  // A later valid load restores publishability.
  ASSERT_EQ(bank.load(TestParams{20, 0}, nonNegative), Status::SUCCESS);
  EXPECT_EQ(bank.apply(), Status::SUCCESS);
  EXPECT_EQ(bank.active().value, 20);
}

/** @test apply() hot-swaps staged to active without touching readers' view semantics. */
TEST(ParamBankTest, ApplyHotReload) {
  ParamBank<TestParams> bank{};
  ASSERT_EQ(bank.load(TestParams{10, 0}, nonNegative), Status::SUCCESS);
  ASSERT_EQ(bank.publishInitial(), Status::SUCCESS);

  ASSERT_EQ(bank.load(TestParams{20, 0}, nonNegative), Status::SUCCESS);
  EXPECT_EQ(bank.staged().value, 20);

  ASSERT_EQ(bank.apply(), Status::SUCCESS);
  EXPECT_EQ(bank.active().value, 20);
  EXPECT_EQ(bank.activeGeneration(), 2u);
}

/* ----------------------------- Rollback ----------------------------- */

/** @test rollback() is WARN_NOOP before any apply(). */
TEST(ParamBankTest, RollbackNoopBeforeApply) {
  ParamBank<TestParams> bank{};
  ASSERT_EQ(bank.load(TestParams{5, 0}, nonNegative), Status::SUCCESS);
  ASSERT_EQ(bank.publishInitial(), Status::SUCCESS);

  EXPECT_FALSE(bank.canRollback());
  EXPECT_EQ(bank.rollback(), Status::WARN_NOOP);
}

/** @test rollback() restores the previous active set (one level). */
TEST(ParamBankTest, RollbackRestoresPrevious) {
  ParamBank<TestParams> bank{};
  ASSERT_EQ(bank.load(TestParams{10, 0}, nonNegative), Status::SUCCESS);
  ASSERT_EQ(bank.publishInitial(), Status::SUCCESS);

  ASSERT_EQ(bank.load(TestParams{20, 0}, nonNegative), Status::SUCCESS);
  ASSERT_EQ(bank.apply(), Status::SUCCESS);
  EXPECT_EQ(bank.active().value, 20);
  EXPECT_TRUE(bank.canRollback());

  ASSERT_EQ(bank.rollback(), Status::SUCCESS);
  EXPECT_EQ(bank.active().value, 10);
  EXPECT_FALSE(bank.canRollback());
}

/** @test Staging over the rollback source forfeits rollback. */
TEST(ParamBankTest, LoadAfterApplyForfeitsRollback) {
  ParamBank<TestParams> bank{};
  ASSERT_EQ(bank.load(TestParams{10, 0}, nonNegative), Status::SUCCESS);
  ASSERT_EQ(bank.publishInitial(), Status::SUCCESS);

  ASSERT_EQ(bank.load(TestParams{20, 0}, nonNegative), Status::SUCCESS);
  ASSERT_EQ(bank.apply(), Status::SUCCESS);
  EXPECT_TRUE(bank.canRollback());

  // This load scribbles the bank rollback would restore; the stale
  // history is unrecoverable and the bank says so.
  ASSERT_EQ(bank.load(TestParams{30, 0}, nonNegative), Status::SUCCESS);
  EXPECT_FALSE(bank.canRollback());
  EXPECT_EQ(bank.rollback(), Status::WARN_NOOP);
  EXPECT_EQ(bank.active().value, 20);
}

/* ----------------------------- Composition ----------------------------- */

namespace {

/** @brief Tunable-parameter set for the composition test. */
struct ModelParams {
  std::int32_t rate{100};
};

/**
 * @brief Schedulable component owning a ParamBank — tasks and A/B params
 *        in one component, which the bank's composability exists for.
 */
class TunableSchedulable : public system_core::system_component::SchedulableComponentBase {
public:
  TunableSchedulable() noexcept = default;

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 998; }
  [[nodiscard]] const char* componentName() const noexcept override { return "TunableSchedulable"; }
  [[nodiscard]] const char* label() const noexcept override { return "TUNABLE_SCHEDULABLE"; }

  [[nodiscard]] Status loadParams(const ModelParams& p) noexcept {
    return bank_.load(p, [](const ModelParams& m) noexcept { return m.rate > 0; });
  }

  [[nodiscard]] ModelParams params() const noexcept { return bank_.active(); }
  [[nodiscard]] Status applyParams() noexcept { return bank_.apply(); }

  [[nodiscard]] std::uint8_t step() noexcept { return 0; }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    if (bank_.publishInitial() != Status::SUCCESS) {
      return static_cast<std::uint8_t>(Status::ERROR_NOT_CONFIGURED);
    }
    const bool REGISTERED =
        registerTask<TunableSchedulable, &TunableSchedulable::step>(0, this, "step");
    return REGISTERED ? 0 : 1;
  }

private:
  system_core::system_component::ParamBank<ModelParams> bank_;
};

} // namespace

/** @test A schedulable component owns a bank: tasks and hot-reloadable params coexist. */
TEST(ParamBankTest, SchedulableComponentComposition) {
  TunableSchedulable comp{};

  ASSERT_EQ(comp.loadParams(ModelParams{250}), Status::SUCCESS);
  ASSERT_EQ(comp.init(), 0u);

  EXPECT_TRUE(comp.isInitialized());
  EXPECT_EQ(comp.taskCount(), 1u);
  EXPECT_NE(comp.taskByUid(0), nullptr);
  EXPECT_EQ(comp.params().rate, 250);

  ASSERT_EQ(comp.loadParams(ModelParams{500}), Status::SUCCESS);
  ASSERT_EQ(comp.applyParams(), Status::SUCCESS);
  EXPECT_EQ(comp.params().rate, 500);
}

/* ----------------------------- Concurrency ----------------------------- */

/**
 * @test Readers never observe a torn parameter set while the writer
 * publishes. Two self-consistent patterns (flags is always the bitwise
 * complement of value) alternate under maximum publish pressure; any
 * observed set must satisfy the invariant exactly.
 */
TEST(ParamBankTest, ConcurrentReadersSeeOnlyPublishedSets) {
  ParamBank<TestParams> bank{};

  auto consistent = [](const TestParams& p) noexcept {
    return p.flags == ~static_cast<std::uint32_t>(p.value);
  };
  const TestParams A{0x11111111, ~static_cast<std::uint32_t>(0x11111111)};
  const TestParams B{0x77777777, ~static_cast<std::uint32_t>(0x77777777)};
  ASSERT_EQ(bank.load(A, consistent), Status::SUCCESS);
  ASSERT_EQ(bank.publishInitial(), Status::SUCCESS);

  std::atomic<bool> stop{false};
  std::atomic<std::uint64_t> tornReads{0};
  std::atomic<std::uint64_t> totalReads{0};

  std::vector<std::thread> readers;
  readers.reserve(3);
  for (int t = 0; t < 3; ++t) {
    readers.emplace_back([&] {
      std::uint64_t local = 0;
      std::uint64_t torn = 0;
      while (!stop.load(std::memory_order_relaxed)) {
        const TestParams SNAP = bank.active();
        if (SNAP.flags != ~static_cast<std::uint32_t>(SNAP.value)) {
          ++torn;
        }
        ++local;
      }
      totalReads.fetch_add(local, std::memory_order_relaxed);
      tornReads.fetch_add(torn, std::memory_order_relaxed);
    });
  }

  // Single writer: maximum publish pressure for a fixed cycle budget.
  bool flip = false;
  for (int i = 0; i < 200000; ++i) {
    ASSERT_EQ(bank.load(flip ? A : B, consistent), Status::SUCCESS);
    ASSERT_EQ(bank.apply(), Status::SUCCESS);
    flip = !flip;
  }

  stop.store(true, std::memory_order_relaxed);
  for (auto& r : readers) {
    r.join();
  }

  EXPECT_EQ(tornReads.load(), 0u);
  EXPECT_GT(totalReads.load(), 0u);
}

/* ----------------------------- Contracts ----------------------------- */

/** @test noexcept contracts for the public API. */
TEST(ParamBankTest, NoexceptContracts) {
  using Bank = ParamBank<TestParams>;
  static_assert(noexcept(std::declval<const Bank&>().active()));
  static_assert(noexcept(std::declval<const Bank&>().staged()));
  static_assert(noexcept(std::declval<const Bank&>().canRollback()));
  static_assert(noexcept(std::declval<const Bank&>().isLoaded()));
  static_assert(noexcept(std::declval<const Bank&>().activeGeneration()));
  static_assert(noexcept(std::declval<const Bank&>().stagedGeneration()));
  static_assert(noexcept(std::declval<Bank&>().load(std::declval<const TestParams&>())));
  static_assert(noexcept(std::declval<Bank&>().publishInitial()));
  static_assert(noexcept(std::declval<Bank&>().apply()));
  static_assert(noexcept(std::declval<Bank&>().rollback()));
  SUCCEED();
}

/** @test TParams must be trivially copyable (compile-time contract). */
TEST(ParamBankTest, TriviallyCopyableCheck) {
  static_assert(std::is_trivially_copyable_v<TestParams>);
  SUCCEED();
}
