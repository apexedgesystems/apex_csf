/**
 * @file ApexRegistry.cpp
 * @brief Implementation of the unified registry for components, tasks, and data.
 */

#include "src/system/core/components/registry/apex/inc/ApexRegistry.hpp"
#include "src/system/core/components/registry/apex/inc/RegistryExport.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/DataCategory.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <cstring>

#include <fstream>

#include <fmt/core.h>

namespace system_core {
namespace registry {

/* ----------------------------- Construction ----------------------------- */

ApexRegistry::ApexRegistry() noexcept : RegistryBase() {
  // Reserve initial capacity to reduce allocations during registration
  components_.reserve(MAX_COMPONENTS);
  tasks_.reserve(MAX_TASKS);
  data_.reserve(MAX_DATA_ENTRIES);
}

/* ----------------------------- Lifecycle Hooks ----------------------------- */

std::uint8_t ApexRegistry::doInit() noexcept {
  // Nothing special to initialize beyond base class
  return 0;
}

void ApexRegistry::doReset() noexcept {
  components_.clear();
  tasks_.clear();
  data_.clear();
  frozen_ = false;
}

/* ----------------------------- Registration ----------------------------- */

Status ApexRegistry::registerComponent(std::uint32_t fullUid, const char* name, void* component,
                                       system_component::ComponentType type) noexcept {
  if (frozen_) {
    return Status::ERROR_ALREADY_FROZEN;
  }

  if (name == nullptr) {
    return Status::ERROR_NULL_POINTER;
  }

  // Check capacity
  if (components_.size() >= MAX_COMPONENTS) {
    return Status::ERROR_CAPACITY_EXCEEDED;
  }

  // Check for duplicate
  if (findComponentIndex(fullUid) != SIZE_MAX) {
    return Status::ERROR_DUPLICATE_COMPONENT;
  }

  // Add entry
  ComponentEntry entry{};
  entry.fullUid = fullUid;
  entry.name = name;
  entry.component = component;
  entry.type = type;
  entry.taskCount = 0;
  entry.dataCount = 0;
  components_.push_back(entry);

  // Warn if empty name
  if (name[0] == '\0') {
    return Status::WARN_EMPTY_NAME;
  }

  return Status::SUCCESS;
}

Status ApexRegistry::registerTask(std::uint32_t fullUid, std::uint8_t taskUid, const char* name,
                                  schedulable::SchedulableTask* task) noexcept {
  if (frozen_) {
    return Status::ERROR_ALREADY_FROZEN;
  }

  if (name == nullptr || task == nullptr) {
    return Status::ERROR_NULL_POINTER;
  }

  // Check capacity
  if (tasks_.size() >= MAX_TASKS) {
    return Status::ERROR_CAPACITY_EXCEEDED;
  }

  // Find owning component
  std::size_t compIdx = findComponentIndex(fullUid);
  if (compIdx == SIZE_MAX) {
    return Status::ERROR_COMPONENT_NOT_FOUND;
  }

  // Check for duplicate task (same fullUid + taskUid)
  for (const auto& t : tasks_) {
    if (t.fullUid == fullUid && t.taskUid == taskUid) {
      return Status::ERROR_DUPLICATE_TASK;
    }
  }

  // Add entry
  TaskEntry entry{};
  entry.fullUid = fullUid;
  entry.taskUid = taskUid;
  entry.name = name;
  entry.task = task;
  tasks_.push_back(entry);

  // Link to component
  if (!linkTaskToComponent(compIdx, tasks_.size() - 1)) {
    // Component has too many tasks - remove the entry we just added
    tasks_.pop_back();
    return Status::ERROR_CAPACITY_EXCEEDED;
  }

  // Warn if empty name
  if (name[0] == '\0') {
    return Status::WARN_EMPTY_NAME;
  }

  return Status::SUCCESS;
}

Status ApexRegistry::registerData(std::uint32_t fullUid, data::DataCategory category,
                                  const char* name, const void* ptr, std::size_t size) noexcept {
  if (frozen_) {
    return Status::ERROR_ALREADY_FROZEN;
  }

  if (name == nullptr) {
    return Status::ERROR_NULL_POINTER;
  }

  if (ptr == nullptr) {
    return Status::ERROR_NULL_POINTER;
  }

  if (size == 0) {
    return Status::ERROR_ZERO_SIZE;
  }

  // Check capacity
  if (data_.size() >= MAX_DATA_ENTRIES) {
    return Status::ERROR_CAPACITY_EXCEEDED;
  }

  // Find owning component
  std::size_t compIdx = findComponentIndex(fullUid);
  if (compIdx == SIZE_MAX) {
    return Status::ERROR_COMPONENT_NOT_FOUND;
  }

  // Check for duplicate (same fullUid + category + name)
  for (const auto& d : data_) {
    if (d.fullUid == fullUid && d.category == category) {
      if (std::strcmp(d.name, name) == 0) {
        return Status::ERROR_DUPLICATE_DATA;
      }
    }
  }

  // Add entry
  DataEntry entry{};
  entry.fullUid = fullUid;
  entry.category = category;
  entry.name = name;
  entry.dataPtr = ptr;
  entry.size = size;
  data_.push_back(entry);

  // Link to component
  if (!linkDataToComponent(compIdx, data_.size() - 1)) {
    // Component has too many data entries - remove what we added
    data_.pop_back();
    return Status::ERROR_CAPACITY_EXCEEDED;
  }

  // Warn if empty name
  if (name[0] == '\0') {
    return Status::WARN_EMPTY_NAME;
  }

  return Status::SUCCESS;
}

Status ApexRegistry::freeze() noexcept {
  if (frozen_) {
    return Status::ERROR_ALREADY_FROZEN;
  }

  frozen_ = true;
  return Status::SUCCESS;
}

/* ----------------------------- Component Queries ----------------------------- */

ComponentEntry* ApexRegistry::getComponentEntry(std::uint32_t fullUid) noexcept {
  std::size_t idx = findComponentIndex(fullUid);
  if (idx == SIZE_MAX) {
    return nullptr;
  }
  return &components_[idx];
}

const ComponentEntry* ApexRegistry::getComponentEntry(std::uint32_t fullUid) const noexcept {
  for (const auto& c : components_) {
    if (c.fullUid == fullUid) {
      return &c;
    }
  }
  return nullptr;
}

apex::compat::span<ComponentEntry> ApexRegistry::getAllComponents() noexcept {
  return {components_.data(), components_.size()};
}

apex::compat::span<const ComponentEntry> ApexRegistry::getAllComponents() const noexcept {
  return {components_.data(), components_.size()};
}

/* ----------------------------- Task Queries ----------------------------- */

TaskEntry* ApexRegistry::getTask(std::uint32_t fullUid, std::uint8_t taskUid) noexcept {
  for (auto& t : tasks_) {
    if (t.fullUid == fullUid && t.taskUid == taskUid) {
      return &t;
    }
  }
  return nullptr;
}

apex::compat::span<TaskEntry> ApexRegistry::getAllTasks() noexcept {
  return {tasks_.data(), tasks_.size()};
}

apex::compat::span<const TaskEntry> ApexRegistry::getAllTasks() const noexcept {
  return {tasks_.data(), tasks_.size()};
}

/* ----------------------------- Data Queries ----------------------------- */

DataEntry* ApexRegistry::getData(std::uint32_t fullUid, data::DataCategory category) noexcept {
  for (auto& d : data_) {
    if (d.fullUid == fullUid && d.category == category) {
      return &d;
    }
  }
  return nullptr;
}

DataEntry* ApexRegistry::getData(std::uint32_t fullUid, data::DataCategory category,
                                 const char* name) noexcept {
  if (name == nullptr) {
    return nullptr;
  }

  for (auto& d : data_) {
    if (d.fullUid == fullUid && d.category == category) {
      if (std::strcmp(d.name, name) == 0) {
        return &d;
      }
    }
  }
  return nullptr;
}

apex::compat::span<DataEntry> ApexRegistry::getAllData() noexcept {
  return {data_.data(), data_.size()};
}

apex::compat::span<const DataEntry> ApexRegistry::getAllData() const noexcept {
  return {data_.data(), data_.size()};
}

/* ----------------------------- Component Data Access ----------------------------- */

std::size_t ApexRegistry::getTasksForComponent(std::uint32_t fullUid, TaskEntry** outTasks,
                                               std::size_t maxTasks) noexcept {
  if (outTasks == nullptr || maxTasks == 0) {
    return 0;
  }

  const ComponentEntry* comp = getComponentEntry(fullUid);
  if (comp == nullptr) {
    return 0;
  }

  std::size_t count = 0;
  for (std::size_t i = 0; i < comp->taskCount && count < maxTasks; ++i) {
    std::size_t taskIdx = comp->taskIndices[i];
    if (taskIdx < tasks_.size()) {
      outTasks[count++] = &tasks_[taskIdx];
    }
  }

  return count;
}

std::size_t ApexRegistry::getDataForComponent(std::uint32_t fullUid, DataEntry** outData,
                                              std::size_t maxData) noexcept {
  if (outData == nullptr || maxData == 0) {
    return 0;
  }

  const ComponentEntry* comp = getComponentEntry(fullUid);
  if (comp == nullptr) {
    return 0;
  }

  std::size_t count = 0;
  for (std::size_t i = 0; i < comp->dataCount && count < maxData; ++i) {
    std::size_t dataIdx = comp->dataIndices[i];
    if (dataIdx < data_.size()) {
      outData[count++] = &data_[dataIdx];
    }
  }

  return count;
}

/* ----------------------------- Statistics ----------------------------- */

std::size_t ApexRegistry::totalDataSize() const noexcept {
  std::size_t total = 0;
  for (const auto& d : data_) {
    total += d.size;
  }
  return total;
}

/* ----------------------------- Internal Helpers ----------------------------- */

std::size_t ApexRegistry::findComponentIndex(std::uint32_t fullUid) const noexcept {
  for (std::size_t i = 0; i < components_.size(); ++i) {
    if (components_[i].fullUid == fullUid) {
      return i;
    }
  }
  return SIZE_MAX;
}

bool ApexRegistry::linkTaskToComponent(std::size_t compIdx, std::size_t taskIdx) noexcept {
  if (compIdx >= components_.size()) {
    return false;
  }

  ComponentEntry& comp = components_[compIdx];
  if (comp.taskCount >= MAX_TASKS_PER_COMPONENT) {
    return false;
  }

  comp.taskIndices[comp.taskCount++] = taskIdx;
  return true;
}

bool ApexRegistry::linkDataToComponent(std::size_t compIdx, std::size_t dataIdx) noexcept {
  if (compIdx >= components_.size()) {
    return false;
  }

  ComponentEntry& comp = components_[compIdx];
  if (comp.dataCount >= MAX_DATA_PER_COMPONENT) {
    return false;
  }

  comp.dataIndices[comp.dataCount++] = dataIdx;
  return true;
}

/* ----------------------------- Logging ----------------------------- */

static constexpr std::string_view REGISTRY_LOG_FN = "registry.log";

void ApexRegistry::initRegistryLog(const std::filesystem::path& logDir) noexcept {
  auto logPath = logDir / REGISTRY_LOG_FN;
  auto log =
      std::make_shared<logs::SystemLog>(logPath.string(), logs::SystemLog::Mode::ASYNC, 4096);
  log->setLevel(logs::SystemLog::Level::DEBUG);
  setComponentLog(std::move(log));
}

void ApexRegistry::logRegistryContents() noexcept {
  if (!componentLog()) {
    return;
  }

  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "    COMPREHENSIVE REGISTRY CONTENTS    ");
  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "");

