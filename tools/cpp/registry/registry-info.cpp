/**
 * @file registry-info.cpp
 * @brief CLI tool to inspect and dump RDAT registry database files.
 *
 * Reads a registry.rdat file and displays its contents in human-readable
 * or JSON format. Useful for debugging and external tooling integration.
 */

#include "src/system/core/components/registry/apex/inc/RegistryExport.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/DataCategory.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

using system_core::data::DataCategory;
using system_core::registry::isValidRdatHeader;
using system_core::registry::RDAT_FILENAME;
using system_core::registry::RdatComponentEntry;
using system_core::registry::RdatDataEntry;
using system_core::registry::RdatHeader;
using system_core::registry::RdatTaskEntry;

/* ----------------------------- Constants ----------------------------- */

constexpr std::string_view USAGE = R"(
Usage: registry-info [OPTIONS] <rdat-file>

Inspect and dump RDAT registry database files.

Arguments:
  <rdat-file>    Path to registry.rdat file

Options:
  --help         Show this help message
  --json         Output in JSON format
  --summary      Show only summary (counts)
  --components   Show component entries only
  --tasks        Show task entries only
  --data         Show data entries only

Examples:
  registry-info .apex_fs/db/registry.rdat
  registry-info --json .apex_fs/db/registry.rdat
  registry-info --summary .apex_fs/db/registry.rdat
)";

/* ----------------------------- Data Structures ----------------------------- */

struct ParsedRegistry {
  RdatHeader header;
  std::vector<RdatComponentEntry> components;
  std::vector<RdatTaskEntry> tasks;
  std::vector<RdatDataEntry> data;
  std::vector<char> stringTable;

  /** Get string from string table by offset. */
  [[nodiscard]] const char* getString(std::uint32_t offset) const noexcept {
    if (offset >= stringTable.size()) {
      return "<invalid>";
    }
    return stringTable.data() + offset;
  }
};

struct Options {
  std::filesystem::path inputFile;
  bool json{false};
  bool summaryOnly{false};
  bool componentsOnly{false};
  bool tasksOnly{false};
  bool dataOnly{false};
};

/* ----------------------------- File Parsing ----------------------------- */

[[nodiscard]] bool parseRdatFile(const std::filesystem::path& path, ParsedRegistry& out) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    fmt::print(stderr, "Error: Cannot open file: {}\n", path.string());
    return false;
  }

  auto fileSize = in.tellg();
  if (fileSize < static_cast<std::streamoff>(sizeof(RdatHeader))) {
    fmt::print(stderr, "Error: File too small to contain RDAT header\n");
    return false;
  }

  in.seekg(0);

  // Read header
  in.read(reinterpret_cast<char*>(&out.header), sizeof(RdatHeader));
  if (!in) {
    fmt::print(stderr, "Error: Failed to read header\n");
    return false;
  }

  if (!isValidRdatHeader(out.header)) {
    fmt::print(stderr, "Error: Invalid RDAT header (bad magic or version)\n");
    return false;
  }

  // Read component entries
  out.components.resize(out.header.componentCount);
  if (out.header.componentCount > 0) {
    in.read(reinterpret_cast<char*>(out.components.data()),
            static_cast<std::streamsize>(out.header.componentCount * sizeof(RdatComponentEntry)));
    if (!in) {
      fmt::print(stderr, "Error: Failed to read component entries\n");
      return false;
    }
  }

  // Read task entries
  out.tasks.resize(out.header.taskCount);
  if (out.header.taskCount > 0) {
    in.read(reinterpret_cast<char*>(out.tasks.data()),
            static_cast<std::streamsize>(out.header.taskCount * sizeof(RdatTaskEntry)));
    if (!in) {
      fmt::print(stderr, "Error: Failed to read task entries\n");
      return false;
    }
  }

  // Read data entries
  out.data.resize(out.header.dataCount);
  if (out.header.dataCount > 0) {
    in.read(reinterpret_cast<char*>(out.data.data()),
            static_cast<std::streamsize>(out.header.dataCount * sizeof(RdatDataEntry)));
    if (!in) {
      fmt::print(stderr, "Error: Failed to read data entries\n");
      return false;
    }
  }

  // Read string table (rest of file)
  auto currentPos = in.tellg();
  auto stringTableSize = static_cast<std::size_t>(fileSize - currentPos);
  out.stringTable.resize(stringTableSize);
  if (stringTableSize > 0) {
    in.read(out.stringTable.data(), static_cast<std::streamsize>(stringTableSize));
    if (!in) {
      fmt::print(stderr, "Error: Failed to read string table\n");
      return false;
    }
  }

  return true;
}

