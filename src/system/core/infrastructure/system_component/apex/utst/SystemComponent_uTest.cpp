/**
 * @file SystemComponent_uTest.cpp
 * @brief Unit tests for system_core::system_component::SystemComponent<TParams>.
 *
 * Notes:
 *  - Tests load/init/apply cycle, rollback, and file loading.
 *  - Uses a minimal MockComponent to exercise the templated class.
 *  - File-based tests use temporary files.
 */

#include "src/system/core/infrastructure/system_component/apex/inc/SystemComponent.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <type_traits>

using system_core::system_component::Status;
using system_core::system_component::SystemComponent;

/** @brief Minimal params POD used for tests. */
struct TestParams {
  std::int32_t value{0};
  std::uint32_t flags{0};
};
static_assert(sizeof(TestParams) == 8, "TestParams should be 8 bytes");

/** @brief Minimal derived that implements the required hooks. */
class MockComponent : public SystemComponent<TestParams> {
public:
  MockComponent() = default;

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 999; }
  [[nodiscard]] const char* componentName() const noexcept override { return "MockComponent"; }
  [[nodiscard]] const char* label() const noexcept override { return "TEST_COMPONENT"; }

  int appliedValue() const noexcept { return appliedValue_; }
  bool doInitCalled() const noexcept { return doInitCalled_; }

protected:
  [[nodiscard]] bool validateParams(const TestParams& p) const noexcept override {
    return p.value >= 0; // Negative values are invalid
  }

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    doInitCalled_ = true;
    appliedValue_ = activeParams().value;
    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

private:
  int appliedValue_{0};
  bool doInitCalled_{false};
};

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

/* ----------------------------- Default Construction ----------------------------- */

/** @test Initial state: not configured, not initialized. */
TEST(SystemComponentTest, InitialStateNotConfigured) {
  MockComponent c{};
  EXPECT_FALSE(c.isConfigured());
  EXPECT_FALSE(c.isInitialized());
  EXPECT_EQ(c.activeGeneration(), 0u);
  EXPECT_EQ(c.stagedGeneration(), 0u);
}

/** @test init() fails without load() (ERROR_NOT_CONFIGURED). */
TEST(SystemComponentTest, InitFailsWithoutLoad) {
  MockComponent c{};
  const std::uint8_t CODE = c.init();
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::ERROR_NOT_CONFIGURED));
  EXPECT_FALSE(c.isInitialized());
}

/* ----------------------------- SystemComponent Method Tests ----------------------------- */

/** @test load(struct): validation failure returns ERROR_LOAD_INVALID. */
TEST(SystemComponentTest, LoadStructValidationFailure) {
  MockComponent c{};
  const TestParams BAD{-1, 0}; // Negative value is invalid

  const std::uint8_t CODE = c.load(BAD);
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::ERROR_LOAD_INVALID));
  EXPECT_EQ(c.stagedGeneration(), 1u); // Staging attempt counted
  EXPECT_FALSE(c.isConfigured());      // Not configured on failure
}

/** @test load(struct): success marks configured and bumps stagedGeneration. */
TEST(SystemComponentTest, LoadStructSuccess) {
  MockComponent c{};
  const TestParams OK{42, 0xFF};

  const std::uint8_t CODE = c.load(OK);
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_EQ(c.stagedGeneration(), 1u);
  EXPECT_TRUE(c.isConfigured());
  EXPECT_EQ(c.stagedParams().value, 42);
  EXPECT_EQ(c.stagedParams().flags, 0xFF);
}

/** @test load(path): success reads binary into staged params. */
TEST(SystemComponentTest, LoadFileSuccess) {
  MockComponent c{};
  const TestParams EXPECTED{100, 0xDEAD};
  TempParamsFile tmpFile(EXPECTED);

  const std::uint8_t CODE = c.load(tmpFile.path());
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isConfigured());
  EXPECT_EQ(c.stagedParams().value, 100);
  EXPECT_EQ(c.stagedParams().flags, 0xDEAD);
}

/** @test load(path): nonexistent file returns ERROR_LOAD_INVALID. */
TEST(SystemComponentTest, LoadFileNotFound) {
  MockComponent c{};
  const std::filesystem::path BAD_PATH{"/nonexistent/path/to/file.bin"};

  const std::uint8_t CODE = c.load(BAD_PATH);
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::ERROR_LOAD_INVALID));
  EXPECT_FALSE(c.isConfigured());
}

/** @test load(path): validation failure after file read. */
TEST(SystemComponentTest, LoadFileValidationFailure) {
  MockComponent c{};
  const TestParams BAD{-5, 0}; // Negative = invalid
  TempParamsFile tmpFile(BAD);

  const std::uint8_t CODE = c.load(tmpFile.path());
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::ERROR_LOAD_INVALID));
  EXPECT_FALSE(c.isConfigured());
}

