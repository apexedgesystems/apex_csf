/**
 * @file SystemComponentBase_uTest.cpp
 * @brief Unit tests for system_core::system_component::SystemComponentBase.
 *
 * Notes:
 *  - Tests are platform-agnostic.
 *  - Uses a minimal DummyComponent to exercise the base class.
 *  - Tests template method pattern: init() → doInit().
 *  - Tests configuration requirement: init() fails without setConfigured(true).
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SystemComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"

#include <gtest/gtest.h>

#include <memory>

using system_core::system_component::Status;
using system_core::system_component::SystemComponentBase;

/** @brief Minimal derived to exercise init + accessors. */
class DummyComponent : public SystemComponentBase {
public:
  DummyComponent() noexcept = default;

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 999; }
  [[nodiscard]] const char* componentName() const noexcept override { return "DummyComponent"; }
  [[nodiscard]] const char* label() const noexcept override { return "DUMMY"; }

  /** @brief Mark as configured for testing (exposes protected method). */
  void configure() noexcept { setConfigured(true); }

  /** @brief Track if doInit was called. */
  bool doInitCalled() const noexcept { return doInitCalled_; }

  /** @brief Track if doReset was called. */
  bool doResetCalled() const noexcept { return doResetCalled_; }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    doInitCalled_ = true;
    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

  void doReset() noexcept override { doResetCalled_ = true; }

private:
  bool doInitCalled_{false};
  bool doResetCalled_{false};
};

/** @brief Component that fails during doInit. */
class FailingComponent : public SystemComponentBase {
public:
  FailingComponent() noexcept = default;

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 998; }
  [[nodiscard]] const char* componentName() const noexcept override { return "FailingComponent"; }
  [[nodiscard]] const char* label() const noexcept override { return "FAILING"; }

  void configure() noexcept { setConfigured(true); }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    setLastError("init failed for test");
    return static_cast<std::uint8_t>(Status::ERROR_PARAM);
  }
};

/* ----------------------------- Default Construction ----------------------------- */

/** @test Defaults before init() are sane. */
TEST(SystemComponentBaseTest, DefaultsBeforeInit) {
  DummyComponent c{};
  EXPECT_FALSE(c.isInitialized());
  EXPECT_FALSE(c.isConfigured());
  EXPECT_EQ(c.status(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_STREQ(c.label(), "DUMMY");
  EXPECT_EQ(c.lastError(), nullptr);
}

/* ----------------------------- SystemComponentBase Method Tests ----------------------------- */

/** @test init() fails with ERROR_NOT_CONFIGURED when not configured. */
TEST(SystemComponentBaseTest, InitFailsWithoutConfiguration) {
  DummyComponent c{};
  EXPECT_FALSE(c.isConfigured());

  const std::uint8_t CODE = c.init();
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::ERROR_NOT_CONFIGURED));
  EXPECT_FALSE(c.isInitialized());
  EXPECT_FALSE(c.doInitCalled());    // doInit should not be called
  EXPECT_NE(c.lastError(), nullptr); // Should have error context
}

/** @test isConfigured() returns true after setConfigured(true). */
TEST(SystemComponentBaseTest, SetConfiguredMarksConfigured) {
  DummyComponent c{};
  EXPECT_FALSE(c.isConfigured());

  c.configure();
  EXPECT_TRUE(c.isConfigured());
}

/** @test init() succeeds when configured and calls doInit(). */
TEST(SystemComponentBaseTest, InitSucceedsWhenConfigured) {
  DummyComponent c{};
  c.configure();
  EXPECT_TRUE(c.isConfigured());

  const std::uint8_t CODE = c.init();
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isInitialized());
  EXPECT_TRUE(c.doInitCalled());
  EXPECT_EQ(c.lastError(), nullptr); // No error on success
}

/** @test init() is idempotent (second call returns SUCCESS without re-init). */
TEST(SystemComponentBaseTest, InitIsIdempotent) {
  DummyComponent c{};
  c.configure();

  EXPECT_EQ(c.init(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isInitialized());

  // Second call should return SUCCESS without calling doInit again
  const std::uint8_t CODE = c.init();
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isInitialized());
}

/** @test init() propagates failure status from doInit(). */
TEST(SystemComponentBaseTest, InitPropagatesDoInitFailure) {
  FailingComponent c{};
  c.configure();

  const std::uint8_t CODE = c.init();
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::ERROR_PARAM));
  EXPECT_FALSE(c.isInitialized());   // Should not be marked initialized on failure
  EXPECT_NE(c.lastError(), nullptr); // Should have error context
}

/** @test reset() clears initialized flag and calls doReset(). */
TEST(SystemComponentBaseTest, ResetClearsInitialized) {
  DummyComponent c{};
  c.configure();
  ASSERT_EQ(c.init(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isInitialized());

  c.reset();
  EXPECT_FALSE(c.isInitialized());
  EXPECT_TRUE(c.doResetCalled());
  EXPECT_TRUE(c.isConfigured());     // Configuration preserved
  EXPECT_EQ(c.lastError(), nullptr); // Error context cleared
}

/** @test reset() allows re-initialization. */
TEST(SystemComponentBaseTest, ResetAllowsReInit) {
  DummyComponent c{};
  c.configure();
  ASSERT_EQ(c.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  c.reset();
  EXPECT_FALSE(c.isInitialized());

  // Should be able to init again
  const std::uint8_t CODE = c.init();
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isInitialized());
}

/** @test reset() resets status to SUCCESS. */
TEST(SystemComponentBaseTest, ResetResetsStatus) {
  FailingComponent c{};
  c.configure();
  ASSERT_EQ(c.init(), static_cast<std::uint8_t>(Status::ERROR_PARAM));

  c.reset();
  EXPECT_EQ(c.status(), static_cast<std::uint8_t>(Status::SUCCESS));
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test noexcept contracts for public API. */
TEST(SystemComponentBaseTest, NoexceptContracts) {
  static_assert(noexcept(std::declval<const DummyComponent&>().status()),
                "status() should be noexcept");
  static_assert(noexcept(std::declval<const DummyComponent&>().isInitialized()),
                "isInitialized() should be noexcept");
  static_assert(noexcept(std::declval<const DummyComponent&>().isConfigured()),
                "isConfigured() should be noexcept");
  static_assert(noexcept(std::declval<const DummyComponent&>().label()),
                "label() should be noexcept");
  static_assert(noexcept(std::declval<const DummyComponent&>().lastError()),
                "lastError() should be noexcept");
  static_assert(noexcept(std::declval<DummyComponent&>().init()), "init() should be noexcept");
  static_assert(noexcept(std::declval<DummyComponent&>().reset()), "reset() should be noexcept");
  SUCCEED();
}
