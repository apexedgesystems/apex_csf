/**
 * @file IComponent_uTest.cpp
 * @brief Unit tests for IComponent interface and base types.
 */

#include "src/system/core/infrastructure/system_component/base/inc/ComponentType.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/IComponent.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"

#include <gtest/gtest.h>

using system_core::system_component::ComponentType;
using system_core::system_component::IComponent;
using system_core::system_component::Status;

/**
 * @brief Minimal concrete implementation for testing IComponent.
 *
 * Demonstrates that IComponent can be implemented with minimal overhead.
 */
class TestComponent final : public IComponent {
public:
  static constexpr std::uint16_t COMPONENT_ID = 999;
  static constexpr const char* COMPONENT_NAME = "TestComponent";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] ComponentType componentType() const noexcept override {
    return ComponentType::SUPPORT;
  }
  [[nodiscard]] const char* label() const noexcept override { return "TEST"; }

  [[nodiscard]] std::uint8_t init() noexcept override {
    if (initialized_) {
      return static_cast<std::uint8_t>(Status::ERROR_ALREADY_INITIALIZED);
    }
    initialized_ = true;
    status_ = static_cast<std::uint8_t>(Status::SUCCESS);
    return status_;
  }

  void reset() noexcept override {
    initialized_ = false;
    status_ = static_cast<std::uint8_t>(Status::SUCCESS);
  }

  [[nodiscard]] std::uint8_t status() const noexcept override { return status_; }
  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }
  [[nodiscard]] std::uint32_t fullUid() const noexcept override { return fullUid_; }
  [[nodiscard]] std::uint8_t instanceIndex() const noexcept override { return instanceIndex_; }
  [[nodiscard]] bool isRegistered() const noexcept override { return registered_; }

  void setInstanceIndex(std::uint8_t idx) noexcept {
    instanceIndex_ = idx;
    fullUid_ = (static_cast<std::uint32_t>(COMPONENT_ID) << 8) | idx;
    registered_ = true;
  }

private:
  std::uint8_t status_{static_cast<std::uint8_t>(Status::SUCCESS)};
  std::uint32_t fullUid_{0xFFFFFFFF};
  std::uint8_t instanceIndex_{0};
  bool initialized_{false};
  bool registered_{false};
};

/* ----------------------------- IComponent Method Tests ----------------------------- */

/** @test Verify IComponent can be instantiated via concrete implementation. */
TEST(IComponent, CanInstantiateConcrete) {
  TestComponent comp;
  EXPECT_EQ(comp.componentId(), 999);
  EXPECT_STREQ(comp.componentName(), "TestComponent");
}

/** @test Verify componentType returns expected value. */
TEST(IComponent, ComponentTypeReturnsExpected) {
  TestComponent comp;
  EXPECT_EQ(comp.componentType(), ComponentType::SUPPORT);
}

/** @test Verify label returns expected value. */
TEST(IComponent, LabelReturnsExpected) {
  TestComponent comp;
  EXPECT_STREQ(comp.label(), "TEST");
}

