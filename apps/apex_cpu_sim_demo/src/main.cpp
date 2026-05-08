/**
 * @file main.cpp
 * @brief Intel 4004 CPU Simulator Demo (L0 + L1 + L2).
 *
 * Simulates the Intel 4004 microprocessor at three production fidelity
 * levels:
 *
 *   L0: Behavioral CPU (Intel4004Cpu)
 *       Fast functional model. Runs full programs, reports register state.
 *
 *   L1: Component hybrid (Intel4004GridLevel1)
 *       Transistor-level visualization. 1305 NOR gates use Level 1
 *       Shichman-Hodges physics (validated 0.0000V vs ngspice). 222 pass
 *       gates and 610 dynamic storage transistors use binary switch.
 *       105 standalone loads use resistive G_LOAD. Behavioral timing
 *       injection drives clocks and machine states.
 *
 *   L2: Engineered physics (Intel4004GridLevel2)
 *       BSIM3 (smooth Vgst_eff) on the 338 cross-coupled latch transistors,
 *       Meyer intrinsic + overlap capacitances on every device, and 66
 *       layout-extracted bootstrap caps loaded from a data file. Behavioral
 *       overlay is OFF -- decode signals, OPR/OPA latches, and ACC are
 *       resolved by physics + custom-physics writeback primitives.
 *
 *   For multi-byte programs every level uses the L0/L1/L2 hybrid pattern:
 *   L0 is authoritative for instruction state, and a fresh transistor-level
 *   circuit is built per byte (seeded from L0's prior ACC) so each byte's
 *   transistor voltages can be observed without inter-byte drift.
 *
 * Usage:
 *   ./ApexCpuSimDemo                              # default: LDM 5 at L0+L1
 *   ./ApexCpuSimDemo --level 0                    # L0 only (fast)
 *   ./ApexCpuSimDemo --level 1                    # L0 + L1 transistor sim
 *   ./ApexCpuSimDemo --level 2                    # L0 + L2 engineered physics
 *   ./ApexCpuSimDemo --program "D5 00 D3"         # custom hex program
 *   ./ApexCpuSimDemo --probe ACC.0 --probe D0     # probe specific nets
 *   ./ApexCpuSimDemo --netlist path/to/4004.spice # custom netlist
 *   ./ApexCpuSimDemo --bootstrap-caps PATH        # custom L2 bootstrap caps
 */

#include "src/sim/electronics/intel4004/behavioral/inc/Intel4004Cpu.hpp"
#include "src/sim/electronics/intel4004/behavioral/inc/Intel4004Disassembler.hpp"
#include "src/sim/electronics/intel4004/behavioral/inc/Intel4004Instructions.hpp"
#include "src/sim/electronics/intel4004/behavioral/inc/Intel4004Programs.hpp"
#include "src/sim/electronics/intel4004/grid/inc/Intel4004Grid.hpp"
#include "src/sim/electronics/intel4004/grid/inc/Intel4004GridLevel1.hpp"
#include "src/sim/electronics/intel4004/grid/inc/Intel4004GridLevel2.hpp"
#include "src/sim/electronics/intel4004/netlist/inc/SpiceNetlistParser.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using sim::electronics::intel4004::DisassembledInstruction;
using sim::electronics::intel4004::Intel4004Cpu;
using sim::electronics::intel4004::Intel4004GridLevel1;
using sim::electronics::intel4004::Intel4004GridLevel2;
using sim::electronics::intel4004::Intel4004Netlist;
using sim::electronics::intel4004::disassemble;
using sim::electronics::intel4004::loadSpiceNetlist;

/* ----------------------------- Constants ----------------------------- */

/// Default warmup NOPs to stabilize the L1/L2 timing generator.
static constexpr std::size_t DEFAULT_WARMUP_NOPS = 16;

/// Default netlist path relative to the project root.
static constexpr const char* DEFAULT_NETLIST_PATH =
    "src/sim/electronics/intel4004/netlist/data/lajos-4004.spice";

/// Default bootstrap-cap data file relative to the project root.
static constexpr const char* DEFAULT_BOOTSTRAP_CAPS_PATH =
    "src/sim/electronics/intel4004/netlist/data/lajos-4004-bootstrap-caps.txt";

