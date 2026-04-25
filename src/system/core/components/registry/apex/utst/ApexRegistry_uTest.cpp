/**
 * @file ApexRegistry_uTest.cpp
 * @brief Unit tests for ApexRegistry unified component/task/data registry.
 *
 * Tests cover:
 *  - Component registration and queries
 *  - Task registration and queries
 *  - Data registration and queries
 *  - Freeze semantics (RT-safety boundary)
 *  - Error handling (duplicates, capacity, etc.)
 */

#include "src/system/core/components/registry/apex/inc/ApexRegistry.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/TaskBuilder.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/IComponentResolver.hpp"

#include <cstdint>

#include <array>
#include <string_view>

#include <gtest/gtest.h>

using system_core::data::DataCategory;
using system_core::registry::ApexRegistry;
using system_core::registry::ComponentEntry;
using system_core::registry::DataEntry;
using system_core::registry::Status;
using system_core::registry::TaskEntry;
using system_core::schedulable::bindFreeFunction;
using system_core::schedulable::SchedulableTask;

/* ----------------------------- Test Fixtures ----------------------------- */

namespace {

/** No-op task function for testing. */
std::uint8_t noopTaskFn() { return 0; }

/** Create a simple task for testing. */
SchedulableTask createTestTask(std::string_view label) {
  auto delegate = bindFreeFunction(&noopTaskFn);
  return SchedulableTask(delegate, label);
}

/** Test data structure. */
struct TestData {
  std::uint32_t field1{0};
  std::uint32_t field2{0};
  std::array<std::uint8_t, 8> buffer{};
};

} // namespace

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed registry is not frozen. */
TEST(ApexRegistryTest, DefaultNotFrozen) {
  ApexRegistry registry;
  EXPECT_FALSE(registry.isFrozen());
}

/** @test Default-constructed registry has zero entries. */
TEST(ApexRegistryTest, DefaultEmpty) {
  ApexRegistry registry;
  EXPECT_EQ(registry.componentCount(), 0u);
  EXPECT_EQ(registry.taskCount(), 0u);
  EXPECT_EQ(registry.dataCount(), 0u);
}

/** @test Registry has correct component identity. */
TEST(ApexRegistryTest, ComponentIdentity) {
  ApexRegistry registry;
  EXPECT_EQ(registry.componentId(), ApexRegistry::COMPONENT_ID);
  EXPECT_EQ(registry.componentId(), 3u);
  EXPECT_STREQ(registry.componentName(), ApexRegistry::COMPONENT_NAME);
  EXPECT_STREQ(registry.componentName(), "Registry");
  EXPECT_STREQ(registry.label(), "REGISTRY");
}

/* ----------------------------- Component Registration ----------------------------- */

/** @test Register single component succeeds. */
TEST(ApexRegistryTest, RegisterComponentSuccess) {
  ApexRegistry registry;
  auto status = registry.registerComponent(0x6600, "TestModel");
  EXPECT_EQ(status, Status::SUCCESS);
  EXPECT_EQ(registry.componentCount(), 1u);
}

/** @test Register component with null name fails. */
TEST(ApexRegistryTest, RegisterComponentNullName) {
  ApexRegistry registry;
  auto status = registry.registerComponent(0x6600, nullptr);
  EXPECT_EQ(status, Status::ERROR_NULL_POINTER);
  EXPECT_EQ(registry.componentCount(), 0u);
}

/** @test Register component with empty name returns warning. */
TEST(ApexRegistryTest, RegisterComponentEmptyName) {
  ApexRegistry registry;
  auto status = registry.registerComponent(0x6600, "");
  EXPECT_EQ(status, Status::WARN_EMPTY_NAME);
  EXPECT_EQ(registry.componentCount(), 1u);
}

/** @test Register duplicate component fails. */
TEST(ApexRegistryTest, RegisterComponentDuplicate) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "ModelA"), Status::SUCCESS);
  auto status = registry.registerComponent(0x6600, "ModelB");
  EXPECT_EQ(status, Status::ERROR_DUPLICATE_COMPONENT);
  EXPECT_EQ(registry.componentCount(), 1u);
}

/** @test Register component after freeze fails. */
TEST(ApexRegistryTest, RegisterComponentAfterFreeze) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);
  EXPECT_EQ(registry.freeze(), Status::SUCCESS);
  auto status = registry.registerComponent(0x6601, "AnotherModel");
  EXPECT_EQ(status, Status::ERROR_ALREADY_FROZEN);
}