/** @test Verify init lifecycle. */
TEST(IComponent, InitLifecycle) {
  TestComponent comp;
  EXPECT_FALSE(comp.isInitialized());
  EXPECT_EQ(comp.status(), static_cast<std::uint8_t>(Status::SUCCESS));

  const auto RESULT = comp.init();
  EXPECT_EQ(RESULT, static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(comp.isInitialized());
}

/** @test Verify double init returns error. */
TEST(IComponent, DoubleInitReturnsError) {
  TestComponent comp;
  EXPECT_EQ(comp.init(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_EQ(comp.init(), static_cast<std::uint8_t>(Status::ERROR_ALREADY_INITIALIZED));
}

/** @test Verify reset clears initialized state. */
TEST(IComponent, ResetClearsInitialized) {
  TestComponent comp;
  EXPECT_EQ(comp.init(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(comp.isInitialized());

  comp.reset();
  EXPECT_FALSE(comp.isInitialized());
}

/** @test Verify registration sets fullUid and instanceIndex. */
TEST(IComponent, RegistrationSetsUid) {
  TestComponent comp;
  EXPECT_FALSE(comp.isRegistered());
  EXPECT_EQ(comp.fullUid(), 0xFFFFFFFF);

  comp.setInstanceIndex(2);
  EXPECT_TRUE(comp.isRegistered());
  EXPECT_EQ(comp.instanceIndex(), 2);
  EXPECT_EQ(comp.fullUid(), (999 << 8) | 2);
}

/** @test Verify IComponent pointer can hold concrete implementation. */
TEST(IComponent, PolymorphicAccess) {
  TestComponent concrete;
  IComponent* iface = &concrete;

  EXPECT_EQ(iface->componentId(), 999);
  EXPECT_STREQ(iface->componentName(), "TestComponent");
  EXPECT_EQ(iface->componentType(), ComponentType::SUPPORT);

  EXPECT_EQ(iface->init(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(iface->isInitialized());
}

/* ----------------------------- ComponentType Tests ----------------------------- */

/** @test Verify ComponentType toString for all values. */
TEST(ComponentType, ToStringAllValues) {
  using system_core::system_component::toString;

  EXPECT_STREQ(toString(ComponentType::EXECUTIVE), "EXECUTIVE");
  EXPECT_STREQ(toString(ComponentType::CORE), "CORE");
  EXPECT_STREQ(toString(ComponentType::SW_MODEL), "SW_MODEL");
  EXPECT_STREQ(toString(ComponentType::HW_MODEL), "HW_MODEL");
  EXPECT_STREQ(toString(ComponentType::SUPPORT), "SUPPORT");
  EXPECT_STREQ(toString(ComponentType::DRIVER), "DRIVER");
}

/** @test Verify logSubdir for component types. */
TEST(ComponentType, LogSubdirMapping) {
  using system_core::system_component::logSubdir;

  EXPECT_STREQ(logSubdir(ComponentType::EXECUTIVE), "core");
  EXPECT_STREQ(logSubdir(ComponentType::CORE), "core");
  EXPECT_STREQ(logSubdir(ComponentType::SW_MODEL), "models");
  EXPECT_STREQ(logSubdir(ComponentType::HW_MODEL), "models");
  EXPECT_STREQ(logSubdir(ComponentType::SUPPORT), "support");
  EXPECT_STREQ(logSubdir(ComponentType::DRIVER), "drivers");
}

/** @test Verify isModel helper. */
TEST(ComponentType, IsModelHelper) {
  using system_core::system_component::isModel;

  EXPECT_FALSE(isModel(ComponentType::EXECUTIVE));
  EXPECT_FALSE(isModel(ComponentType::CORE));
  EXPECT_TRUE(isModel(ComponentType::SW_MODEL));
  EXPECT_TRUE(isModel(ComponentType::HW_MODEL));
  EXPECT_FALSE(isModel(ComponentType::SUPPORT));
  EXPECT_FALSE(isModel(ComponentType::DRIVER));
}

/** @test Verify isCoreInfra helper. */
TEST(ComponentType, IsCoreInfraHelper) {
  using system_core::system_component::isCoreInfra;

  EXPECT_TRUE(isCoreInfra(ComponentType::EXECUTIVE));
  EXPECT_TRUE(isCoreInfra(ComponentType::CORE));
  EXPECT_FALSE(isCoreInfra(ComponentType::SW_MODEL));
  EXPECT_FALSE(isCoreInfra(ComponentType::HW_MODEL));
  EXPECT_FALSE(isCoreInfra(ComponentType::SUPPORT));
  EXPECT_FALSE(isCoreInfra(ComponentType::DRIVER));
}

/** @test Verify isSchedulable helper. */
TEST(ComponentType, IsSchedulableHelper) {
  using system_core::system_component::isSchedulable;

  EXPECT_FALSE(isSchedulable(ComponentType::EXECUTIVE));
  EXPECT_FALSE(isSchedulable(ComponentType::CORE));
  EXPECT_TRUE(isSchedulable(ComponentType::SW_MODEL));
  EXPECT_TRUE(isSchedulable(ComponentType::HW_MODEL));
  EXPECT_TRUE(isSchedulable(ComponentType::SUPPORT));
  EXPECT_TRUE(isSchedulable(ComponentType::DRIVER));
}

/* ----------------------------- Status Tests ----------------------------- */

/** @test Verify Status toString for all values. */
TEST(SystemComponentStatus, ToStringAllValues) {
  using system_core::system_component::toString;

  EXPECT_STREQ(toString(Status::SUCCESS), "SUCCESS");
  EXPECT_STREQ(toString(Status::WARN_NOOP), "WARN_NOOP");
  EXPECT_STREQ(toString(Status::ERROR_PARAM), "ERROR_PARAM");
  EXPECT_STREQ(toString(Status::ERROR_ALREADY_INITIALIZED), "ERROR_ALREADY_INITIALIZED");
  EXPECT_STREQ(toString(Status::ERROR_NOT_INITIALIZED), "ERROR_NOT_INITIALIZED");
  EXPECT_STREQ(toString(Status::ERROR_NOT_CONFIGURED), "ERROR_NOT_CONFIGURED");
  EXPECT_STREQ(toString(Status::ERROR_LOAD_INVALID), "ERROR_LOAD_INVALID");
  EXPECT_STREQ(toString(Status::ERROR_CONFIG_APPLY_FAIL), "ERROR_CONFIG_APPLY_FAIL");
}

/** @test Verify SUCCESS is zero. */
TEST(SystemComponentStatus, SuccessIsZero) {
  EXPECT_EQ(static_cast<std::uint8_t>(Status::SUCCESS), 0);
}
