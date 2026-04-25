/**
 * @file ComponentCore_uTest.cpp
 * @brief Unit tests for ComponentCore (shared concrete IComponent base).
 *
 * Covers:
 *   - Default-constructed state
 *   - init() template method (idempotent, success, failure, hooks)
 *   - reset() and re-init
 *   - Registration / fullUid calculation
 *   - preInitCheck() and preInit() hook ordering
 *   - Polymorphic IComponent interface satisfaction
 */

#include "src/system/core/infrastructure/system_component/core/inc/ComponentCore.hpp"

#include <gtest/gtest.h>

/* ----------------------------- Test Fixtures ----------------------------- */

/**
 * @brief Concrete ComponentCore test subclass with hook instrumentation.
 */
class TestComponent : public system_core::system_component::ComponentCore {
public:
  TestComponent() noexcept = default;

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 42; }
  [[nodiscard]] const char* componentName() const noexcept override { return "TestComponent"; }
  [[nodiscard]] system_core::system_component::ComponentType
  componentType() const noexcept override {
    return system_core::system_component::ComponentType::SW_MODEL;
  }
  [[nodiscard]] const char* label() const noexcept override { return "TEST_CORE"; }

  // Hook controls.
  void setInitFails(bool v) noexcept { initFails_ = v; }
  void setPreInitCheckResult(std::uint8_t r) noexcept { preInitCheckResult_ = r; }

  // Hook call counters.
  int doInitCalls() const noexcept { return doInitCalls_; }
  int doResetCalls() const noexcept { return doResetCalls_; }
  int preInitCheckCalls() const noexcept { return preInitCheckCalls_; }
  int preInitCalls() const noexcept { return preInitCalls_; }

  // Order log (records each hook in the order it was invoked).
  const char* orderAt(std::size_t i) const noexcept {
    return i < orderLen_ ? order_[i] : nullptr;
  }
  std::size_t orderLen() const noexcept { return orderLen_; }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    record("doInit");
    ++doInitCalls_;
    if (initFails_) {
      setLastError("doInit failed");
      return 7; // Arbitrary non-zero status.
    }
    return 0;
  }

  void doReset() noexcept override {
    record("doReset");
    ++doResetCalls_;
  }

  [[nodiscard]] std::uint8_t preInitCheck() noexcept override {
    record("preInitCheck");
    ++preInitCheckCalls_;
    if (preInitCheckResult_ != 0) {
      setLastError("preInitCheck failed");
    }
    return preInitCheckResult_;
  }

  void preInit() noexcept override {
    record("preInit");
    ++preInitCalls_;
  }

private:
  void record(const char* hook) noexcept {
    if (orderLen_ < kOrderCap) {
      order_[orderLen_++] = hook;
    }
  }

  static constexpr std::size_t kOrderCap = 8;
  const char* order_[kOrderCap]{};
  std::size_t orderLen_{0};

  bool initFails_{false};
  std::uint8_t preInitCheckResult_{0};
  int doInitCalls_{0};
  int doResetCalls_{0};
  int preInitCheckCalls_{0};
  int preInitCalls_{0};
};

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed ComponentCore has expected initial state. */
TEST(ComponentCore_DefaultConstruction, InitialStateIsCorrect) {
  TestComponent comp;

  EXPECT_EQ(comp.componentId(), 42);
  EXPECT_STREQ(comp.componentName(), "TestComponent");
  EXPECT_EQ(comp.componentType(), system_core::system_component::ComponentType::SW_MODEL);
  EXPECT_STREQ(comp.label(), "TEST_CORE");
  EXPECT_FALSE(comp.isInitialized());
  EXPECT_FALSE(comp.isRegistered());
  EXPECT_EQ(comp.status(), 0);
  EXPECT_EQ(comp.lastError(), nullptr);
  EXPECT_EQ(comp.instanceIndex(), 0);
  EXPECT_EQ(comp.fullUid(), system_core::system_component::INVALID_COMPONENT_UID);
}