/// Default sub-steps per machine phase at L2 (caps need finer dt).
static constexpr std::size_t L2_STEPS_PER_PHASE = 5;

/// Default examples directory relative to the project root.
static constexpr const char* DEFAULT_EXAMPLES_DIR = "apps/apex_cpu_sim_demo/examples";

/// File extension for example program files.
static constexpr const char* EXAMPLE_EXTENSION = ".4004";

/* ----------------------------- CliArgs ----------------------------- */

struct CliArgs {
  std::string netlistPath;
  std::string bootstrapCapsPath;
  std::string program;        ///< Program as space-separated hex bytes.
  std::string exampleName;    ///< Example file (basename or full path).
  std::vector<std::string> probeNets;
  int level = 1;
  std::size_t warmupNops = DEFAULT_WARMUP_NOPS;
  bool listExamples = false;
};

/* ----------------------------- CLI ----------------------------- */

static void printUsage() {
  fmt::print(
      "Intel 4004 CPU Simulator Demo\n\n"
      "Usage: ApexCpuSimDemo [options]\n"
      "  --level N             Fidelity level (default: 1)\n"
      "                          0 = L0 behavioral CPU only\n"
      "                          1 = L0 + L1 component hybrid\n"
      "                          2 = L0 + L2 engineered physics (BSIM3 + caps)\n"
      "  --behavioral-only     Alias for --level 0\n"
      "  --netlist PATH        SPICE netlist path (default: built-in)\n"
      "  --bootstrap-caps PATH Bootstrap cap data file (default: built-in, L2 only)\n"
      "  --program \"HEX\"       Program as space-separated hex bytes (default: LDM 5)\n"
      "  --example NAME        Load a canned example program by name or path.\n"
      "                        Names resolve to apps/apex_cpu_sim_demo/examples/<NAME>.4004.\n"
      "  --list-examples       List the canned example programs and exit.\n"
      "  --probe NET           Probe a net (can repeat, L1/L2 only)\n"
      "  --warmup N            Warmup NOP count for L1/L2 (default: 16)\n"
      "  -h, --help            Show this help\n\n"
      "Examples:\n"
      "  ApexCpuSimDemo                            # LDM 5 at L0+L1\n"
      "  ApexCpuSimDemo --level 0                  # L0 only\n"
      "  ApexCpuSimDemo --level 2                  # L0+L2 engineered physics\n"
      "  ApexCpuSimDemo --example add              # Run the canned 'add' example\n"
      "  ApexCpuSimDemo --example my_prog.4004     # Run a local file by path\n"
      "  ApexCpuSimDemo --list-examples            # List canned examples\n"
      "  ApexCpuSimDemo --program \"D5 00 D3\"       # LDM 5, NOP, LDM 3\n"
      "  ApexCpuSimDemo --probe ACC.0 --probe D0   # Probe nets at L1/L2\n");
}

static CliArgs parseArgs(int argc, char* argv[]) {
  CliArgs args;
  args.netlistPath = DEFAULT_NETLIST_PATH;
  args.bootstrapCapsPath = DEFAULT_BOOTSTRAP_CAPS_PATH;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--netlist") == 0 && i + 1 < argc) {
      args.netlistPath = argv[++i];
    } else if (std::strcmp(argv[i], "--bootstrap-caps") == 0 && i + 1 < argc) {
      args.bootstrapCapsPath = argv[++i];
    } else if (std::strcmp(argv[i], "--program") == 0 && i + 1 < argc) {
      args.program = argv[++i];
    } else if (std::strcmp(argv[i], "--example") == 0 && i + 1 < argc) {
      args.exampleName = argv[++i];
    } else if (std::strcmp(argv[i], "--list-examples") == 0) {
      args.listExamples = true;
    } else if (std::strcmp(argv[i], "--probe") == 0 && i + 1 < argc) {
      args.probeNets.push_back(argv[++i]);
    } else if (std::strcmp(argv[i], "--level") == 0 && i + 1 < argc) {
      args.level = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--behavioral-only") == 0) {
      args.level = 0;
    } else if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
      args.warmupNops = static_cast<std::size_t>(std::atoi(argv[++i]));
    } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      printUsage();
      std::exit(0);
    }
  }

  if (args.level < 0 || args.level > 2) {
    fmt::print(stderr, "ERROR: --level must be 0, 1, or 2 (got {})\n", args.level);
    std::exit(1);
  }
  return args;
}