/** @test Multiple components can be registered. */
TEST(ApexRegistryTest, RegisterMultipleComponents) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6500, "ModelA"), Status::SUCCESS);
  EXPECT_EQ(registry.registerComponent(0x6600, "ModelB"), Status::SUCCESS);
  EXPECT_EQ(registry.registerComponent(0x6601, "ModelB"), Status::SUCCESS);
  EXPECT_EQ(registry.componentCount(), 3u);
}

/* ----------------------------- Task Registration ----------------------------- */

/** @test Register task succeeds when component exists. */
TEST(ApexRegistryTest, RegisterTaskSuccess) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  auto task = createTestTask("step");
  auto status = registry.registerTask(0x6600, 0, "step", &task);
  EXPECT_EQ(status, Status::SUCCESS);
  EXPECT_EQ(registry.taskCount(), 1u);
}

/** @test Register task fails when component not registered. */
TEST(ApexRegistryTest, RegisterTaskNoComponent) {
  ApexRegistry registry;
  auto task = createTestTask("step");
  auto status = registry.registerTask(0x6600, 0, "step", &task);
  EXPECT_EQ(status, Status::ERROR_COMPONENT_NOT_FOUND);
}

/** @test Register task with null pointer fails. */
TEST(ApexRegistryTest, RegisterTaskNullPointer) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);
  auto status = registry.registerTask(0x6600, 0, "step", nullptr);
  EXPECT_EQ(status, Status::ERROR_NULL_POINTER);
}

/** @test Register task with null name fails. */
TEST(ApexRegistryTest, RegisterTaskNullName) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);
  auto task = createTestTask("step");
  auto status = registry.registerTask(0x6600, 0, nullptr, &task);
  EXPECT_EQ(status, Status::ERROR_NULL_POINTER);
}

/** @test Register duplicate task fails. */
TEST(ApexRegistryTest, RegisterTaskDuplicate) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  auto task1 = createTestTask("step1");
  auto task2 = createTestTask("step2");
  EXPECT_EQ(registry.registerTask(0x6600, 0, "step1", &task1), Status::SUCCESS);
  auto status = registry.registerTask(0x6600, 0, "step2", &task2);
  EXPECT_EQ(status, Status::ERROR_DUPLICATE_TASK);
}

/** @test Register task after freeze fails. */
TEST(ApexRegistryTest, RegisterTaskAfterFreeze) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);
  EXPECT_EQ(registry.freeze(), Status::SUCCESS);

  auto task = createTestTask("step");
  auto status = registry.registerTask(0x6600, 0, "step", &task);
  EXPECT_EQ(status, Status::ERROR_ALREADY_FROZEN);
}

/** @test Task is linked to component. */
TEST(ApexRegistryTest, TaskLinkedToComponent) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  auto task1 = createTestTask("step1");
  auto task2 = createTestTask("step2");
  EXPECT_EQ(registry.registerTask(0x6600, 0, "step1", &task1), Status::SUCCESS);
  EXPECT_EQ(registry.registerTask(0x6600, 1, "step2", &task2), Status::SUCCESS);

  auto* comp = registry.getComponentEntry(0x6600);
  ASSERT_NE(comp, nullptr);
  EXPECT_EQ(comp->taskCount, 2u);
}

/* ----------------------------- Data Registration ----------------------------- */

/** @test Register data succeeds when component exists. */
TEST(ApexRegistryTest, RegisterDataSuccess) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  TestData data{};
  auto status = registry.registerData(0x6600, DataCategory::STATE, "state", &data, sizeof(data));
  EXPECT_EQ(status, Status::SUCCESS);
  EXPECT_EQ(registry.dataCount(), 1u);
}

/** @test Register data fails when component not registered. */
TEST(ApexRegistryTest, RegisterDataNoComponent) {
  ApexRegistry registry;
  TestData data{};
  auto status = registry.registerData(0x6600, DataCategory::STATE, "state", &data, sizeof(data));
  EXPECT_EQ(status, Status::ERROR_COMPONENT_NOT_FOUND);
}

/** @test Register data with null pointer fails. */
TEST(ApexRegistryTest, RegisterDataNullPointer) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);
  auto status = registry.registerData(0x6600, DataCategory::STATE, "state", nullptr, 16);
  EXPECT_EQ(status, Status::ERROR_NULL_POINTER);
}

