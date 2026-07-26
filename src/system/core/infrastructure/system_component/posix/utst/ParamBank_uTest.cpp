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

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <type_traits>

using system_core::system_component::ParamBank;
using system_core::system_component::Status;

/** @brief Minimal params POD used for tests. */
struct TestParams {
  std::int32_t value{0};
  std::uint32_t flags{0};
};
static_assert(sizeof(TestParams) == 8, "TestParams should be 8 bytes");

namespace {

/** @brief Validator shared by most tests: negative values are invalid. */
bool nonNegative(const TestParams& p) noexcept { return p.value >= 0; }

/** @brief Helper to create a temporary binary file with TestParams. */
class TempParamsFile {
public:
  explicit TempParamsFile(const TestParams& p) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    path_ = std::filesystem::temp_directory_path() /
            ("test_params_" + std::to_string(dist(gen)) + ".bin");
    std::ofstream file(path_, std::ios::binary);
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

  EXPECT_EQ(bank.load(tmpFile.path(), nonNegative), Status::SUCCESS);
  EXPECT_TRUE(bank.isLoaded());
  EXPECT_EQ(bank.staged().value, 100);
  EXPECT_EQ(bank.staged().flags, 0xDEADu);
}

/** @test load(path): nonexistent file is rejected. */
TEST(ParamBankTest, LoadFileNotFound) {
  ParamBank<TestParams> bank{};
  const std::filesystem::path BAD_PATH{"/nonexistent/path/to/file.bin"};

  EXPECT_EQ(bank.load(BAD_PATH, nonNegative), Status::ERROR_LOAD_INVALID);
  EXPECT_FALSE(bank.isLoaded());
}

/** @test load(path): validation failure after a successful file read. */
TEST(ParamBankTest, LoadFileValidationFailure) {
  ParamBank<TestParams> bank{};
  const TestParams BAD{-5, 0};
  TempParamsFile tmpFile(BAD);

  EXPECT_EQ(bank.load(tmpFile.path(), nonNegative), Status::ERROR_LOAD_INVALID);
  EXPECT_FALSE(bank.isLoaded());
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

  [[nodiscard]] const ModelParams& params() const noexcept { return bank_.active(); }
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