/* ----------------------------- File Helpers ----------------------------- */

/// Parse a hex string like "D5 00 D3" into a byte vector.
static std::vector<std::uint8_t> parseHexProgram(const std::string& hex) {
  std::vector<std::uint8_t> bytes;
  std::istringstream iss(hex);
  std::string token;
  while (iss >> token) {
    bytes.push_back(static_cast<std::uint8_t>(std::stoul(token, nullptr, 16)));
  }
  return bytes;
}

/// Resolve an `--example NAME` argument to a path on disk. Names that
/// look like a path (contain `/` or already end in the extension) are
/// returned as-is; bare names are resolved against the canonical
/// examples directory.
static std::string resolveExamplePath(const std::string& name) {
  const bool LOOKS_LIKE_PATH = name.find('/') != std::string::npos ||
                               name.size() >= 5 &&
                                   name.substr(name.size() - 5) == EXAMPLE_EXTENSION;
  if (LOOKS_LIKE_PATH) return name;
  return std::string(DEFAULT_EXAMPLES_DIR) + "/" + name + EXAMPLE_EXTENSION;
}

/// Load a `.4004` example program. Strips `#`-to-end-of-line comments,
/// captures the leading `#` block as the description, and parses the
/// remaining whitespace-separated tokens as hex bytes.
struct LoadedExample {
  std::string description; ///< Leading `#` block (one line per `#`-prefixed line, stripped).
  std::vector<std::uint8_t> bytes;
};

static LoadedExample loadExampleProgram(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) {
    fmt::print(stderr, "ERROR: cannot open example file '{}'\n", path);
    std::exit(1);
  }

  LoadedExample result;
  std::string line;
  bool inHeader = true;
  while (std::getline(f, line)) {
    // Strip a CRLF.
    if (!line.empty() && line.back() == '\r') line.pop_back();

    // Find a `#` comment; everything before it is code, everything
    // after is comment. A line starting with `#` is comment-only.
    const auto HASH = line.find('#');
    std::string code;
    std::string comment;
    if (HASH == std::string::npos) {
      code = line;
    } else {
      code = line.substr(0, HASH);
      comment = line.substr(HASH + 1);
    }

    // Trim whitespace from code.
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!code.empty() && isSpace(code.front())) code.erase(code.begin());
    while (!code.empty() && isSpace(code.back())) code.pop_back();

    // Header rule: while we still haven't seen any code bytes, every
    // commented line contributes to the description.
    if (inHeader && code.empty() && !comment.empty()) {
      // Trim leading space from the comment text.
      auto first = comment.find_first_not_of(" \t");
      if (first == std::string::npos) {
        result.description += "\n";
      } else {
        result.description += comment.substr(first);
        result.description += "\n";
      }
      continue;
    }
    if (!code.empty()) inHeader = false;

    // Parse hex tokens from the code portion.
    std::istringstream iss(code);
    std::string token;
    while (iss >> token) {
      try {
        result.bytes.push_back(static_cast<std::uint8_t>(std::stoul(token, nullptr, 16)));
      } catch (const std::exception&) {
        fmt::print(stderr, "ERROR: invalid hex token '{}' in {}\n", token, path);
        std::exit(1);
      }
    }
  }
  return result;
}