/** @test Register data with null name fails. */
TEST(ApexRegistryTest, RegisterDataNullName) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);
  TestData data{};
  auto status = registry.registerData(0x6600, DataCategory::STATE, nullptr, &data, sizeof(data));
  EXPECT_EQ(status, Status::ERROR_NULL_POINTER);
}

/** @test Register data with zero size fails. */
TEST(ApexRegistryTest, RegisterDataZeroSize) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);
  TestData data{};
  auto status = registry.registerData(0x6600, DataCategory::STATE, "state", &data, 0);
  EXPECT_EQ(status, Status::ERROR_ZERO_SIZE);
}

/** @test Register duplicate data (same fullUid+category+name) fails. */
TEST(ApexRegistryTest, RegisterDataDuplicate) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  TestData data1{};
  TestData data2{};
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "state", &data1, sizeof(data1)),
            Status::SUCCESS);
  auto status = registry.registerData(0x6600, DataCategory::STATE, "state", &data2, sizeof(data2));
  EXPECT_EQ(status, Status::ERROR_DUPLICATE_DATA);
}

/** @test Same fullUid+category with different names succeeds. */
TEST(ApexRegistryTest, RegisterDataSameCategoryDifferentName) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  TestData data1{};
  TestData data2{};
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "state1", &data1, sizeof(data1)),
            Status::SUCCESS);
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "state2", &data2, sizeof(data2)),
            Status::SUCCESS);
  EXPECT_EQ(registry.dataCount(), 2u);
}

/** @test Register data after freeze fails. */
TEST(ApexRegistryTest, RegisterDataAfterFreeze) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);
  EXPECT_EQ(registry.freeze(), Status::SUCCESS);

  TestData data{};
  auto status = registry.registerData(0x6600, DataCategory::STATE, "state", &data, sizeof(data));
  EXPECT_EQ(status, Status::ERROR_ALREADY_FROZEN);
}

/** @test Data is linked to component. */
TEST(ApexRegistryTest, DataLinkedToComponent) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  TestData data1{};
  TestData data2{};
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "state", &data1, sizeof(data1)),
            Status::SUCCESS);
  EXPECT_EQ(
      registry.registerData(0x6600, DataCategory::TUNABLE_PARAM, "params", &data2, sizeof(data2)),
      Status::SUCCESS);

  auto* comp = registry.getComponentEntry(0x6600);
  ASSERT_NE(comp, nullptr);
  EXPECT_EQ(comp->dataCount, 2u);
}

/* ----------------------------- Freeze Semantics ----------------------------- */

/** @test Freeze succeeds on first call. */
TEST(ApexRegistryTest, FreezeSuccess) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);
  auto status = registry.freeze();
  EXPECT_EQ(status, Status::SUCCESS);
  EXPECT_TRUE(registry.isFrozen());
}

/** @test Freeze fails on second call. */
TEST(ApexRegistryTest, FreezeTwice) {
  ApexRegistry registry;
  EXPECT_EQ(registry.freeze(), Status::SUCCESS);
  auto status = registry.freeze();
  EXPECT_EQ(status, Status::ERROR_ALREADY_FROZEN);
}

/* ----------------------------- Component Queries ----------------------------- */

/** @test getComponentEntry returns registered component. */
TEST(ApexRegistryTest, GetComponentEntrySuccess) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  auto* comp = registry.getComponentEntry(0x6600);
  ASSERT_NE(comp, nullptr);
  EXPECT_EQ(comp->fullUid, 0x6600u);
  EXPECT_STREQ(comp->name, "TestModel");
}

/** @test getComponentEntry returns nullptr for unknown UID. */
TEST(ApexRegistryTest, GetComponentEntryNotFound) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  auto* comp = registry.getComponentEntry(0x9999);
  EXPECT_EQ(comp, nullptr);
}

/** @test IComponentResolver::getComponent returns SystemComponentBase pointer. */
TEST(ApexRegistryTest, GetComponentResolverInterface) {
  ApexRegistry registry;

  // Create a mock component pointer (normally this would be a real SystemComponentBase*)
  int dummyComponent = 42;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel", &dummyComponent), Status::SUCCESS);

  // Use the IComponentResolver interface
  system_core::system_component::IComponentResolver* resolver = &registry;
  auto* comp = resolver->getComponent(0x6600);
  EXPECT_EQ(comp,
            reinterpret_cast<system_core::system_component::SystemComponentBase*>(&dummyComponent));

  // Non-existent component returns nullptr
  EXPECT_EQ(resolver->getComponent(0x9999), nullptr);
}