  // Summary
  componentLog()->info(label(), fmt::format("Components: {}", components_.size()));
  componentLog()->info(label(), fmt::format("Tasks: {}", tasks_.size()));
  componentLog()->info(label(), fmt::format("Data Entries: {}", data_.size()));
  componentLog()->info(label(), fmt::format("Total Data Size: {} bytes", totalDataSize()));
  componentLog()->info(label(), fmt::format("Registry Frozen: {}", frozen_ ? "YES" : "NO"));
  componentLog()->info(label(), "");

  // Component details
  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "          REGISTERED COMPONENTS        ");
  componentLog()->info(label(), "========================================");

  for (const auto& comp : components_) {
    componentLog()->info(label(), "");
    componentLog()->info(label(),
                         fmt::format("Component: {} (fullUid=0x{:06X})", comp.name, comp.fullUid));
    componentLog()->info(label(), "----------------------------------------");

    // Extract componentId and instanceIndex from fullUid
    std::uint16_t compId = static_cast<std::uint16_t>(comp.fullUid >> 8);
    std::uint8_t instIdx = static_cast<std::uint8_t>(comp.fullUid & 0xFF);
    componentLog()->info(label(), fmt::format("  componentId: {}", compId));
    componentLog()->info(label(), fmt::format("  instanceIndex: {}", instIdx));
    componentLog()->info(label(), fmt::format("  type: {}", system_component::toString(comp.type)));
    componentLog()->info(label(), fmt::format("  Tasks: {}", comp.taskCount));
    componentLog()->info(label(), fmt::format("  Data Entries: {}", comp.dataCount));

    // List tasks
    if (comp.taskCount > 0) {
      componentLog()->info(label(), "  Tasks:");
      for (std::size_t i = 0; i < comp.taskCount; ++i) {
        std::size_t taskIdx = comp.taskIndices[i];
        if (taskIdx < tasks_.size()) {
          const auto& task = tasks_[taskIdx];
          componentLog()->info(
              label(), fmt::format("    [{}] taskUid={} \"{}\"", i, task.taskUid, task.name));
        }
      }
    }

    // List data entries
    if (comp.dataCount > 0) {
      componentLog()->info(label(), "  Data:");
      for (std::size_t i = 0; i < comp.dataCount; ++i) {
        std::size_t dataIdx = comp.dataIndices[i];
        if (dataIdx < data_.size()) {
          const auto& d = data_[dataIdx];
          componentLog()->info(label(),
                               fmt::format("    [{}] {} \"{}\" ({} bytes @ {})", i,
                                           data::toString(d.category), d.name, d.size, d.dataPtr));
        }
      }
    }
  }

  componentLog()->info(label(), "");

  // Statistics summary
  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "         REGISTRY STATISTICS           ");
  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "");

  // Task frequency by component
  componentLog()->info(label(), "Tasks per Component:");
  for (const auto& comp : components_) {
    componentLog()->info(label(), fmt::format("  {}: {} task(s)", comp.name, comp.taskCount));
  }
  componentLog()->info(label(), "");

  // Data size by component
  componentLog()->info(label(), "Data size per Component:");
  for (const auto& comp : components_) {
    std::size_t compDataSize = 0;
    for (std::size_t i = 0; i < comp.dataCount; ++i) {
      std::size_t dataIdx = comp.dataIndices[i];
      if (dataIdx < data_.size()) {
        compDataSize += data_[dataIdx].size;
      }
    }
    componentLog()->info(label(), fmt::format("  {}: {} bytes", comp.name, compDataSize));
  }
  componentLog()->info(label(), "");

  // Data by category
  componentLog()->info(label(), "Data entries by Category:");
  std::size_t staticCount = 0, tunableCount = 0, stateCount = 0, inputCount = 0, outputCount = 0;
  for (const auto& d : data_) {
    switch (d.category) {
    case data::DataCategory::STATIC_PARAM:
      ++staticCount;
      break;
    case data::DataCategory::TUNABLE_PARAM:
      ++tunableCount;
      break;
    case data::DataCategory::STATE:
      ++stateCount;
      break;
    case data::DataCategory::INPUT:
      ++inputCount;
      break;
    case data::DataCategory::OUTPUT:
      ++outputCount;
      break;
    }
  }
  componentLog()->info(label(), fmt::format("  STATIC_PARAM: {}", staticCount));
  componentLog()->info(label(), fmt::format("  TUNABLE_PARAM: {}", tunableCount));
  componentLog()->info(label(), fmt::format("  STATE: {}", stateCount));
  componentLog()->info(label(), fmt::format("  INPUT: {}", inputCount));
  componentLog()->info(label(), fmt::format("  OUTPUT: {}", outputCount));
  componentLog()->info(label(), "");

  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "       END OF REGISTRY CONTENTS        ");
  componentLog()->info(label(), "========================================");

  componentLog()->flush();
}