/** @test Full lifecycle: load → init → activeParams available. */
TEST(SystemComponentTest, FullLifecycleLoadInit) {
  MockComponent c{};
  const TestParams PARAMS{77, 0xBEEF};

  // Load
  ASSERT_EQ(c.load(PARAMS), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isConfigured());
  EXPECT_FALSE(c.isInitialized());

  // Init
  ASSERT_EQ(c.init(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isInitialized());
  EXPECT_TRUE(c.doInitCalled());
  EXPECT_EQ(c.appliedValue(), 77);
}

/** @test init() applies staged → active on first init. */
TEST(SystemComponentTest, InitAppliesStagedToActive) {
  MockComponent c{};
  const TestParams PARAMS{55, 0xCAFE};

  ASSERT_EQ(c.load(PARAMS), static_cast<std::uint8_t>(Status::SUCCESS));

  // Before init: active is default (0)
  EXPECT_EQ(c.activeParams().value, 0);

  ASSERT_EQ(c.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  // After init: component used staged params (via activeParams in doInit)
  EXPECT_EQ(c.appliedValue(), 55);
}

/** @test apply() fails if not configured. */
TEST(SystemComponentTest, ApplyFailsWithoutConfig) {
  MockComponent c{};
  const std::uint8_t CODE = c.apply();
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::ERROR_NOT_CONFIGURED));
}

/** @test apply() swaps staged → active without re-init. */
TEST(SystemComponentTest, ApplyHotReload) {
  MockComponent c{};
  const TestParams INITIAL{10, 0};
  const TestParams UPDATED{20, 0};

  // Initial load and init
  ASSERT_EQ(c.load(INITIAL), static_cast<std::uint8_t>(Status::SUCCESS));
  ASSERT_EQ(c.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  // Load new params (goes to staged bank)
  ASSERT_EQ(c.load(UPDATED), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_EQ(c.stagedParams().value, 20);

  // Apply (hot-reload)
  ASSERT_EQ(c.apply(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_EQ(c.activeParams().value, 20);
  EXPECT_EQ(c.activeGeneration(), 2u); // +1 for init, +1 for apply
}

/** @test canRollback() is false before any apply. */
TEST(SystemComponentTest, CanRollbackInitiallyFalse) {
  MockComponent c{};
  EXPECT_FALSE(c.canRollback());
}

/** @test rollback() returns WARN_NOOP before any apply. */
TEST(SystemComponentTest, RollbackNoopBeforeApply) {
  MockComponent c{};
  const TestParams PARAMS{5, 0};
  ASSERT_EQ(c.load(PARAMS), static_cast<std::uint8_t>(Status::SUCCESS));
  ASSERT_EQ(c.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  const std::uint8_t CODE = c.rollback();
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::WARN_NOOP));
}

/** @test rollback() restores previous active after apply. */
TEST(SystemComponentTest, RollbackRestoresPrevious) {
  MockComponent c{};
  const TestParams FIRST{10, 0};
  const TestParams SECOND{20, 0};

  // Load and init with first params
  ASSERT_EQ(c.load(FIRST), static_cast<std::uint8_t>(Status::SUCCESS));
  ASSERT_EQ(c.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  // Load and apply second params
  ASSERT_EQ(c.load(SECOND), static_cast<std::uint8_t>(Status::SUCCESS));
  ASSERT_EQ(c.apply(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_EQ(c.activeParams().value, 20);
  EXPECT_TRUE(c.canRollback());

  // Rollback
  ASSERT_EQ(c.rollback(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_EQ(c.activeParams().value, 10);
  EXPECT_FALSE(c.canRollback()); // Only one level of rollback
}

/** @test reset() clears initialized but preserves configured. */
TEST(SystemComponentTest, ResetClearsInitializedPreservesConfigured) {
  MockComponent c{};
  const TestParams PARAMS{30, 0};

  ASSERT_EQ(c.load(PARAMS), static_cast<std::uint8_t>(Status::SUCCESS));
  ASSERT_EQ(c.init(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isInitialized());
  EXPECT_TRUE(c.isConfigured());

  c.reset();
  EXPECT_FALSE(c.isInitialized());
  EXPECT_TRUE(c.isConfigured()); // Config preserved
}

/** @test reset() allows re-init with same params. */
TEST(SystemComponentTest, ResetAllowsReInit) {
  MockComponent c{};
  const TestParams PARAMS{40, 0};

  ASSERT_EQ(c.load(PARAMS), static_cast<std::uint8_t>(Status::SUCCESS));
  ASSERT_EQ(c.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  c.reset();
  ASSERT_EQ(c.init(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isInitialized());
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test noexcept contracts for public API. */
TEST(SystemComponentTest, NoexceptContracts) {
  static_assert(noexcept(std::declval<const MockComponent&>().activeParams()),
                "activeParams() should be noexcept");
  static_assert(noexcept(std::declval<const MockComponent&>().stagedParams()),
                "stagedParams() should be noexcept");
  static_assert(noexcept(std::declval<const MockComponent&>().activeGeneration()),
                "activeGeneration() should be noexcept");
  static_assert(noexcept(std::declval<const MockComponent&>().stagedGeneration()),
                "stagedGeneration() should be noexcept");
  static_assert(noexcept(std::declval<const MockComponent&>().canRollback()),
                "canRollback() should be noexcept");
  static_assert(noexcept(std::declval<MockComponent&>().load(std::declval<const TestParams&>())),
                "load(struct) should be noexcept");
  static_assert(noexcept(std::declval<MockComponent&>().apply()), "apply() should be noexcept");
  static_assert(noexcept(std::declval<MockComponent&>().rollback()),
                "rollback() should be noexcept");
  SUCCEED();
}

/* ----------------------------- Constants Tests ----------------------------- */

/** @test TParams must be trivially copyable (compile-time check). */
TEST(SystemComponentTest, TriviallyCopyableCheck) {
  static_assert(std::is_trivially_copyable_v<TestParams>, "TestParams must be trivially copyable");
  SUCCEED();
}