/** @test INVALID_COMPONENT_UID matches sentinel value. */
TEST(ComponentCore_DefaultConstruction, InvalidUidSentinel) {
  EXPECT_EQ(system_core::system_component::INVALID_COMPONENT_UID, 0xFFFFFFFFu);
}

/* ----------------------------- Lifecycle: init ----------------------------- */

/** @test init() calls hooks in order: preInitCheck -> preInit -> doInit. */
TEST(ComponentCore_Lifecycle, InitHookOrder) {
  TestComponent comp;

  const std::uint8_t RESULT = comp.init();

  EXPECT_EQ(RESULT, 0);
  ASSERT_EQ(comp.orderLen(), 3u);
  EXPECT_STREQ(comp.orderAt(0), "preInitCheck");
  EXPECT_STREQ(comp.orderAt(1), "preInit");
  EXPECT_STREQ(comp.orderAt(2), "doInit");
}

/** @test Successful init() flips initialized flag and clears lastError. */
TEST(ComponentCore_Lifecycle, InitSuccessClearsErrorAndSetsFlag) {
  TestComponent comp;

  const std::uint8_t RESULT = comp.init();

  EXPECT_EQ(RESULT, 0);
  EXPECT_TRUE(comp.isInitialized());
  EXPECT_EQ(comp.status(), 0);
  EXPECT_EQ(comp.lastError(), nullptr);
  EXPECT_EQ(comp.doInitCalls(), 1);
}

/** @test init() is idempotent: second call returns status without re-running hooks. */
TEST(ComponentCore_Lifecycle, InitIdempotent) {
  TestComponent comp;

  (void)comp.init();
  const std::uint8_t RESULT = comp.init();

  EXPECT_EQ(RESULT, 0);
  EXPECT_TRUE(comp.isInitialized());
  EXPECT_EQ(comp.doInitCalls(), 1);
  EXPECT_EQ(comp.preInitCheckCalls(), 1);
  EXPECT_EQ(comp.preInitCalls(), 1);
}

/** @test doInit() failure preserves error state and leaves initialized flag false. */
TEST(ComponentCore_Lifecycle, InitFailurePreservesError) {
  TestComponent comp;
  comp.setInitFails(true);

  const std::uint8_t RESULT = comp.init();

  EXPECT_EQ(RESULT, 7);
  EXPECT_EQ(comp.status(), 7);
  EXPECT_FALSE(comp.isInitialized());
  EXPECT_NE(comp.lastError(), nullptr);
  EXPECT_EQ(comp.doInitCalls(), 1);
}

/** @test preInitCheck() failure short-circuits init() before doInit() runs. */
TEST(ComponentCore_Lifecycle, PreInitCheckFailureShortCircuits) {
  TestComponent comp;
  comp.setPreInitCheckResult(9);

  const std::uint8_t RESULT = comp.init();

  EXPECT_EQ(RESULT, 9);
  EXPECT_EQ(comp.status(), 9);
  EXPECT_FALSE(comp.isInitialized());
  EXPECT_EQ(comp.preInitCheckCalls(), 1);
  EXPECT_EQ(comp.preInitCalls(), 0);
  EXPECT_EQ(comp.doInitCalls(), 0);
  EXPECT_NE(comp.lastError(), nullptr);
}

/** @test After preInitCheck failure, retrying with cleared check succeeds. */
TEST(ComponentCore_Lifecycle, RetryAfterPreInitCheckFailureSucceeds) {
  TestComponent comp;
  comp.setPreInitCheckResult(9);
  (void)comp.init();
  ASSERT_FALSE(comp.isInitialized());

  comp.setPreInitCheckResult(0);
  const std::uint8_t RESULT = comp.init();

  EXPECT_EQ(RESULT, 0);
  EXPECT_TRUE(comp.isInitialized());
  EXPECT_EQ(comp.doInitCalls(), 1);
}

/* ----------------------------- Lifecycle: reset ----------------------------- */

