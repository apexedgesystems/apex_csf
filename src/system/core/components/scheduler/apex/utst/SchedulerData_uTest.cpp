/**
 * @file SchedulerData_uTest.cpp
 * @brief Unit tests for SchedulerData TPRM structures.
 *
 * Coverage:
 *   - SchedulerTprmHeader size and layout
 *   - SchedulerTaskEntry size and layout
 *   - schedulerTprmSize helper function
 *   - isUnsequenced helper function
 *   - Binary serialization round-trip
 */

#include "src/system/core/components/scheduler/apex/inc/SchedulerData.hpp"

#include <cstring>

#include <vector>

#include <gtest/gtest.h>

using system_core::scheduler::isUnsequenced;
using system_core::scheduler::NO_SEQUENCE_GROUP;
using system_core::scheduler::SchedulerTaskEntry;
using system_core::scheduler::SchedulerTprmHeader;
using system_core::scheduler::schedulerTprmSize;

/* ----------------------------- SchedulerTprmHeader Tests ----------------------------- */

/** @test Verifies header struct is exactly 3 bytes. */
TEST(SchedulerDataTest, HeaderSizeIs3Bytes) { EXPECT_EQ(sizeof(SchedulerTprmHeader), 3); }

/** @test Verifies header fields are at expected offsets. */
TEST(SchedulerDataTest, HeaderFieldOffsets) {
  SchedulerTprmHeader header{};
  auto* base = reinterpret_cast<std::uint8_t*>(&header);

  // Set each field and verify byte position
  header.numPools = 0xAA;
  header.workersPerPool = 0xBB;
  header.numTasks = 0xCC;

  EXPECT_EQ(base[0], 0xAA); // numPools at offset 0
  EXPECT_EQ(base[1], 0xBB); // workersPerPool at offset 1
  EXPECT_EQ(base[2], 0xCC); // numTasks at offset 2
}

/** @test Verifies header default construction. */
TEST(SchedulerDataTest, HeaderDefaultConstruction) {
  SchedulerTprmHeader header{};
  EXPECT_EQ(header.numPools, 0);
  EXPECT_EQ(header.workersPerPool, 0);
  EXPECT_EQ(header.numTasks, 0);
}

/** @test Verifies header aggregate initialization. */
TEST(SchedulerDataTest, HeaderAggregateInit) {
  SchedulerTprmHeader header{2, 8, 6};
  EXPECT_EQ(header.numPools, 2);
  EXPECT_EQ(header.workersPerPool, 8);
  EXPECT_EQ(header.numTasks, 6);
}

/* ----------------------------- SchedulerTaskEntry Tests ----------------------------- */

/** @test Verifies task entry struct is exactly 15 bytes. */
TEST(SchedulerDataTest, TaskEntrySizeIs15Bytes) { EXPECT_EQ(sizeof(SchedulerTaskEntry), 15); }

/** @test Verifies task entry fields are at expected offsets. */
TEST(SchedulerDataTest, TaskEntryFieldOffsets) {
  SchedulerTaskEntry entry{};
  auto* base = reinterpret_cast<std::uint8_t*>(&entry);

  // Set fields with identifiable values
  entry.fullUid = 0x01020304;
  entry.taskUid = 0x05;
  entry.poolIndex = 0x06;
  entry.freqN = 0x0708;
  entry.freqD = 0x090A;
  entry.offset = 0x0B0C;
  entry.priority = 0x0D;
  entry.sequenceGroup = 0x0E;
  entry.sequencePhase = 0x0F;

  // Verify little-endian layout
  EXPECT_EQ(base[0], 0x04);  // fullUid byte 0 (lowest)
  EXPECT_EQ(base[1], 0x03);  // fullUid byte 1
  EXPECT_EQ(base[2], 0x02);  // fullUid byte 2
  EXPECT_EQ(base[3], 0x01);  // fullUid byte 3 (highest)
  EXPECT_EQ(base[4], 0x05);  // taskUid
  EXPECT_EQ(base[5], 0x06);  // poolIndex
  EXPECT_EQ(base[6], 0x08);  // freqN low byte
  EXPECT_EQ(base[7], 0x07);  // freqN high byte
  EXPECT_EQ(base[8], 0x0A);  // freqD low byte
  EXPECT_EQ(base[9], 0x09);  // freqD high byte
  EXPECT_EQ(base[10], 0x0C); // offset low byte
  EXPECT_EQ(base[11], 0x0B); // offset high byte
  EXPECT_EQ(base[12], 0x0D); // priority
  EXPECT_EQ(base[13], 0x0E); // sequenceGroup
  EXPECT_EQ(base[14], 0x0F); // sequencePhase
}

