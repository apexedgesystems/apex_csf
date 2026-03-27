/**
 * @file RegistryData_uTest.cpp
 * @brief Unit tests for registry entry structures.
 *
 * Tests DataEntry, TaskEntry, and ComponentEntry structures including:
 *  - Default construction
 *  - Validity checks
 *  - DataEntry byte access methods
 */

#include "src/system/core/components/registry/apex/inc/RegistryData.hpp"

#include <cstdint>

#include <array>

#include <gtest/gtest.h>

using system_core::data::DataCategory;
using system_core::registry::ComponentEntry;
using system_core::registry::DataEntry;
using system_core::registry::TaskEntry;

/* ----------------------------- DataEntry Tests ----------------------------- */

/** @test Default-constructed DataEntry has zeroed fields. */
TEST(DataEntryTest, DefaultConstruction) {
  DataEntry entry{};
  EXPECT_EQ(entry.fullUid, 0u);
  EXPECT_EQ(entry.category, DataCategory::STATIC_PARAM);
  EXPECT_EQ(entry.name, nullptr);
  EXPECT_EQ(entry.dataPtr, nullptr);
  EXPECT_EQ(entry.size, 0u);
}

/** @test isValid returns false for default-constructed entry. */
TEST(DataEntryTest, InvalidByDefault) {
  DataEntry entry{};
  EXPECT_FALSE(entry.isValid());
}

/** @test isValid returns true when dataPtr and size are set. */
TEST(DataEntryTest, ValidWithData) {
  std::array<std::uint8_t, 16> buffer{};
  DataEntry entry{};
  entry.dataPtr = buffer.data();
  entry.size = buffer.size();
  EXPECT_TRUE(entry.isValid());
}

/** @test isValid returns false when size is zero even with valid pointer. */
TEST(DataEntryTest, InvalidWithZeroSize) {
  std::array<std::uint8_t, 16> buffer{};
  DataEntry entry{};
  entry.dataPtr = buffer.data();
  entry.size = 0;
  EXPECT_FALSE(entry.isValid());
}

/** @test getBytes returns empty span for null pointer. */
TEST(DataEntryTest, GetBytesNullPointer) {
  DataEntry entry{};
  entry.size = 16;
  auto bytes = entry.getBytes();
  EXPECT_TRUE(bytes.empty());
}

/** @test getBytes returns empty span for zero size. */
TEST(DataEntryTest, GetBytesZeroSize) {
  std::array<std::uint8_t, 16> buffer{};
  DataEntry entry{};
  entry.dataPtr = buffer.data();
  entry.size = 0;
  auto bytes = entry.getBytes();
  EXPECT_TRUE(bytes.empty());
}

/** @test getBytes returns all bytes of the data block. */
TEST(DataEntryTest, GetBytesAll) {
  std::array<std::uint8_t, 4> buffer = {0x11, 0x22, 0x33, 0x44};
  DataEntry entry{};
  entry.dataPtr = buffer.data();
  entry.size = buffer.size();

  auto bytes = entry.getBytes();
  EXPECT_EQ(bytes.size(), 4u);
  EXPECT_EQ(bytes[0], 0x11);
  EXPECT_EQ(bytes[1], 0x22);
  EXPECT_EQ(bytes[2], 0x33);
  EXPECT_EQ(bytes[3], 0x44);
}

/** @test getBytes with offset and length returns correct slice. */
TEST(DataEntryTest, GetBytesSlice) {
  std::array<std::uint8_t, 8> buffer = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
  DataEntry entry{};
  entry.dataPtr = buffer.data();
  entry.size = buffer.size();

  auto bytes = entry.getBytes(2, 3);
  EXPECT_EQ(bytes.size(), 3u);
  EXPECT_EQ(bytes[0], 0x22);
  EXPECT_EQ(bytes[1], 0x33);
  EXPECT_EQ(bytes[2], 0x44);
}

/** @test getBytes with out-of-bounds offset returns empty span. */
TEST(DataEntryTest, GetBytesOffsetOutOfBounds) {
  std::array<std::uint8_t, 4> buffer{};
  DataEntry entry{};
  entry.dataPtr = buffer.data();
  entry.size = buffer.size();

  auto bytes = entry.getBytes(5, 1);
  EXPECT_TRUE(bytes.empty());
}

/** @test getBytes with length exceeding remaining returns empty span. */
TEST(DataEntryTest, GetBytesLengthOutOfBounds) {
  std::array<std::uint8_t, 4> buffer{};
  DataEntry entry{};
  entry.dataPtr = buffer.data();
  entry.size = buffer.size();

  auto bytes = entry.getBytes(2, 5);
  EXPECT_TRUE(bytes.empty());
}

/* ----------------------------- TaskEntry Tests ----------------------------- */

/** @test Default-constructed TaskEntry has zeroed fields. */
TEST(TaskEntryTest, DefaultConstruction) {
  TaskEntry entry{};
  EXPECT_EQ(entry.fullUid, 0u);
  EXPECT_EQ(entry.taskUid, 0u);
  EXPECT_EQ(entry.name, nullptr);
  EXPECT_EQ(entry.task, nullptr);
}

/** @test isValid returns false for default-constructed entry. */
TEST(TaskEntryTest, InvalidByDefault) {
  TaskEntry entry{};
  EXPECT_FALSE(entry.isValid());
}

/* ----------------------------- ComponentEntry Tests ----------------------------- */

/** @test Default-constructed ComponentEntry has zeroed fields. */
TEST(ComponentEntryTest, DefaultConstruction) {
  ComponentEntry entry{};
  EXPECT_EQ(entry.fullUid, 0u);
  EXPECT_EQ(entry.name, nullptr);
  EXPECT_EQ(entry.taskCount, 0u);
  EXPECT_EQ(entry.dataCount, 0u);
}

/** @test isValid returns false for default-constructed entry. */
TEST(ComponentEntryTest, InvalidByDefault) {
  ComponentEntry entry{};
  EXPECT_FALSE(entry.isValid());
}

/** @test isValid returns true when name is set. */
TEST(ComponentEntryTest, ValidWithName) {
  ComponentEntry entry{};
  entry.name = "TestComponent";
  EXPECT_TRUE(entry.isValid());
}

/* ----------------------------- Constants Tests ----------------------------- */

/** @test Registry capacity constants are reasonable. */
TEST(RegistryConstantsTest, CapacityValues) {
  EXPECT_GT(system_core::registry::MAX_COMPONENTS, 0u);
  EXPECT_GT(system_core::registry::MAX_TASKS, 0u);
  EXPECT_GT(system_core::registry::MAX_DATA_ENTRIES, 0u);
  EXPECT_GT(system_core::registry::MAX_TASKS_PER_COMPONENT, 0u);
  EXPECT_GT(system_core::registry::MAX_DATA_PER_COMPONENT, 0u);
}