/* ----------------------------- Export ----------------------------- */

Status ApexRegistry::exportDatabase(const std::filesystem::path& dbDir) noexcept {
  // Build string table and track offsets
  std::vector<char> stringTable;
  std::vector<std::uint32_t> componentNameOffsets;
  std::vector<std::uint32_t> taskNameOffsets;
  std::vector<std::uint32_t> dataNameOffsets;

  componentNameOffsets.reserve(components_.size());
  taskNameOffsets.reserve(tasks_.size());
  dataNameOffsets.reserve(data_.size());

  // Helper to add string to table
  auto addString = [&stringTable](const char* str) -> std::uint32_t {
    std::uint32_t offset = static_cast<std::uint32_t>(stringTable.size());
    if (str != nullptr) {
      std::size_t len = std::strlen(str);
      stringTable.insert(stringTable.end(), str, str + len + 1); // Include null
    } else {
      stringTable.push_back('\0');
    }
    return offset;
  };

  // Add component names
  for (const auto& comp : components_) {
    componentNameOffsets.push_back(addString(comp.name));
  }

  // Add task names
  for (const auto& task : tasks_) {
    taskNameOffsets.push_back(addString(task.name));
  }

  // Add data names
  for (const auto& d : data_) {
    dataNameOffsets.push_back(addString(d.name));
  }

  // Build header
  RdatHeader header{};
  header.magic = RDAT_MAGIC;
  header.version = RDAT_VERSION;
  header.flags = 0;
  header.componentCount = static_cast<std::uint16_t>(components_.size());
  header.taskCount = static_cast<std::uint16_t>(tasks_.size());
  header.dataCount = static_cast<std::uint16_t>(data_.size());
  header.reserved = 0;

  // Build component entries with task/data ranges
  std::vector<RdatComponentEntry> compEntries;
  compEntries.reserve(components_.size());

  // Track task/data indices per component
  std::uint16_t taskIdx = 0;
  std::uint16_t dataIdx = 0;

  for (std::size_t i = 0; i < components_.size(); ++i) {
    const auto& comp = components_[i];
    RdatComponentEntry entry{};
    entry.fullUid = comp.fullUid;
    entry.nameOffset = componentNameOffsets[i];
    entry.taskStart = taskIdx;
    entry.taskCount = static_cast<std::uint16_t>(comp.taskCount);
    entry.dataStart = dataIdx;
    entry.dataCount = static_cast<std::uint16_t>(comp.dataCount);
    entry.componentType = static_cast<std::uint8_t>(comp.type);
    entry.reserved1 = 0;
    entry.reserved2 = 0;
    entry.reserved3 = 0;
    compEntries.push_back(entry);

    taskIdx += entry.taskCount;
    dataIdx += entry.dataCount;
  }

  // Build task entries (ordered by component)
  std::vector<RdatTaskEntry> taskEntries;
  taskEntries.reserve(tasks_.size());

  for (const auto& comp : components_) {
    for (std::size_t i = 0; i < comp.taskCount; ++i) {
      std::size_t srcIdx = comp.taskIndices[i];
      if (srcIdx < tasks_.size()) {
        const auto& task = tasks_[srcIdx];
        RdatTaskEntry entry{};
        entry.fullUid = task.fullUid;
        entry.taskUid = task.taskUid;
        entry.reserved1 = 0;
        entry.reserved2 = 0;
        entry.nameOffset = taskNameOffsets[srcIdx];
        entry.reserved3 = 0;
        taskEntries.push_back(entry);
      }
    }
  }

  // Build data entries (ordered by component)
  std::vector<RdatDataEntry> dataEntries;
  dataEntries.reserve(data_.size());

  for (const auto& comp : components_) {
    for (std::size_t i = 0; i < comp.dataCount; ++i) {
      std::size_t srcIdx = comp.dataIndices[i];
      if (srcIdx < data_.size()) {
        const auto& d = data_[srcIdx];
        RdatDataEntry entry{};
        entry.fullUid = d.fullUid;
        entry.category = static_cast<std::uint8_t>(d.category);
        entry.reserved1 = 0;
        entry.reserved2 = 0;
        entry.nameOffset = dataNameOffsets[srcIdx];
        entry.size = static_cast<std::uint32_t>(d.size);
        entry.reserved3 = 0;
        dataEntries.push_back(entry);
      }
    }
  }

  // Write file
  auto filePath = dbDir / RDAT_FILENAME;
  std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
  if (!out) {
    return Status::ERROR_IO;
  }

  // Write header
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  if (!out) {
    return Status::ERROR_IO;
  }

  // Write component entries
  if (!compEntries.empty()) {
    out.write(reinterpret_cast<const char*>(compEntries.data()),
              static_cast<std::streamsize>(compEntries.size() * sizeof(RdatComponentEntry)));
    if (!out) {
      return Status::ERROR_IO;
    }
  }

  // Write task entries
  if (!taskEntries.empty()) {
    out.write(reinterpret_cast<const char*>(taskEntries.data()),
              static_cast<std::streamsize>(taskEntries.size() * sizeof(RdatTaskEntry)));
    if (!out) {
      return Status::ERROR_IO;
    }
  }

  // Write data entries
  if (!dataEntries.empty()) {
    out.write(reinterpret_cast<const char*>(dataEntries.data()),
              static_cast<std::streamsize>(dataEntries.size() * sizeof(RdatDataEntry)));
    if (!out) {
      return Status::ERROR_IO;
    }
  }

  // Write string table
  if (!stringTable.empty()) {
    out.write(stringTable.data(), static_cast<std::streamsize>(stringTable.size()));
    if (!out) {
      return Status::ERROR_IO;
    }
  }

  out.close();
  if (!out) {
    return Status::ERROR_IO;
  }

  return Status::SUCCESS;
}

} // namespace registry
} // namespace system_core