/// Print the available example programs found in the canonical
/// examples directory. Returns the count.
static std::size_t listExamples() {
  namespace fs = std::filesystem;
  const fs::path DIR(DEFAULT_EXAMPLES_DIR);
  if (!fs::exists(DIR) || !fs::is_directory(DIR)) {
    fmt::print(stderr, "ERROR: examples directory '{}' not found\n", DEFAULT_EXAMPLES_DIR);
    return 0;
  }

  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(DIR)) {
    if (entry.is_regular_file() && entry.path().extension() == EXAMPLE_EXTENSION) {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());

  fmt::print("Available examples ({}):\n", DEFAULT_EXAMPLES_DIR);
  for (const auto& f : files) {
    // Read description hint from the first commented line that isn't
    // a framing header repeating the filename.
    std::ifstream in(f);
    std::string line, hint;
    const std::string FRAMED_TITLE = f.stem().string() + EXAMPLE_EXTENSION;
    while (std::getline(in, line)) {
      auto first = line.find_first_not_of(" \t");
      if (first == std::string::npos) continue;
      if (line[first] != '#') break; // hit code, no hint found
      // Skip leading `#` and `=` framing characters.
      auto start = line.find_first_not_of("#= \t", first);
      if (start == std::string::npos) continue;
      std::string candidate = line.substr(start);
      while (!candidate.empty() &&
             (candidate.back() == '=' ||
              std::isspace(static_cast<unsigned char>(candidate.back())))) {
        candidate.pop_back();
      }
      if (candidate == FRAMED_TITLE) continue; // skip "=== name.4004 ===" header
      hint = candidate;
      break;
    }
    fmt::print("  {:<20}  {}\n", f.stem().string(), hint);
  }
  return files.size();
}

/// Build a per-byte ROM: warmupNops NOPs followed by a single program byte.
static std::vector<std::uint8_t> buildWarmupRom(std::uint8_t programByte,
                                                std::size_t warmupNops) {
  std::vector<std::uint8_t> full(warmupNops + 1);
  std::fill(full.begin(), full.begin() + warmupNops, 0x00);
  full[warmupNops] = programByte;
  return full;
}

/// Return a per-byte label for status output. The first byte of each
/// instruction gets its disassembled mnemonic; the data byte of a
/// 2-byte instruction gets a "+data" marker so it isn't misread as an
/// opcode.
static std::vector<std::string> perByteLabels(const std::vector<std::uint8_t>& program) {
  std::vector<std::string> labels(program.size());
  std::size_t i = 0;
  while (i < program.size()) {
    const auto INSTR = disassemble(program.data() + i, program.size() - i);
    labels[i] = INSTR.mnemonic;
    if (INSTR.length == 2 && i + 1 < program.size()) {
      labels[i + 1] = "+data";
    }
    i += INSTR.length;
  }
  return labels;
}

/// Print a probe table for a single transient state.
template <class Grid>
static void printProbes(Grid& grid, const std::vector<double>& voltages,
                        const std::vector<std::string>& nets, double threshold) {
  for (const auto& name : nets) {
    const auto ID = grid.findNet(name);
    if (ID > 0 && ID < voltages.size()) {
      const double V = voltages[ID];
      const char LOGIC = (V > threshold) ? 'H' : 'L';
      fmt::print("  {:<12} = {:7.3f}V  [{}]\n", name, V, LOGIC);
    } else {
      fmt::print("  {:<12} = (not found)\n", name);
    }
  }
}

static const std::vector<std::string>& defaultProbeNets() {
  static const std::vector<std::string> NETS = {
      "ACC.0", "ACC.1", "ACC.2", "ACC.3", "CY",    "D0",    "D1",
      "D2",    "D3",    "SYNC",  "M12",   "M22",   "OPA.0", "OPA.1",
      "OPA.2", "OPA.3", "OPR.0", "OPR.1", "OPR.2", "OPR.3"};
  return NETS;
}

/* ----------------------------- API ----------------------------- */

/// Snapshot of L0 CPU state observed at a particular program byte.
/// Captured per byte so the L1/L2 hybrid can seed the transistor
/// circuit with the same architectural state L0 had after executing
/// that byte.
struct L0Snapshot {
  std::uint8_t acc = 0;
  bool carry = false;
  std::array<std::uint8_t, 16> registers{};
  // RAM state mirrors Intel4004Cpu's array layout. Carrying these per-byte
  // lets the L1/L2 hybrid simulate WRM/RDM/etc. with state continuity --
  // each byte's fresh transistor circuit gets seeded with whatever RAM L0
  // observed at the same point.
  std::array<std::uint8_t, Intel4004Cpu::RAM_DATA_SIZE> ramData{};
  std::array<std::uint8_t, Intel4004Cpu::RAM_STATUS_SIZE> ramStatus{};
  std::array<std::uint8_t, Intel4004Cpu::RAM_OUTPUT_SIZE> ramOutput{};
  std::uint8_t ramBank = 0;
  std::uint8_t srcAddress = 0;
  bool visited = false; ///< True if L0's PC actually stepped through this byte.
};