/* ----------------------------- Human Output ----------------------------- */

void printSummary(const ParsedRegistry& reg) {
  fmt::print("=== RDAT Registry Summary ===\n");
  fmt::print("  Version:    {}\n", reg.header.version);
  fmt::print("  Components: {}\n", reg.header.componentCount);
  fmt::print("  Tasks:      {}\n", reg.header.taskCount);
  fmt::print("  Data:       {}\n", reg.header.dataCount);
}

void printComponents(const ParsedRegistry& reg) {
  fmt::print("\n=== Components ({}) ===\n", reg.header.componentCount);
  for (std::size_t i = 0; i < reg.components.size(); ++i) {
    const auto& comp = reg.components[i];
    std::uint16_t compId = static_cast<std::uint16_t>(comp.fullUid >> 8);
    std::uint8_t instIdx = static_cast<std::uint8_t>(comp.fullUid & 0xFF);

    fmt::print("[{}] {} (fullUid=0x{:06X}, compId={}, inst={})\n", i,
               reg.getString(comp.nameOffset), comp.fullUid, compId, instIdx);
    fmt::print("    Tasks: {} (start={}), Data: {} (start={})\n", comp.taskCount, comp.taskStart,
               comp.dataCount, comp.dataStart);
  }
}

void printTasks(const ParsedRegistry& reg) {
  fmt::print("\n=== Tasks ({}) ===\n", reg.header.taskCount);
  for (std::size_t i = 0; i < reg.tasks.size(); ++i) {
    const auto& task = reg.tasks[i];
    fmt::print("[{}] {} (fullUid=0x{:06X}, taskUid={})\n", i, reg.getString(task.nameOffset),
               task.fullUid, task.taskUid);
  }
}

const char* categoryToString(std::uint8_t cat) {
  switch (static_cast<DataCategory>(cat)) {
  case DataCategory::STATIC_PARAM:
    return "STATIC_PARAM";
  case DataCategory::TUNABLE_PARAM:
    return "TUNABLE_PARAM";
  case DataCategory::STATE:
    return "STATE";
  case DataCategory::INPUT:
    return "INPUT";
  case DataCategory::OUTPUT:
    return "OUTPUT";
  default:
    return "UNKNOWN";
  }
}

void printData(const ParsedRegistry& reg) {
  fmt::print("\n=== Data Entries ({}) ===\n", reg.header.dataCount);
  for (std::size_t i = 0; i < reg.data.size(); ++i) {
    const auto& d = reg.data[i];
    fmt::print("[{}] {} (fullUid=0x{:06X}, {}, {} bytes)\n", i, reg.getString(d.nameOffset),
               d.fullUid, categoryToString(d.category), d.size);
  }
}

void printHuman(const ParsedRegistry& reg, const Options& opts) {
  if (opts.summaryOnly) {
    printSummary(reg);
    return;
  }

  printSummary(reg);

  if (!opts.tasksOnly && !opts.dataOnly) {
    printComponents(reg);
  }

  if (!opts.componentsOnly && !opts.dataOnly) {
    printTasks(reg);
  }

  if (!opts.componentsOnly && !opts.tasksOnly) {
    printData(reg);
  }
}

/* ----------------------------- JSON Output ----------------------------- */