/** @test Verifies task entry default construction. */
TEST(SchedulerDataTest, TaskEntryDefaultConstruction) {
  SchedulerTaskEntry entry{};
  EXPECT_EQ(entry.fullUid, 0u);
  EXPECT_EQ(entry.taskUid, 0);
  EXPECT_EQ(entry.poolIndex, 0);
  EXPECT_EQ(entry.freqN, 0);
  EXPECT_EQ(entry.freqD, 0);
  EXPECT_EQ(entry.offset, 0);
  EXPECT_EQ(entry.priority, 0);
  EXPECT_EQ(entry.sequenceGroup, 0);
  EXPECT_EQ(entry.sequencePhase, 0);
}

/** @test Verifies task entry aggregate initialization. */
TEST(SchedulerDataTest, TaskEntryAggregateInit) {
  SchedulerTaskEntry entry{0x00006500, 1, 0, 5, 1, 10, 63, 0, 1};
  EXPECT_EQ(entry.fullUid, 0x00006500u);
  EXPECT_EQ(entry.taskUid, 1);
  EXPECT_EQ(entry.poolIndex, 0);
  EXPECT_EQ(entry.freqN, 5);
  EXPECT_EQ(entry.freqD, 1);
  EXPECT_EQ(entry.offset, 10);
  EXPECT_EQ(entry.priority, 63);
  EXPECT_EQ(entry.sequenceGroup, 0);
  EXPECT_EQ(entry.sequencePhase, 1);
}

/** @test Verifies negative priority values. */
TEST(SchedulerDataTest, TaskEntryNegativePriority) {
  SchedulerTaskEntry entry{};
  entry.priority = -64;
  EXPECT_EQ(entry.priority, -64);

  entry.priority = -128;
  EXPECT_EQ(entry.priority, -128);
}

/* ----------------------------- Helper Function Tests ----------------------------- */

/** @test Verifies schedulerTprmSize calculation. */
TEST(SchedulerDataTest, SchedulerTprmSizeCalculation) {
  // 0 tasks: just header
  EXPECT_EQ(schedulerTprmSize(0), 3);

  // 1 task: header + 1 entry
  EXPECT_EQ(schedulerTprmSize(1), 3 + 15);

  // 6 tasks (our demo): header + 6 entries
  EXPECT_EQ(schedulerTprmSize(6), 3 + 6 * 15);

  // Max tasks (255): header + 255 entries
  EXPECT_EQ(schedulerTprmSize(255), 3 + 255 * 15);
}

/** @test Verifies isUnsequenced helper. */
TEST(SchedulerDataTest, IsUnsequencedHelper) {
  EXPECT_TRUE(isUnsequenced(NO_SEQUENCE_GROUP));
  EXPECT_TRUE(isUnsequenced(0xFF));
  EXPECT_FALSE(isUnsequenced(0));
  EXPECT_FALSE(isUnsequenced(1));
  EXPECT_FALSE(isUnsequenced(254));
}

/** @test Verifies NO_SEQUENCE_GROUP constant. */
TEST(SchedulerDataTest, NoSequenceGroupConstant) { EXPECT_EQ(NO_SEQUENCE_GROUP, 0xFF); }

/** @test Verifies extractComponentId helper. */
TEST(SchedulerDataTest, ExtractComponentId) {
  using system_core::scheduler::extractComponentId;

  // componentId=101, instance=0 -> fullUid=0x00006500
  EXPECT_EQ(extractComponentId(0x00006500), 101);

  // componentId=102, instance=5 -> fullUid=0x00006605
  EXPECT_EQ(extractComponentId(0x00006605), 102);

  // componentId=0 (executive), instance=0
  EXPECT_EQ(extractComponentId(0x00000000), 0);

  // componentId=1 (scheduler), instance=0
  EXPECT_EQ(extractComponentId(0x00000100), 1);

  // Max componentId=65535, instance=0
  EXPECT_EQ(extractComponentId(0x00FFFF00), 65535);
}

/** @test Verifies extractInstanceIndex helper. */
TEST(SchedulerDataTest, ExtractInstanceIndex) {
  using system_core::scheduler::extractInstanceIndex;

  // componentId=101, instance=0
  EXPECT_EQ(extractInstanceIndex(0x00006500), 0);

  // componentId=102, instance=5
  EXPECT_EQ(extractInstanceIndex(0x00006605), 5);

  // componentId=101, instance=255 (max)
  EXPECT_EQ(extractInstanceIndex(0x000065FF), 255);
}

/** @test Verifies composeFullUid helper. */
TEST(SchedulerDataTest, ComposeFullUid) {
  using system_core::scheduler::composeFullUid;

  // componentId=101, instance=0
  EXPECT_EQ(composeFullUid(101, 0), 0x00006500u);

  // componentId=102, instance=5
  EXPECT_EQ(composeFullUid(102, 5), 0x00006605u);

  // Executive: componentId=0, instance=0
  EXPECT_EQ(composeFullUid(0, 0), 0x00000000u);

  // Scheduler: componentId=1, instance=0
  EXPECT_EQ(composeFullUid(1, 0), 0x00000100u);

  // Max values
  EXPECT_EQ(composeFullUid(65535, 255), 0x00FFFFFFu);
}