/// Run L0 (behavioral CPU) over the program until PC walks off the end of
/// loaded ROM, the CPU halts, or the safety step cap is hit. Returns
/// per-byte snapshots (ACC + CY + R0..R15) for L1/L2 hybrid seeding;
/// for non-linear programs later iterations overwrite earlier slots.
static std::vector<L0Snapshot> runL0(const std::vector<std::uint8_t>& program,
                                     Intel4004Cpu& cpu) {
  fmt::print("--- L0: Behavioral CPU ---\n");
  cpu.loadProgram(program.data(), program.size());

  // Allow up to ~64 instructions per program byte for control-flow loops.
  const std::size_t MAX_STEPS = 64 * program.size() + 64;

  std::vector<L0Snapshot> perByte(program.size());
  std::size_t stepIdx = 0;
  while (!cpu.halted && cpu.pc < program.size() && stepIdx < MAX_STEPS) {
    const std::uint16_t PREV_PC = cpu.pc;
    // Decode the just-fetched instruction's length so we fill only the
    // byte slot(s) the instruction *occupies* in ROM, not the bytes
    // between the call site and the jump target for control-flow ops.
    const std::size_t REMAINING = program.size() - PREV_PC;
    const auto INSTR = disassemble(program.data() + PREV_PC, REMAINING);
    if (!cpu.step()) break;
    L0Snapshot snap;
    snap.acc = cpu.accumulator;
    snap.carry = cpu.carry;
    snap.visited = true;
    for (int r = 0; r < 16; ++r) snap.registers[r] = cpu.registers[r];
    snap.ramData = cpu.ramData;
    snap.ramStatus = cpu.ramStatus;
    snap.ramOutput = cpu.ramOutput;
    snap.ramBank = cpu.ramBank;
    snap.srcAddress = cpu.srcAddress;
    for (std::size_t b = PREV_PC; b < PREV_PC + INSTR.length && b < perByte.size(); ++b) {
      perByte[b] = snap;
    }
    ++stepIdx;
  }

  fmt::print("  Steps executed: {}\n", stepIdx);
  fmt::print("  ACC = {} (0b{:04b})\n", cpu.accumulator, cpu.accumulator);
  fmt::print("  CY  = {}\n", cpu.carry ? 1 : 0);
  for (int r = 0; r < 16; ++r) {
    if (cpu.registers[r] != 0) {
      fmt::print("  R{:<2} = {} (0b{:04b})\n", r, cpu.registers[r], cpu.registers[r]);
    }
  }
  fmt::print("\n");
  return perByte;
}

/// Seed a transistor-level grid with the architectural state captured
/// from L0: ACC, CY, and all 16 registers. Used by both runL1 and
/// runL2 to ensure each per-byte simulation starts from the same
/// state L0 had after executing the previous byte.
template <class Grid>
static void seedFromL0(Grid& grid, std::vector<double>& voltages,
                       const L0Snapshot& snap) {
  grid.forceAccLogic(voltages, snap.acc);
  grid.forceCarry(voltages, snap.carry);
  for (int r = 0; r < 16; ++r) {
    grid.forceRegisterValue(voltages, r, snap.registers[r]);
  }
  // Seed RAM/IO state too. The grid stores parallel CPU state for the
  // calculator-era 0xE-group ops (WRM, RDM, ADM, SBM, WR0..WR3,
  // RD0..RD3, WMP). Without this, each fresh per-byte grid would see
  // empty RAM and silently return zero on reads.
  grid.ramData_ = snap.ramData;
  grid.ramStatus_ = snap.ramStatus;
  grid.ramOutput_ = snap.ramOutput;
  grid.ramBank_ = snap.ramBank;
  grid.srcAddress_ = snap.srcAddress;
}