void printJson(const ParsedRegistry& reg, const Options& opts) {
  fmt::print("{{\n");

  // Header
  fmt::print("  \"version\": {},\n", reg.header.version);
  fmt::print("  \"componentCount\": {},\n", reg.header.componentCount);
  fmt::print("  \"taskCount\": {},\n", reg.header.taskCount);
  fmt::print("  \"dataCount\": {},\n", reg.header.dataCount);

  if (!opts.summaryOnly) {
    // Components
    if (!opts.tasksOnly && !opts.dataOnly) {
      fmt::print("  \"components\": [\n");
      for (std::size_t i = 0; i < reg.components.size(); ++i) {
        const auto& comp = reg.components[i];
        std::uint16_t compId = static_cast<std::uint16_t>(comp.fullUid >> 8);
        std::uint8_t instIdx = static_cast<std::uint8_t>(comp.fullUid & 0xFF);

        fmt::print("    {{\n");
        fmt::print("      \"name\": \"{}\",\n", reg.getString(comp.nameOffset));
        fmt::print("      \"fullUid\": {},\n", comp.fullUid);
        fmt::print("      \"componentId\": {},\n", compId);
        fmt::print("      \"instanceIndex\": {},\n", instIdx);
        fmt::print("      \"taskStart\": {},\n", comp.taskStart);
        fmt::print("      \"taskCount\": {},\n", comp.taskCount);
        fmt::print("      \"dataStart\": {},\n", comp.dataStart);
        fmt::print("      \"dataCount\": {}\n", comp.dataCount);
        fmt::print("    }}{}\n", (i + 1 < reg.components.size()) ? "," : "");
      }
      fmt::print("  ],\n");
    }

    // Tasks
    if (!opts.componentsOnly && !opts.dataOnly) {
      fmt::print("  \"tasks\": [\n");
      for (std::size_t i = 0; i < reg.tasks.size(); ++i) {
        const auto& task = reg.tasks[i];
        fmt::print("    {{\n");
        fmt::print("      \"name\": \"{}\",\n", reg.getString(task.nameOffset));
        fmt::print("      \"fullUid\": {},\n", task.fullUid);
        fmt::print("      \"taskUid\": {}\n", task.taskUid);
        fmt::print("    }}{}\n", (i + 1 < reg.tasks.size()) ? "," : "");
      }
      fmt::print("  ],\n");
    }

    // Data
    if (!opts.componentsOnly && !opts.tasksOnly) {
      fmt::print("  \"data\": [\n");
      for (std::size_t i = 0; i < reg.data.size(); ++i) {
        const auto& d = reg.data[i];
        fmt::print("    {{\n");
        fmt::print("      \"name\": \"{}\",\n", reg.getString(d.nameOffset));
        fmt::print("      \"fullUid\": {},\n", d.fullUid);
        fmt::print("      \"category\": \"{}\",\n", categoryToString(d.category));
        fmt::print("      \"size\": {}\n", d.size);
        fmt::print("    }}{}\n", (i + 1 < reg.data.size()) ? "," : "");
      }
      fmt::print("  ]\n");
    }
  }

  fmt::print("}}\n");
}

/* ----------------------------- Argument Parsing ----------------------------- */

bool parseArgs(int argc, char* argv[], Options& opts) {
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      fmt::print("{}\n", USAGE);
      return false;
    }
    if (arg == "--json") {
      opts.json = true;
    } else if (arg == "--summary") {
      opts.summaryOnly = true;
    } else if (arg == "--components") {
      opts.componentsOnly = true;
    } else if (arg == "--tasks") {
      opts.tasksOnly = true;
    } else if (arg == "--data") {
      opts.dataOnly = true;
    } else if (arg[0] == '-') {
      fmt::print(stderr, "Error: Unknown option: {}\n", arg);
      return false;
    } else {
      opts.inputFile = arg;
    }
  }

  if (opts.inputFile.empty()) {
    fmt::print(stderr, "Error: No input file specified\n");
    fmt::print("{}\n", USAGE);
    return false;
  }

  return true;
}

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  Options opts;
  if (!parseArgs(argc, argv, opts)) {
    return 1;
  }

  ParsedRegistry reg;
  if (!parseRdatFile(opts.inputFile, reg)) {
    return 1;
  }

  if (opts.json) {
    printJson(reg, opts);
  } else {
    printHuman(reg, opts);
  }

  return 0;
}
