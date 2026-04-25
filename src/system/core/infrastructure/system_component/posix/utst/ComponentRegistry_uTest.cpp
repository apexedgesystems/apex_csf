/**
 * @file ComponentRegistry_uTest.cpp
 * @brief Unit tests for ComponentRegistry collision detection.
 *
 * Verifies the registry accepts both POSIX (SystemComponentBase) and MCU
 * (McuComponentBase) components through the shared ComponentCore base.
 * This is the cross-tier registration property unlocked in Phase 4 of
 * the executive/component restructure.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/ComponentRegistry.hpp"

#include "src/system/core/infrastructure/system_component/mcu/inc/McuComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SystemComponentBase.hpp"

#include <gtest/gtest.h>

/* ----------------------------- Test Fixtures ----------------------------- */

/**
 * @brief Concrete POSIX-tier component for registry testing.
 */
class TestPosixComponent : public system_core::system_component::SystemComponentBase {
public:
  TestPosixComponent(std::uint16_t id, const char* name) noexcept : id_(id), name_(name) {
    setConfigured(true);
  }

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return id_; }
  [[nodiscard]] const char* componentName() const noexcept override { return name_; }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override { return 0; }

private:
  std::uint16_t id_;
  const char* name_;
};

/**
 * @brief Concrete MCU-tier component for registry testing.
 */
class TestMcuComponent : public system_core::system_component::mcu::McuComponentBase {
public:
  TestMcuComponent(std::uint16_t id, const char* name) noexcept : id_(id), name_(name) {}

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return id_; }
  [[nodiscard]] const char* componentName() const noexcept override { return name_; }
  [[nodiscard]] system_core::system_component::ComponentType
  componentType() const noexcept override {
    return system_core::system_component::ComponentType::CORE;
  }
  [[nodiscard]] const char* label() const noexcept override { return "TEST_MCU"; }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override { return 0; }

private:
  std::uint16_t id_;
  const char* name_;
};

/* ----------------------------- Cross-Tier Registration ----------------------------- */

/** @test Registry accepts a POSIX component and assigns instance index 0. */
TEST(ComponentRegistry_PosixTier, AcceptsSystemComponentBase) {
  system_core::system_component::ComponentRegistry registry;
  TestPosixComponent comp{100, "PosixComp"};
  std::uint8_t instanceIdx = 0xFF;

  EXPECT_TRUE(registry.registerComponent(&comp, instanceIdx));
  EXPECT_EQ(instanceIdx, 0u);
  EXPECT_TRUE(comp.isRegistered());
  EXPECT_EQ(comp.fullUid(), (100U << 8) | 0U);
}

/** @test Registry accepts an MCU component through the same call path. */
TEST(ComponentRegistry_McuTier, AcceptsMcuComponentBase) {
  system_core::system_component::ComponentRegistry registry;
  TestMcuComponent comp{200, "McuComp"};
  std::uint8_t instanceIdx = 0xFF;

  EXPECT_TRUE(registry.registerComponent(&comp, instanceIdx));
  EXPECT_EQ(instanceIdx, 0u);
  EXPECT_TRUE(comp.isRegistered());
  EXPECT_EQ(comp.fullUid(), (200U << 8) | 0U);
}

/** @test Registry handles a mixed-tier registration sequence. */
TEST(ComponentRegistry_MixedTier, AcceptsBothTiersInSequence) {
  system_core::system_component::ComponentRegistry registry;
  TestPosixComponent posixComp{100, "PosixComp"};
  TestMcuComponent mcuComp{200, "McuComp"};
  std::uint8_t idx = 0;

  EXPECT_TRUE(registry.registerComponent(&posixComp, idx));
  EXPECT_TRUE(registry.registerComponent(&mcuComp, idx));
  EXPECT_TRUE(posixComp.isRegistered());
  EXPECT_TRUE(mcuComp.isRegistered());
}

/* ----------------------------- Multi-Instance ----------------------------- */

/** @test Same id + same name = multi-instance with incrementing indices. */
TEST(ComponentRegistry_MultiInstance, AssignsIncreasingInstanceIndices) {
  system_core::system_component::ComponentRegistry registry;
  TestPosixComponent first{50, "Sensor"};
  TestPosixComponent second{50, "Sensor"};
  TestPosixComponent third{50, "Sensor"};
  std::uint8_t idx = 0;

  ASSERT_TRUE(registry.registerComponent(&first, idx));
  EXPECT_EQ(idx, 0u);
  ASSERT_TRUE(registry.registerComponent(&second, idx));
  EXPECT_EQ(idx, 1u);
  ASSERT_TRUE(registry.registerComponent(&third, idx));
  EXPECT_EQ(idx, 2u);

  EXPECT_EQ(first.instanceIndex(), 0u);
  EXPECT_EQ(second.instanceIndex(), 1u);
  EXPECT_EQ(third.instanceIndex(), 2u);
}

/** @test Multi-instance works across tiers when ids and names align. */
TEST(ComponentRegistry_MultiInstance, MixedTierSameIdSameName) {
  system_core::system_component::ComponentRegistry registry;
  TestPosixComponent posixSensor{50, "Sensor"};
  TestMcuComponent mcuSensor{50, "Sensor"};
  std::uint8_t idx = 0;

  ASSERT_TRUE(registry.registerComponent(&posixSensor, idx));
  EXPECT_EQ(idx, 0u);
  ASSERT_TRUE(registry.registerComponent(&mcuSensor, idx));
  EXPECT_EQ(idx, 1u);
}

/* ----------------------------- Collisions ----------------------------- */

/** @test Same id + different name triggers a collision and rejection. */
TEST(ComponentRegistry_Collision, RejectsSameIdDifferentName) {
  system_core::system_component::ComponentRegistry registry;
  TestPosixComponent first{75, "Imu"};
  TestPosixComponent second{75, "Magnetometer"};
  std::uint8_t idx = 0xFF;

  ASSERT_TRUE(registry.registerComponent(&first, idx));
  EXPECT_FALSE(registry.registerComponent(&second, idx));
  EXPECT_TRUE(first.isRegistered());
  EXPECT_FALSE(second.isRegistered());
}

/** @test Cross-tier collision (POSIX vs MCU same id, different names) is rejected. */
TEST(ComponentRegistry_Collision, RejectsCrossTierIdNameMismatch) {
  system_core::system_component::ComponentRegistry registry;
  TestPosixComponent posix{99, "PosixThing"};
  TestMcuComponent mcu{99, "McuThing"};
  std::uint8_t idx = 0xFF;

  ASSERT_TRUE(registry.registerComponent(&posix, idx));
  EXPECT_FALSE(registry.registerComponent(&mcu, idx));
}