/** @test getAllComponents returns all registered components. */
TEST(ApexRegistryTest, GetAllComponents) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6500, "ModelA"), Status::SUCCESS);
  EXPECT_EQ(registry.registerComponent(0x6600, "ModelB"), Status::SUCCESS);

  auto comps = registry.getAllComponents();
  EXPECT_EQ(comps.size(), 2u);
}

/* ----------------------------- Task Queries ----------------------------- */

/** @test getTask returns registered task. */
TEST(ApexRegistryTest, GetTaskSuccess) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  auto task = createTestTask("step");
  EXPECT_EQ(registry.registerTask(0x6600, 5, "step", &task), Status::SUCCESS);

  auto* entry = registry.getTask(0x6600, 5);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->fullUid, 0x6600u);
  EXPECT_EQ(entry->taskUid, 5u);
  EXPECT_STREQ(entry->name, "step");
  EXPECT_EQ(entry->task, &task);
}

/** @test getTask returns nullptr for unknown task. */
TEST(ApexRegistryTest, GetTaskNotFound) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  auto task = createTestTask("step");
  EXPECT_EQ(registry.registerTask(0x6600, 0, "step", &task), Status::SUCCESS);

  EXPECT_EQ(registry.getTask(0x6600, 99), nullptr);
  EXPECT_EQ(registry.getTask(0x9999, 0), nullptr);
}

/** @test getAllTasks returns all registered tasks. */
TEST(ApexRegistryTest, GetAllTasks) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  auto task1 = createTestTask("step1");
  auto task2 = createTestTask("step2");
  EXPECT_EQ(registry.registerTask(0x6600, 0, "step1", &task1), Status::SUCCESS);
  EXPECT_EQ(registry.registerTask(0x6600, 1, "step2", &task2), Status::SUCCESS);

  auto tasks = registry.getAllTasks();
  EXPECT_EQ(tasks.size(), 2u);
}

/* ----------------------------- Data Queries ----------------------------- */

/** @test getData by fullUid+category returns first match. */
TEST(ApexRegistryTest, GetDataByCategory) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  TestData data{};
  data.field1 = 0x1234;
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "state", &data, sizeof(data)),
            Status::SUCCESS);

  auto* entry = registry.getData(0x6600, DataCategory::STATE);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->fullUid, 0x6600u);
  EXPECT_EQ(entry->category, DataCategory::STATE);
  EXPECT_EQ(entry->size, sizeof(TestData));
}

/** @test getData by fullUid+category+name returns exact match. */
TEST(ApexRegistryTest, GetDataByName) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  TestData data1{};
  TestData data2{};
  data1.field1 = 0x1111;
  data2.field1 = 0x2222;
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "state1", &data1, sizeof(data1)),
            Status::SUCCESS);
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "state2", &data2, sizeof(data2)),
            Status::SUCCESS);

  auto* entry = registry.getData(0x6600, DataCategory::STATE, "state2");
  ASSERT_NE(entry, nullptr);
  EXPECT_STREQ(entry->name, "state2");
}

/** @test getData returns nullptr for unknown data. */
TEST(ApexRegistryTest, GetDataNotFound) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  EXPECT_EQ(registry.getData(0x6600, DataCategory::STATE), nullptr);
  EXPECT_EQ(registry.getData(0x6600, DataCategory::STATE, "unknown"), nullptr);
}

/** @test getAllData returns all registered data entries. */
TEST(ApexRegistryTest, GetAllData) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  TestData data1{};
  TestData data2{};
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "state", &data1, sizeof(data1)),
            Status::SUCCESS);
  EXPECT_EQ(
      registry.registerData(0x6600, DataCategory::TUNABLE_PARAM, "params", &data2, sizeof(data2)),
      Status::SUCCESS);

  auto data = registry.getAllData();
  EXPECT_EQ(data.size(), 2u);
}

/* ----------------------------- Component Data Access ----------------------------- */

/** @test getTasksForComponent returns component's tasks. */
TEST(ApexRegistryTest, GetTasksForComponent) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);
  EXPECT_EQ(registry.registerComponent(0x6601, "OtherModel"), Status::SUCCESS);

  auto task1 = createTestTask("step1");
  auto task2 = createTestTask("step2");
  auto task3 = createTestTask("other");
  EXPECT_EQ(registry.registerTask(0x6600, 0, "step1", &task1), Status::SUCCESS);
  EXPECT_EQ(registry.registerTask(0x6600, 1, "step2", &task2), Status::SUCCESS);
  EXPECT_EQ(registry.registerTask(0x6601, 0, "other", &task3), Status::SUCCESS);

  std::array<TaskEntry*, 10> tasks{};
  std::size_t count = registry.getTasksForComponent(0x6600, tasks.data(), tasks.size());

  EXPECT_EQ(count, 2u);
  EXPECT_EQ(tasks[0]->taskUid, 0u);
  EXPECT_EQ(tasks[1]->taskUid, 1u);
}

