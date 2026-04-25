/**
 * @file McuComponentBase_uTest.cpp
 * @brief Unit tests for McuComponentBase.
 */

#include "src/system/core/infrastructure/system_component/mcu/inc/McuComponentBase.hpp"

#include <gtest/gtest.h>

/**
 * @brief Concrete test component for McuComponentBase testing.
 */
class TestLiteComponent : public system_core::system_component::mcu::McuComponentBase {
public:
  explicit TestLiteComponent(bool initSuccess = true) noexcept : initSuccess_(initSuccess) {}

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 42; }
  [[nodiscard]] const char* componentName() const noexcept override { return "TestLiteComponent"; }
  [[nodiscard]] system_core::system_component::ComponentType
  componentType() const noexcept override {
    return system_core::system_component::ComponentType::SW_MODEL;
  }
  [[nodiscard]] const char* label() const noexcept override { return "TEST_LITE"; }

  int doInitCallCount() const noexcept { return doInitCallCount_; }
  int doResetCallCount() const noexcept { return doResetCallCount_; }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    ++doInitCallCount_;
    if (!initSuccess_) {
      setLastError("init failed");
      return 1;
    }
    return 0;
  }

  void doReset() noexcept override { ++doResetCallCount_; }

private:
  bool initSuccess_;
  int doInitCallCount_{0};
  int doResetCallCount_{0};
};

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction sets expected initial state. */
TEST(McuComponentBase_DefaultConstruction, InitialStateIsCorrect) {
  TestLiteComponent comp;

  EXPECT_EQ(comp.componentId(), 42);
  EXPECT_STREQ(comp.componentName(), "TestLiteComponent");
  EXPECT_EQ(comp.componentType(), system_core::system_component::ComponentType::SW_MODEL);
  EXPECT_STREQ(comp.label(), "TEST_LITE");
  EXPECT_FALSE(comp.isInitialized());
  EXPECT_FALSE(comp.isRegistered());
  EXPECT_EQ(comp.status(), 0);
  EXPECT_EQ(comp.lastError(), nullptr);
  EXPECT_EQ(comp.instanceIndex(), 0);
  EXPECT_EQ(comp.fullUid(), 0xFFFFFFFF);
}

/* ----------------------------- McuComponentBase Method Tests ----------------------------- */

/** @test init() calls doInit() and sets initialized flag on success. */
TEST(McuComponentBase_Lifecycle, InitSuccessful) {
  TestLiteComponent comp;

  const std::uint8_t RESULT = comp.init();

  EXPECT_EQ(RESULT, 0);
  EXPECT_TRUE(comp.isInitialized());
  EXPECT_EQ(comp.doInitCallCount(), 1);
  EXPECT_EQ(comp.lastError(), nullptr);
}

/** @test init() is idempotent - second call returns success without calling doInit(). */
TEST(McuComponentBase_Lifecycle, InitIdempotent) {
  TestLiteComponent comp;

  (void)comp.init();
  const std::uint8_t RESULT = comp.init();

  EXPECT_EQ(RESULT, 0);
  EXPECT_TRUE(comp.isInitialized());
  EXPECT_EQ(comp.doInitCallCount(), 1); // Only called once
}

/** @test init() failure sets error state. */
TEST(McuComponentBase_Lifecycle, InitFailure) {
  TestLiteComponent comp(false); // Configured to fail

  const std::uint8_t RESULT = comp.init();

  EXPECT_NE(RESULT, 0);
  EXPECT_FALSE(comp.isInitialized());
  EXPECT_NE(comp.lastError(), nullptr);
}

/** @test reset() calls doReset() and clears state. */
TEST(McuComponentBase_Lifecycle, ResetClearsState) {
  TestLiteComponent comp;
  (void)comp.init();

  comp.reset();

  EXPECT_FALSE(comp.isInitialized());
  EXPECT_EQ(comp.status(), 0);
  EXPECT_EQ(comp.lastError(), nullptr);
  EXPECT_EQ(comp.doResetCallCount(), 1);
}

/** @test Component can be re-initialized after reset. */
TEST(McuComponentBase_Lifecycle, ReinitAfterReset) {
  TestLiteComponent comp;
  (void)comp.init();
  comp.reset();

  const std::uint8_t RESULT = comp.init();

  EXPECT_EQ(RESULT, 0);
  EXPECT_TRUE(comp.isInitialized());
  EXPECT_EQ(comp.doInitCallCount(), 2);
}

/** @test setInstanceIndex() computes fullUid correctly. */
TEST(McuComponentBase_Registration, SetInstanceIndex) {
  TestLiteComponent comp;

  comp.setInstanceIndex(3);

  EXPECT_TRUE(comp.isRegistered());
  EXPECT_EQ(comp.instanceIndex(), 3);
  // fullUid = (componentId << 8) | instanceIndex = (42 << 8) | 3 = 10752 + 3 = 10755
  EXPECT_EQ(comp.fullUid(), (42U << 8) | 3U);
}

/** @test Instance index 0 produces correct fullUid. */
TEST(McuComponentBase_Registration, InstanceIndexZero) {
  TestLiteComponent comp;

  comp.setInstanceIndex(0);

  EXPECT_TRUE(comp.isRegistered());
  EXPECT_EQ(comp.instanceIndex(), 0);
  EXPECT_EQ(comp.fullUid(), 42U << 8);
}

/** @test Maximum instance index (255) produces correct fullUid. */
TEST(McuComponentBase_Registration, MaxInstanceIndex) {
  TestLiteComponent comp;

  comp.setInstanceIndex(255);

  EXPECT_TRUE(comp.isRegistered());
  EXPECT_EQ(comp.instanceIndex(), 255);
  EXPECT_EQ(comp.fullUid(), (42U << 8) | 255U);
}

/* ----------------------------- API Tests ----------------------------- */

/** @test McuComponentBase satisfies IComponent interface. */
TEST(McuComponentBase_Interface, ImplementsIComponent) {
  TestLiteComponent comp;
  system_core::system_component::IComponent* iface = &comp;

  EXPECT_EQ(iface->componentId(), 42);
  EXPECT_STREQ(iface->componentName(), "TestLiteComponent");
  EXPECT_EQ(iface->componentType(), system_core::system_component::ComponentType::SW_MODEL);
  EXPECT_STREQ(iface->label(), "TEST_LITE");
  EXPECT_EQ(iface->init(), 0);
  EXPECT_TRUE(iface->isInitialized());
}