/** @test Verifies round-trip extract/compose. */
TEST(SchedulerDataTest, ExtractComposeRoundTrip) {
  using system_core::scheduler::composeFullUid;
  using system_core::scheduler::extractComponentId;
  using system_core::scheduler::extractInstanceIndex;

  // Test round-trip for various values
  const std::uint16_t componentId = 101;
  const std::uint8_t instanceIndex = 7;

  const std::uint32_t fullUid = composeFullUid(componentId, instanceIndex);
  EXPECT_EQ(extractComponentId(fullUid), componentId);
  EXPECT_EQ(extractInstanceIndex(fullUid), instanceIndex);
}

/* ----------------------------- Binary Serialization Tests ----------------------------- */

/** @test Verifies binary round-trip for header. */
TEST(SchedulerDataTest, HeaderBinaryRoundTrip) {
  SchedulerTprmHeader original{4, 16, 10};

  // Serialize to bytes
  std::vector<std::uint8_t> buffer(sizeof(SchedulerTprmHeader));
  std::memcpy(buffer.data(), &original, sizeof(original));

  // Deserialize from bytes
  SchedulerTprmHeader restored{};
  std::memcpy(&restored, buffer.data(), sizeof(restored));

  EXPECT_EQ(restored.numPools, original.numPools);
  EXPECT_EQ(restored.workersPerPool, original.workersPerPool);
  EXPECT_EQ(restored.numTasks, original.numTasks);
}

/** @test Verifies binary round-trip for task entry. */
TEST(SchedulerDataTest, TaskEntryBinaryRoundTrip) {
  SchedulerTaskEntry original{0x00006600, 5, 1, 10, 2, 25, -32, 0, 3};

  // Serialize to bytes
  std::vector<std::uint8_t> buffer(sizeof(SchedulerTaskEntry));
  std::memcpy(buffer.data(), &original, sizeof(original));

  // Deserialize from bytes
  SchedulerTaskEntry restored{};
  std::memcpy(&restored, buffer.data(), sizeof(restored));

  EXPECT_EQ(restored.fullUid, original.fullUid);
  EXPECT_EQ(restored.taskUid, original.taskUid);
  EXPECT_EQ(restored.poolIndex, original.poolIndex);
  EXPECT_EQ(restored.freqN, original.freqN);
  EXPECT_EQ(restored.freqD, original.freqD);
  EXPECT_EQ(restored.offset, original.offset);
  EXPECT_EQ(restored.priority, original.priority);
  EXPECT_EQ(restored.sequenceGroup, original.sequenceGroup);
  EXPECT_EQ(restored.sequencePhase, original.sequencePhase);
}

/** @test Verifies full tprm buffer layout with header + entries. */
TEST(SchedulerDataTest, FullTprmBufferLayout) {
  // Create a buffer representing a complete tprm
  const std::uint8_t numTasks = 2;
  const std::size_t totalSize = schedulerTprmSize(numTasks);
  std::vector<std::uint8_t> buffer(totalSize);

  // Write header
  SchedulerTprmHeader header{1, 8, numTasks};
  std::memcpy(buffer.data(), &header, sizeof(header));

  // Write task entries (fullUid = componentId << 8 | instanceIndex)
  SchedulerTaskEntry task1{0x00006500, 1, 0, 5, 1, 1, 63, 0, 1}; // componentId=101, instance=0
  SchedulerTaskEntry task2{0x00006600,        1, 0, 10, 1, 5, 0,
                           NO_SEQUENCE_GROUP, 0}; // componentId=102, instance=0

  std::memcpy(buffer.data() + sizeof(header), &task1, sizeof(task1));
  std::memcpy(buffer.data() + sizeof(header) + sizeof(task1), &task2, sizeof(task2));

  // Read back and verify
  auto* readHeader = reinterpret_cast<SchedulerTprmHeader*>(buffer.data());
  EXPECT_EQ(readHeader->numPools, 1);
  EXPECT_EQ(readHeader->workersPerPool, 8);
  EXPECT_EQ(readHeader->numTasks, 2);

  auto* entries = reinterpret_cast<SchedulerTaskEntry*>(buffer.data() + sizeof(header));
  EXPECT_EQ(entries[0].fullUid, 0x00006500u);
  EXPECT_EQ(entries[0].sequenceGroup, 0);
  EXPECT_FALSE(isUnsequenced(entries[0].sequenceGroup));

  EXPECT_EQ(entries[1].fullUid, 0x00006600u);
  EXPECT_EQ(entries[1].sequenceGroup, NO_SEQUENCE_GROUP);
  EXPECT_TRUE(isUnsequenced(entries[1].sequenceGroup));
}