/** @test getDataForComponent returns component's data. */
TEST(ApexRegistryTest, GetDataForComponent) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);
  EXPECT_EQ(registry.registerComponent(0x6601, "OtherModel"), Status::SUCCESS);

  TestData data1{};
  TestData data2{};
  TestData data3{};
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "state", &data1, sizeof(data1)),
            Status::SUCCESS);
  EXPECT_EQ(
      registry.registerData(0x6600, DataCategory::TUNABLE_PARAM, "params", &data2, sizeof(data2)),
      Status::SUCCESS);
  EXPECT_EQ(registry.registerData(0x6601, DataCategory::OUTPUT, "output", &data3, sizeof(data3)),
            Status::SUCCESS);

  std::array<DataEntry*, 10> data{};
  std::size_t count = registry.getDataForComponent(0x6600, data.data(), data.size());

  EXPECT_EQ(count, 2u);
}

/** @test getTasksForComponent with null buffer returns 0. */
TEST(ApexRegistryTest, GetTasksForComponentNullBuffer) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);
  auto task = createTestTask("step");
  EXPECT_EQ(registry.registerTask(0x6600, 0, "step", &task), Status::SUCCESS);

  std::size_t count = registry.getTasksForComponent(0x6600, nullptr, 10);
  EXPECT_EQ(count, 0u);
}

/** @test getDataForComponent with unknown component returns 0. */
TEST(ApexRegistryTest, GetDataForComponentUnknown) {
  ApexRegistry registry;
  std::array<DataEntry*, 10> data{};
  std::size_t count = registry.getDataForComponent(0x9999, data.data(), data.size());
  EXPECT_EQ(count, 0u);
}

/* ----------------------------- Statistics ----------------------------- */

/** @test totalDataSize returns sum of all data sizes. */
TEST(ApexRegistryTest, TotalDataSize) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  TestData data1{};
  std::array<std::uint8_t, 64> data2{};
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "state", &data1, sizeof(data1)),
            Status::SUCCESS);
  EXPECT_EQ(
      registry.registerData(0x6600, DataCategory::OUTPUT, "buffer", data2.data(), data2.size()),
      Status::SUCCESS);

  EXPECT_EQ(registry.totalDataSize(), sizeof(TestData) + 64);
}

/** @test totalDataSize returns 0 for empty registry. */
TEST(ApexRegistryTest, TotalDataSizeEmpty) {
  ApexRegistry registry;
  EXPECT_EQ(registry.totalDataSize(), 0u);
}

/* ----------------------------- Reset ----------------------------- */

/** @test Reset clears all entries and unfreezes. */
TEST(ApexRegistryTest, ResetClears) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);
  TestData data{};
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "state", &data, sizeof(data)),
            Status::SUCCESS);
  auto task = createTestTask("step");
  EXPECT_EQ(registry.registerTask(0x6600, 0, "step", &task), Status::SUCCESS);
  EXPECT_EQ(registry.freeze(), Status::SUCCESS);

  registry.reset();

  EXPECT_FALSE(registry.isFrozen());
  EXPECT_EQ(registry.componentCount(), 0u);
  EXPECT_EQ(registry.taskCount(), 0u);
  EXPECT_EQ(registry.dataCount(), 0u);
}

/* ----------------------------- Byte Access Integration ----------------------------- */

/** @test Registered data can be accessed as bytes. */
TEST(ApexRegistryTest, DataByteAccess) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  TestData data{};
  data.field1 = 0x12345678;
  data.field2 = 0xDEADBEEF;
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "state", &data, sizeof(data)),
            Status::SUCCESS);

  auto* entry = registry.getData(0x6600, DataCategory::STATE);
  ASSERT_NE(entry, nullptr);

  auto bytes = entry->getBytes();
  EXPECT_EQ(bytes.size(), sizeof(TestData));

  // Verify first field bytes (little-endian)
  EXPECT_EQ(bytes[0], 0x78);
  EXPECT_EQ(bytes[1], 0x56);
  EXPECT_EQ(bytes[2], 0x34);
  EXPECT_EQ(bytes[3], 0x12);
}