/// Run L1 (Shichman-Hodges + behavioral overlay) per byte, seeded by L0.
static int runL1(const CliArgs& args, const Intel4004Netlist& netlist,
                 const std::vector<std::uint8_t>& program,
                 const std::vector<L0Snapshot>& l0PerByte) {
  fmt::print("--- L1: Transistor Circuit (L0/L1 hybrid) ---\n");
  fmt::print("  Loading netlist: {}\n", args.netlistPath);
  fmt::print("  Transistors: {}\n", netlist.transistorCount());
  fmt::print("  Warmup NOPs per byte: {}\n\n", args.warmupNops);

  const auto& PROBE_NETS = args.probeNets.empty() ? defaultProbeNets() : args.probeNets;
  const auto LABELS = perByteLabels(program);

  L0Snapshot prevState; // zero-initialized = pre-program state
  for (std::size_t b = 0; b < program.size(); ++b) {
    fmt::print("  Byte {} (0x{:02X} {}): ", b, program[b], LABELS[b]);
    std::fflush(stdout);

    auto rom = buildWarmupRom(program[b], args.warmupNops);
    Intel4004GridLevel1 grid;
    auto circuit = grid.buildCircuit(netlist);
    grid.bsParams_.vth = 1.17;
    grid.gminTransient_ = 1e-9;

    auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), args.warmupNops, 0);
    seedFromL0(grid, state.nodeVoltages, prevState);

    grid.enableSparseModeLevel1(circuit);
    circuit.solver().invalidateCache();
    grid.traceExecuteByte(circuit, state, program[b], nullptr);

    bool healthy = true;
    for (int bit = 0; bit < 4; ++bit) {
      const auto NET = grid.accNets_[bit];
      if (NET > 0 && NET < state.nodeVoltages.size()) {
        const double V = state.nodeVoltages[NET];
        if (std::isnan(V) || std::isinf(V)) healthy = false;
      }
    }

    const std::uint8_t L1_ACC = grid.readAccumulator(state.nodeVoltages);
    fmt::print("L0 ACC={} (auth), L1 ACC={} {}\n", l0PerByte[b].acc, L1_ACC,
               healthy ? "[OK]" : "[NaN/Inf]");

    // Only advance the seeding chain when L0 actually visited this byte.
    // Never-visited filler bytes (jump-over targets, padding) leave
    // prevState unchanged so the next visited byte gets the most recent
    // valid L0 snapshot.
    if (l0PerByte[b].visited) prevState = l0PerByte[b];

    if (b == program.size() - 1) {
      fmt::print("\n--- Net Voltages (final byte, L1 state) ---\n");
      printProbes(grid, state.nodeVoltages, PROBE_NETS, Intel4004GridLevel1::THRESHOLD);
    }
  }
  return 0;
}