/** @test reset() calls doReset() and clears initialized flag and status. */
TEST(ComponentCore_Lifecycle, ResetClearsState) {
  TestComponent comp;
  (void)comp.init();
  ASSERT_TRUE(comp.isInitialized());

  comp.reset();

  EXPECT_FALSE(comp.isInitialized());
  EXPECT_EQ(comp.status(), 0);
  EXPECT_EQ(comp.lastError(), nullptr);
  EXPECT_EQ(comp.doResetCalls(), 1);
}

/** @test Component can be re-initialized after reset. */
TEST(ComponentCore_Lifecycle, ReinitAfterReset) {
  TestComponent comp;
  (void)comp.init();
  comp.reset();

  const std::uint8_t RESULT = comp.init();

  EXPECT_EQ(RESULT, 0);
  EXPECT_TRUE(comp.isInitialized());
  EXPECT_EQ(comp.doInitCalls(), 2);
  EXPECT_EQ(comp.doResetCalls(), 1);
}

/** @test reset() is safe even if init() never succeeded. */
TEST(ComponentCore_Lifecycle, ResetWithoutPriorInit) {
  TestComponent comp;

  comp.reset();

  EXPECT_FALSE(comp.isInitialized());
  EXPECT_EQ(comp.status(), 0);
  EXPECT_EQ(comp.doResetCalls(), 1);
}

/* ----------------------------- Registration ----------------------------- */

/** @test setInstanceIndex() computes fullUid as (componentId << 8) | instanceIdx. */
TEST(ComponentCore_Registration, SetInstanceIndexComputesFullUid) {
  TestComponent comp;

  comp.setInstanceIndex(3);

  EXPECT_TRUE(comp.isRegistered());
  EXPECT_EQ(comp.instanceIndex(), 3);
  EXPECT_EQ(comp.fullUid(), (42U << 8) | 3U);
}

/** @test Instance index 0 produces fullUid with low byte zero. */
TEST(ComponentCore_Registration, InstanceIndexZero) {
  TestComponent comp;

  comp.setInstanceIndex(0);

  EXPECT_TRUE(comp.isRegistered());
  EXPECT_EQ(comp.fullUid(), 42U << 8);
}

/** @test Maximum instance index (255) is preserved exactly. */
TEST(ComponentCore_Registration, MaxInstanceIndex) {
  TestComponent comp;

  comp.setInstanceIndex(255);

  EXPECT_EQ(comp.instanceIndex(), 255);
  EXPECT_EQ(comp.fullUid(), (42U << 8) | 255U);
}

/** @test Re-registering with a different index updates fullUid. */
TEST(ComponentCore_Registration, ReRegisterUpdatesUid) {
  TestComponent comp;

  comp.setInstanceIndex(1);
  comp.setInstanceIndex(2);

  EXPECT_EQ(comp.instanceIndex(), 2);
  EXPECT_EQ(comp.fullUid(), (42U << 8) | 2U);
}

/* ----------------------------- API Tests ----------------------------- */

/** @test ComponentCore satisfies IComponent interface polymorphically. */
TEST(ComponentCore_API, ImplementsIComponent) {
  TestComponent comp;
  system_core::system_component::IComponent* iface = &comp;

  EXPECT_EQ(iface->componentId(), 42);
  EXPECT_STREQ(iface->componentName(), "TestComponent");
  EXPECT_EQ(iface->componentType(), system_core::system_component::ComponentType::SW_MODEL);
  EXPECT_STREQ(iface->label(), "TEST_CORE");
  EXPECT_EQ(iface->init(), 0);
  EXPECT_TRUE(iface->isInitialized());
  EXPECT_EQ(iface->status(), 0);
}

/** @test fullUid returns sentinel before registration even after init. */
TEST(ComponentCore_API, FullUidUnsetUntilRegistration) {
  TestComponent comp;

  (void)comp.init();

  EXPECT_FALSE(comp.isRegistered());
  EXPECT_EQ(comp.fullUid(), system_core::system_component::INVALID_COMPONENT_UID);
}