/** @test Field-level byte access works correctly. */
TEST(ApexRegistryTest, DataFieldByteAccess) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  TestData data{};
  data.field1 = 0x11111111;
  data.field2 = 0x22222222;
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "state", &data, sizeof(data)),
            Status::SUCCESS);

  auto* entry = registry.getData(0x6600, DataCategory::STATE);
  ASSERT_NE(entry, nullptr);

  // Access second field (offset 4, length 4)
  auto bytes = entry->getBytes(4, 4);
  EXPECT_EQ(bytes.size(), 4u);
  EXPECT_EQ(bytes[0], 0x22);
  EXPECT_EQ(bytes[1], 0x22);
  EXPECT_EQ(bytes[2], 0x22);
  EXPECT_EQ(bytes[3], 0x22);
}

/* ----------------------------- Export Tests ----------------------------- */

#include "src/system/core/components/registry/apex/inc/RegistryExport.hpp"

#include <filesystem>
#include <random>
#include <fstream>

using system_core::registry::isValidRdatHeader;
using system_core::registry::RDAT_FILENAME;
using system_core::registry::RDAT_MAGIC;
using system_core::registry::RDAT_VERSION;
using system_core::registry::RdatComponentEntry;
using system_core::registry::RdatDataEntry;
using system_core::registry::RdatHeader;
using system_core::registry::RdatTaskEntry;

namespace {

/** RAII cleanup for test directory. */
class TempDir {
public:
  TempDir() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    path_ = std::filesystem::temp_directory_path() /
            ("apex_registry_test_" + std::to_string(dist(gen)));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  const std::filesystem::path& path() const { return path_; }

private:
  std::filesystem::path path_;
};

} // namespace

/** @test Export empty registry creates valid RDAT with zero counts. */
TEST(ApexRegistryExportTest, ExportEmptyRegistry) {
  ApexRegistry registry;
  TempDir tmpDir;

  auto status = registry.exportDatabase(tmpDir.path());
  EXPECT_EQ(status, Status::SUCCESS);

  auto filePath = tmpDir.path() / RDAT_FILENAME;
  ASSERT_TRUE(std::filesystem::exists(filePath));

  // Read and verify header
  std::ifstream in(filePath, std::ios::binary);
  ASSERT_TRUE(in.is_open());

  RdatHeader header{};
  in.read(reinterpret_cast<char*>(&header), sizeof(header));
  ASSERT_TRUE(in.good());

  EXPECT_TRUE(isValidRdatHeader(header));
  EXPECT_EQ(header.componentCount, 0u);
  EXPECT_EQ(header.taskCount, 0u);
  EXPECT_EQ(header.dataCount, 0u);
}

/** @test Export single component creates correct RDAT. */
TEST(ApexRegistryExportTest, ExportSingleComponent) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  TempDir tmpDir;
  auto status = registry.exportDatabase(tmpDir.path());
  EXPECT_EQ(status, Status::SUCCESS);

  // Read file
  auto filePath = tmpDir.path() / RDAT_FILENAME;
  std::ifstream in(filePath, std::ios::binary);
  ASSERT_TRUE(in.is_open());

  RdatHeader header{};
  in.read(reinterpret_cast<char*>(&header), sizeof(header));
  EXPECT_EQ(header.componentCount, 1u);
  EXPECT_EQ(header.taskCount, 0u);
  EXPECT_EQ(header.dataCount, 0u);

  RdatComponentEntry compEntry{};
  in.read(reinterpret_cast<char*>(&compEntry), sizeof(compEntry));
  EXPECT_EQ(compEntry.fullUid, 0x6600u);
  EXPECT_EQ(compEntry.taskCount, 0u);
  EXPECT_EQ(compEntry.dataCount, 0u);
}