/// Run L2 (BSIM3 + Meyer caps + bootstrap caps) per byte, seeded by L0.
static int runL2(const CliArgs& args, const Intel4004Netlist& netlist,
                 const std::vector<std::uint8_t>& program,
                 const std::vector<L0Snapshot>& l0PerByte) {
  fmt::print("--- L2: Engineered Physics (L0/L2 hybrid) ---\n");
  fmt::print("  Loading netlist:        {}\n", args.netlistPath);
  fmt::print("  Loading bootstrap caps: {}\n", args.bootstrapCapsPath);
  fmt::print("  Transistors: {} (338 latch core via BSIM3)\n", netlist.transistorCount());
  fmt::print("  Meyer caps: ON (intrinsic + overlap on every device)\n");
  fmt::print("  Warmup NOPs per byte: {}\n", args.warmupNops);
  fmt::print("  Steps per phase: {} (caps need finer dt than L1)\n\n", L2_STEPS_PER_PHASE);

  const auto& PROBE_NETS = args.probeNets.empty() ? defaultProbeNets() : args.probeNets;
  const auto LABELS = perByteLabels(program);

  L0Snapshot prevState; // zero-initialized = pre-program state
  for (std::size_t b = 0; b < program.size(); ++b) {
    fmt::print("  Byte {} (0x{:02X} {}): ", b, program[b], LABELS[b]);
    std::fflush(stdout);

    auto rom = buildWarmupRom(program[b], args.warmupNops);
    Intel4004GridLevel2 grid;
    grid.enableMeyerCaps_ = true;
    grid.gminTransient_ = grid.gminTransientWithCaps_;
    auto circuit = grid.buildCircuit(netlist);
    grid.loadBootstrapCaps(args.bootstrapCapsPath);

    auto state = grid.simulateLevel1FromScratch(
        circuit, rom.data(), rom.size(), args.warmupNops, /*programBytes=*/0,
        /*clockPeriod=*/1e-6, L2_STEPS_PER_PHASE);
    seedFromL0(grid, state.nodeVoltages, prevState);

    grid.traceExecuteByte(circuit, state, program[b], nullptr);

    bool healthy = true;
    for (int bit = 0; bit < 4; ++bit) {
      const auto NET = grid.accNets_[bit];
      if (NET > 0 && NET < state.nodeVoltages.size()) {
        const double V = state.nodeVoltages[NET];
        if (std::isnan(V) || std::isinf(V)) healthy = false;
      }
    }

    const std::uint8_t L2_ACC = grid.readAccumulator(state.nodeVoltages);
    fmt::print("L0 ACC={} (auth), L2 ACC={} {}\n", l0PerByte[b].acc, L2_ACC,
               healthy ? "[OK]" : "[NaN/Inf]");

    // Only advance the seeding chain when L0 actually visited this byte.
    // Never-visited filler bytes (jump-over targets, padding) leave
    // prevState unchanged so the next visited byte gets the most recent
    // valid L0 snapshot.
    if (l0PerByte[b].visited) prevState = l0PerByte[b];

    if (b == program.size() - 1) {
      fmt::print("\n--- Net Voltages (final byte, L2 state) ---\n");
      printProbes(grid, state.nodeVoltages, PROBE_NETS, Intel4004GridLevel1::THRESHOLD);
    }
  }
  return 0;
}

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const auto ARGS = parseArgs(argc, argv);

  if (ARGS.listExamples) {
    listExamples();
    return 0;
  }

  const char* LEVEL_LABEL = (ARGS.level == 0)   ? "L0 (behavioral)"
                            : (ARGS.level == 1) ? "L0 + L1 (transistor hybrid)"
                                                : "L0 + L2 (engineered physics)";

  fmt::print("=======================================================\n");
  fmt::print("  Intel 4004 CPU Simulator\n");
  fmt::print("  Level: {}\n", LEVEL_LABEL);
  if (ARGS.level >= 1) {
    fmt::print("  Circuit: 2,242 transistors | 1,081 nets | 427 gates\n");
  }
  fmt::print("=======================================================\n\n");

  std::vector<std::uint8_t> program;
  if (!ARGS.exampleName.empty()) {
    const auto PATH = resolveExamplePath(ARGS.exampleName);
    fmt::print("Example: {}\n", PATH);
    auto loaded = loadExampleProgram(PATH);
    if (!loaded.description.empty()) {
      fmt::print("\n{}\n", loaded.description);
    }
    program = std::move(loaded.bytes);
  } else if (!ARGS.program.empty()) {
    program = parseHexProgram(ARGS.program);
  } else {
    using namespace sim::electronics::intel4004;
    program.assign(PROGRAM_LDM.begin(), PROGRAM_LDM.end());
  }

  fmt::print("Program ({} bytes):\n", program.size());
  for (std::size_t i = 0; i < program.size();) {
    const auto INSTR = disassemble(program.data() + i, program.size() - i);
    if (INSTR.length == 2) {
      fmt::print("  {:04X}: {:02X} {:02X}  {}\n", i, program[i], program[i + 1], INSTR.mnemonic);
    } else {
      fmt::print("  {:04X}: {:02X}     {}\n", i, program[i], INSTR.mnemonic);
    }
    i += INSTR.length;
  }
  fmt::print("\n");

  Intel4004Cpu behavioral;
  const auto L0_PER_BYTE = runL0(program, behavioral);

  if (ARGS.level == 0) {
    fmt::print("=======================================================\n");
    fmt::print("  L0 simulation complete.\n");
    fmt::print("=======================================================\n");
    return 0;
  }

  const auto NETLIST = loadSpiceNetlist(ARGS.netlistPath);

  int rc = 0;
  if (ARGS.level == 1) {
    rc = runL1(ARGS, NETLIST, program, L0_PER_BYTE);
  } else { // level == 2
    rc = runL2(ARGS, NETLIST, program, L0_PER_BYTE);
  }

  fmt::print("\n=======================================================\n");
  fmt::print("  Simulation complete.\n");
  fmt::print("=======================================================\n");
  return rc;
}