/** @test Export with tasks creates correct task entries. */
TEST(ApexRegistryExportTest, ExportWithTasks) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  auto task1 = createTestTask("step1");
  auto task2 = createTestTask("step2");
  EXPECT_EQ(registry.registerTask(0x6600, 0, "step1", &task1), Status::SUCCESS);
  EXPECT_EQ(registry.registerTask(0x6600, 1, "step2", &task2), Status::SUCCESS);

  TempDir tmpDir;
  auto status = registry.exportDatabase(tmpDir.path());
  EXPECT_EQ(status, Status::SUCCESS);

  // Read file
  auto filePath = tmpDir.path() / RDAT_FILENAME;
  std::ifstream in(filePath, std::ios::binary);
  ASSERT_TRUE(in.is_open());

  RdatHeader header{};
  in.read(reinterpret_cast<char*>(&header), sizeof(header));
  EXPECT_EQ(header.componentCount, 1u);
  EXPECT_EQ(header.taskCount, 2u);
  EXPECT_EQ(header.dataCount, 0u);

  // Skip component entry
  in.seekg(sizeof(RdatHeader) + sizeof(RdatComponentEntry));

  // Read task entries
  RdatTaskEntry taskEntry1{};
  RdatTaskEntry taskEntry2{};
  in.read(reinterpret_cast<char*>(&taskEntry1), sizeof(taskEntry1));
  in.read(reinterpret_cast<char*>(&taskEntry2), sizeof(taskEntry2));

  EXPECT_EQ(taskEntry1.fullUid, 0x6600u);
  EXPECT_EQ(taskEntry1.taskUid, 0u);
  EXPECT_EQ(taskEntry2.fullUid, 0x6600u);
  EXPECT_EQ(taskEntry2.taskUid, 1u);
}

/** @test Export with data creates correct data entries. */
TEST(ApexRegistryExportTest, ExportWithData) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  TestData data{};
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "state", &data, sizeof(data)),
            Status::SUCCESS);

  TempDir tmpDir;
  auto status = registry.exportDatabase(tmpDir.path());
  EXPECT_EQ(status, Status::SUCCESS);

  // Read file
  auto filePath = tmpDir.path() / RDAT_FILENAME;
  std::ifstream in(filePath, std::ios::binary);
  ASSERT_TRUE(in.is_open());

  RdatHeader header{};
  in.read(reinterpret_cast<char*>(&header), sizeof(header));
  EXPECT_EQ(header.componentCount, 1u);
  EXPECT_EQ(header.taskCount, 0u);
  EXPECT_EQ(header.dataCount, 1u);

  // Skip component entry
  in.seekg(sizeof(RdatHeader) + sizeof(RdatComponentEntry));

  // Read data entry
  RdatDataEntry dataEntry{};
  in.read(reinterpret_cast<char*>(&dataEntry), sizeof(dataEntry));

  EXPECT_EQ(dataEntry.fullUid, 0x6600u);
  EXPECT_EQ(dataEntry.category, static_cast<std::uint8_t>(DataCategory::STATE));
  EXPECT_EQ(dataEntry.size, sizeof(TestData));
}

/** @test Export multiple components preserves order. */
TEST(ApexRegistryExportTest, ExportMultipleComponents) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6500, "ModelA"), Status::SUCCESS);
  EXPECT_EQ(registry.registerComponent(0x6600, "ModelB"), Status::SUCCESS);
  EXPECT_EQ(registry.registerComponent(0x6601, "ModelB"), Status::SUCCESS);

  TempDir tmpDir;
  auto status = registry.exportDatabase(tmpDir.path());
  EXPECT_EQ(status, Status::SUCCESS);

  // Read file
  auto filePath = tmpDir.path() / RDAT_FILENAME;
  std::ifstream in(filePath, std::ios::binary);
  ASSERT_TRUE(in.is_open());

  RdatHeader header{};
  in.read(reinterpret_cast<char*>(&header), sizeof(header));
  EXPECT_EQ(header.componentCount, 3u);

  RdatComponentEntry comp1{};
  RdatComponentEntry comp2{};
  RdatComponentEntry comp3{};
  in.read(reinterpret_cast<char*>(&comp1), sizeof(comp1));
  in.read(reinterpret_cast<char*>(&comp2), sizeof(comp2));
  in.read(reinterpret_cast<char*>(&comp3), sizeof(comp3));

  EXPECT_EQ(comp1.fullUid, 0x6500u);
  EXPECT_EQ(comp2.fullUid, 0x6600u);
  EXPECT_EQ(comp3.fullUid, 0x6601u);
}

/** @test String table contains all component/task/data names. */
TEST(ApexRegistryExportTest, StringTableContainsNames) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "MyComponent"), Status::SUCCESS);

  auto task = createTestTask("myTask");
  EXPECT_EQ(registry.registerTask(0x6600, 0, "myTask", &task), Status::SUCCESS);

  TestData data{};
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "myData", &data, sizeof(data)),
            Status::SUCCESS);

  TempDir tmpDir;
  auto status = registry.exportDatabase(tmpDir.path());
  EXPECT_EQ(status, Status::SUCCESS);

  // Read entire file to check string table
  auto filePath = tmpDir.path() / RDAT_FILENAME;
  std::ifstream in(filePath, std::ios::binary | std::ios::ate);
  ASSERT_TRUE(in.is_open());

  auto fileSize = in.tellg();
  in.seekg(0);

  std::vector<char> fileData(static_cast<std::size_t>(fileSize));
  in.read(fileData.data(), fileSize);

  // String table starts after header + component entries + task entries + data entries
  std::size_t stringTableOffset = sizeof(RdatHeader) + sizeof(RdatComponentEntry) +
                                  sizeof(RdatTaskEntry) + sizeof(RdatDataEntry);

  std::string fileContent(fileData.begin() + static_cast<std::ptrdiff_t>(stringTableOffset),
                          fileData.end());

  EXPECT_NE(fileContent.find("MyComponent"), std::string::npos);
  EXPECT_NE(fileContent.find("myTask"), std::string::npos);
  EXPECT_NE(fileContent.find("myData"), std::string::npos);
}

/** @test Export to nonexistent directory fails. */
TEST(ApexRegistryExportTest, ExportToInvalidPath) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6600, "TestModel"), Status::SUCCESS);

  std::filesystem::path invalidPath = "/nonexistent/path/that/does/not/exist";
  auto status = registry.exportDatabase(invalidPath);
  EXPECT_EQ(status, Status::ERROR_IO);
}

/** @test Component entry tracks correct task/data ranges. */
TEST(ApexRegistryExportTest, ComponentEntryRanges) {
  ApexRegistry registry;
  EXPECT_EQ(registry.registerComponent(0x6500, "ModelA"), Status::SUCCESS);
  EXPECT_EQ(registry.registerComponent(0x6600, "ModelB"), Status::SUCCESS);

  // ModelA: 2 tasks, 1 data
  auto taskA1 = createTestTask("taskA1");
  auto taskA2 = createTestTask("taskA2");
  EXPECT_EQ(registry.registerTask(0x6500, 0, "taskA1", &taskA1), Status::SUCCESS);
  EXPECT_EQ(registry.registerTask(0x6500, 1, "taskA2", &taskA2), Status::SUCCESS);

  TestData dataA{};
  EXPECT_EQ(registry.registerData(0x6500, DataCategory::STATE, "stateA", &dataA, sizeof(dataA)),
            Status::SUCCESS);

  // ModelB: 1 task, 2 data
  auto taskB1 = createTestTask("taskB1");
  EXPECT_EQ(registry.registerTask(0x6600, 0, "taskB1", &taskB1), Status::SUCCESS);

  TestData dataB1{};
  TestData dataB2{};
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::STATE, "stateB", &dataB1, sizeof(dataB1)),
            Status::SUCCESS);
  EXPECT_EQ(registry.registerData(0x6600, DataCategory::OUTPUT, "outB", &dataB2, sizeof(dataB2)),
            Status::SUCCESS);

  TempDir tmpDir;
  auto status = registry.exportDatabase(tmpDir.path());
  EXPECT_EQ(status, Status::SUCCESS);

  // Read file
  auto filePath = tmpDir.path() / RDAT_FILENAME;
  std::ifstream in(filePath, std::ios::binary);
  ASSERT_TRUE(in.is_open());

  RdatHeader header{};
  in.read(reinterpret_cast<char*>(&header), sizeof(header));
  EXPECT_EQ(header.componentCount, 2u);
  EXPECT_EQ(header.taskCount, 3u);
  EXPECT_EQ(header.dataCount, 3u);

  RdatComponentEntry compA{};
  RdatComponentEntry compB{};
  in.read(reinterpret_cast<char*>(&compA), sizeof(compA));
  in.read(reinterpret_cast<char*>(&compB), sizeof(compB));

  // ModelA: tasks at index 0-1, data at index 0
  EXPECT_EQ(compA.taskStart, 0u);
  EXPECT_EQ(compA.taskCount, 2u);
  EXPECT_EQ(compA.dataStart, 0u);
  EXPECT_EQ(compA.dataCount, 1u);

  // ModelB: tasks at index 2, data at index 1-2
  EXPECT_EQ(compB.taskStart, 2u);
  EXPECT_EQ(compB.taskCount, 1u);
  EXPECT_EQ(compB.dataStart, 1u);
  EXPECT_EQ(compB.dataCount, 2u);
}
