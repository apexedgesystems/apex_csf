#ifndef APEX_SIM_ELECTRONICS_CPU_INTEL4004_CPU_HPP
#define APEX_SIM_ELECTRONICS_CPU_INTEL4004_CPU_HPP
/**
 * @file Intel4004Cpu.hpp
 * @brief Behavioral model of the Intel 4004 CPU.
 *
 * Architecture:
 * - 16 x 4-bit index registers (R0-R15), organized as 8 register pairs
 * - 4-bit accumulator with carry flag
 * - 12-bit program counter (4096-byte ROM address space)
 * - 3-level hardware subroutine stack (wrapping)
 * - 46 instructions (41 one-byte, 5 two-byte)
 * - Internal RAM: 1024 data nibbles + 256 status nibbles + 16 output ports
 * - SRC/DCL addressing for RAM bank/chip/register selection
 *
 * This is the golden reference for verifying the transistor-level circuit
 * simulation (Phase 4 behavioral vs circuit comparison).
 *
 * @note RT-safe after loadProgram(): All methods are pure computation.
 */

#include "src/sim/electronics/intel4004/behavioral/inc/Intel4004Instructions.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace sim::electronics::intel4004 {

/* ----------------------------- Intel4004Cpu ----------------------------- */

/**
 * @class Intel4004Cpu
 * @brief Behavioral 4-bit CPU with 46 instructions.
 *
 * Fetch-decode-execute in a single step() call. No pipeline, no cache.
 * Designed for standalone testing and as a reference for circuit verification.
 *
 * @note RT-safe: All methods are pure computation after loadProgram().
 */
class Intel4004Cpu {
public:
  /* ----------------------------- Constants ----------------------------- */

  static constexpr std::size_t NUM_REGISTERS = 16;    ///< R0-R15.
  static constexpr std::size_t NUM_PAIRS = 8;         ///< 8 register pairs.
  static constexpr std::size_t ROM_SIZE = 4096;       ///< 4K byte ROM.
  static constexpr std::size_t STACK_DEPTH = 3;       ///< 3-level subroutine stack.
  static constexpr std::uint8_t DATA_MASK = 0xF;      ///< 4-bit data mask.
  static constexpr std::uint16_t ADDR_MASK = 0xFFF;   ///< 12-bit address mask.
  static constexpr std::size_t RAM_DATA_SIZE = 1024;  ///< Data nibbles.
  static constexpr std::size_t RAM_STATUS_SIZE = 256; ///< Status nibbles.
  static constexpr std::size_t RAM_OUTPUT_SIZE = 16;  ///< Output port nibbles.

  /* ----------------------------- State ----------------------------- */

  std::array<std::uint8_t, NUM_REGISTERS> registers{}; ///< R0-R15, 4-bit each.
  std::uint8_t accumulator = 0;                        ///< 4-bit accumulator.
  bool carry = false;                                  ///< Carry/borrow flag.
  std::uint16_t pc = 0;                                ///< 12-bit program counter.
  std::array<std::uint16_t, STACK_DEPTH> stack{};      ///< 3-level subroutine stack.
  std::uint8_t sp = 0;                                 ///< Stack pointer (0-2, wraps).
  bool halted = false;                                 ///< Software halt (no HW halt on 4004).
  std::array<std::uint8_t, ROM_SIZE> rom{};            ///< Program ROM.

  std::array<std::uint8_t, RAM_DATA_SIZE> ramData{};     ///< RAM data nibbles.
  std::array<std::uint8_t, RAM_STATUS_SIZE> ramStatus{}; ///< RAM status characters.
  std::array<std::uint8_t, RAM_OUTPUT_SIZE> ramOutput{}; ///< RAM output ports.
  std::uint8_t srcAddress = 0;                           ///< SRC address register (8-bit).
  std::uint8_t ramBank = 0;                              ///< DCL RAM bank select (0-7).
  bool testPin = false;                                  ///< TEST input pin.

  std::size_t cyclesExecuted = 0;       ///< Total machine cycles.
  std::size_t instructionsExecuted = 0; ///< Total instructions executed.

  /* ----------------------------- API ----------------------------- */

  /**
   * @brief Load a program into ROM starting at address 0.
   * @param program Program bytes.
   * @param len Number of bytes to load (up to ROM_SIZE).
   * @note NOT RT-safe: Array copy.
   */
  void loadProgram(const std::uint8_t* program, std::size_t len) noexcept {
    for (std::size_t i = 0; i < len && i < ROM_SIZE; ++i) {
      rom[i] = program[i];
    }
  }

  /**
   * @brief Execute one instruction cycle.
   * @return true if CPU is still running, false if halted.
   *
   * Performs fetch-decode-execute in a single call. Two-byte instructions
   * consume both bytes and advance PC by 2.
   *
   * @note RT-safe: Pure arithmetic, bounded execution.
   */
  bool step() noexcept {
    if (halted) {
      return false;
    }

    const std::uint8_t BYTE1 = rom[pc & ADDR_MASK];
    const std::uint8_t GROUP = (BYTE1 >> 4) & 0xF;
    const std::uint8_t LOW = BYTE1 & 0xF;

    std::uint16_t nextPc = (pc + 1) & ADDR_MASK;

    switch (GROUP) {
    case 0x0: // NOP
      break;

    case 0x1: { // JCN -- jump on condition
      const std::uint8_t ADDR_LO = rom[nextPc];
      nextPc = (nextPc + 1) & ADDR_MASK;

      bool condition = false;
      if (LOW & COND_ACC_ZERO) {
        condition = condition || (accumulator == 0);
      }
      if (LOW & COND_CARRY) {
        condition = condition || carry;
      }
      if (LOW & COND_TEST) {
        condition = condition || !testPin;
      }
      if (LOW & COND_INVERT) {
        condition = !condition;
      }

      if (condition) {
        nextPc = (pc & 0xF00) | ADDR_LO;
      }
      break;
    }

    case 0x2: { // FIM or SRC
      if ((BYTE1 & 0x1) == 0) {
        // FIM -- fetch immediate to register pair (2-byte)
        const std::uint8_t PAIR = (LOW >> 1) & 0x7;
        const std::uint8_t DATA = rom[nextPc];
        nextPc = (nextPc + 1) & ADDR_MASK;
        setRegisterPair(PAIR, DATA);
      } else {
        // SRC -- send register control (1-byte)
        const std::uint8_t PAIR = (LOW >> 1) & 0x7;
        srcAddress = getRegisterPairValue(PAIR);
      }
      break;
    }

    case 0x3: { // FIN or JIN
      if ((BYTE1 & 0x1) == 0) {
        // FIN -- fetch indirect from ROM to register pair (1-byte)
        const std::uint8_t PAIR = (LOW >> 1) & 0x7;
        const std::uint16_t ROM_ADDR = (pc & 0xF00) | getRegisterPairValue(0);
        const std::uint8_t DATA = rom[ROM_ADDR & ADDR_MASK];
        setRegisterPair(PAIR, DATA);
      } else {
        // JIN -- jump indirect (1-byte)
        const std::uint8_t PAIR = (LOW >> 1) & 0x7;
        nextPc = (pc & 0xF00) | getRegisterPairValue(PAIR);
      }
      break;
    }

    case 0x4: { // JUN -- jump unconditional (2-byte)
      const std::uint8_t ADDR_HI = LOW;
      const std::uint8_t ADDR_LO = rom[nextPc];
      nextPc = static_cast<std::uint16_t>((ADDR_HI << 8) | ADDR_LO);
      break;
    }

    case 0x5: { // JMS -- jump to subroutine (2-byte)
      const std::uint8_t ADDR_HI = LOW;
      const std::uint8_t ADDR_LO = rom[nextPc];
      const std::uint16_t RETURN_ADDR = (nextPc + 1) & ADDR_MASK;
      pushStack(RETURN_ADDR);
      nextPc = static_cast<std::uint16_t>((ADDR_HI << 8) | ADDR_LO);
      break;
    }

    case 0x6: // INC -- increment register (1-byte)
      registers[LOW] = (registers[LOW] + 1) & DATA_MASK;
      break;

    case 0x7: { // ISZ -- increment and skip if zero (2-byte)
      const std::uint8_t ADDR_LO = rom[nextPc];
      nextPc = (nextPc + 1) & ADDR_MASK;
      registers[LOW] = (registers[LOW] + 1) & DATA_MASK;
      if (registers[LOW] != 0) {
        nextPc = (pc & 0xF00) | ADDR_LO;
      }
      break;
    }

    case 0x8: { // ADD -- add register to accumulator with carry
      const std::uint16_t SUM =
          static_cast<std::uint16_t>(accumulator) + registers[LOW] + (carry ? 1 : 0);
      accumulator = static_cast<std::uint8_t>(SUM & DATA_MASK);
      carry = (SUM > 0xF);
      break;
    }

    case 0x9: { // SUB -- subtract register with borrow
      // SUB: ACC = ACC + ~Rr + CY  (carry=1 means no borrow)
      const std::uint16_t DIFF =
          static_cast<std::uint16_t>(accumulator) + (~registers[LOW] & DATA_MASK) + (carry ? 1 : 0);
      accumulator = static_cast<std::uint8_t>(DIFF & DATA_MASK);
      carry = (DIFF > 0xF);
      break;
    }

    case 0xA: // LD -- load register to accumulator
      accumulator = registers[LOW];
      break;

    case 0xB: { // XCH -- exchange register and accumulator
      const std::uint8_t TMP = accumulator;
      accumulator = registers[LOW];
      registers[LOW] = TMP;
      break;
    }

    case 0xC: // BBL -- branch back and load
      accumulator = LOW;
      nextPc = popStack();
      break;

    case 0xD: // LDM -- load immediate to accumulator
      accumulator = LOW;
      break;

    case 0xE: // I/O operations
      executeIO(BYTE1);
      break;

    case 0xF: // Accumulator operations
      executeAccumulator(BYTE1);
      break;

    default:
      break;
    }

    pc = nextPc;
    ++cyclesExecuted;
    ++instructionsExecuted;
    return !halted;
  }

  /**
   * @brief Run until halted or max cycles reached.
   * @param maxCycles Maximum number of cycles before forced stop.
   * @note RT-safe: Bounded loop.
   */
  void run(std::size_t maxCycles = 100000) noexcept {
    while (!halted && cyclesExecuted < maxCycles) {
      step();
    }
  }

  /**
   * @brief Reset CPU state to power-on defaults. ROM is preserved.
   * @note RT-safe: Array fill + assignment.
   */
  void reset() noexcept {
    registers.fill(0);
    accumulator = 0;
    carry = false;
    pc = 0;
    stack.fill(0);
    sp = 0;
    halted = false;
    ramData.fill(0);
    ramStatus.fill(0);
    ramOutput.fill(0);
    srcAddress = 0;
    ramBank = 0;
    testPin = false;
    cyclesExecuted = 0;
    instructionsExecuted = 0;
  }

  /* ----------------------------- Register Pair Helpers ----------------------------- */

  /**
   * @brief Get 8-bit value from a register pair.
   *
   * Pair P0 = (R0:R1), P1 = (R2:R3), ..., P7 = (R14:R15).
   * High nibble from even register, low nibble from odd register.
   *
   * @param pair Register pair index (0-7).
   * @return 8-bit value (high:low nibble).
   * @note RT-safe: Pure arithmetic.
   */
  std::uint8_t getRegisterPairValue(std::uint8_t pair) const noexcept {
    const std::uint8_t IDX = (pair & 0x7) * 2;
    return static_cast<std::uint8_t>((registers[IDX] << 4) | registers[IDX + 1]);
  }

  /**
   * @brief Set a register pair from an 8-bit value.
   * @param pair Register pair index (0-7).
   * @param value 8-bit value to split across the pair.
   * @note RT-safe: Pure arithmetic.
   */
  void setRegisterPair(std::uint8_t pair, std::uint8_t value) noexcept {
    const std::uint8_t IDX = (pair & 0x7) * 2;
    registers[IDX] = (value >> 4) & DATA_MASK;
    registers[IDX + 1] = value & DATA_MASK;
  }

private:
  /* ----------------------------- Stack Operations ----------------------------- */

  void pushStack(std::uint16_t addr) noexcept {
    stack[sp] = addr;
    sp = (sp + 1) % STACK_DEPTH;
  }

  std::uint16_t popStack() noexcept {
    sp = (sp + STACK_DEPTH - 1) % STACK_DEPTH;
    return stack[sp];
  }

  /* ----------------------------- RAM Address Helpers ----------------------------- */

  std::size_t ramDataAddr() const noexcept {
    return static_cast<std::size_t>((ramBank & 0x3) * 256) + srcAddress;
  }

  std::size_t ramStatusAddr(std::uint8_t statusReg) const noexcept {
    const std::size_t BASE =
        static_cast<std::size_t>((ramBank & 0x3) * 64) + ((srcAddress >> 4) & 0xF) * 4;
    return BASE + (statusReg & 0x3);
  }

  std::size_t ramOutputAddr() const noexcept {
    return static_cast<std::size_t>((ramBank & 0x3) * 4) + ((srcAddress >> 6) & 0x3);
  }

  /* ----------------------------- I/O Operations ----------------------------- */

  void executeIO(std::uint8_t byte) noexcept {
    switch (byte) {
    case WRM:
      ramData[ramDataAddr()] = accumulator;
      break;
    case WMP:
      ramOutput[ramOutputAddr()] = accumulator;
      break;
    case WRR:
      break; // ROM port write (external, no-op in behavioral model)
    case WPM:
      break; // Program RAM write (external, no-op)
    case WR0:
      ramStatus[ramStatusAddr(0)] = accumulator;
      break;
    case WR1:
      ramStatus[ramStatusAddr(1)] = accumulator;
      break;
    case WR2:
      ramStatus[ramStatusAddr(2)] = accumulator;
      break;
    case WR3:
      ramStatus[ramStatusAddr(3)] = accumulator;
      break;
    case SBM: {
      const std::uint16_t DIFF = static_cast<std::uint16_t>(accumulator) +
                                 (~ramData[ramDataAddr()] & DATA_MASK) + (carry ? 1 : 0);
      accumulator = static_cast<std::uint8_t>(DIFF & DATA_MASK);
      carry = (DIFF > 0xF);
      break;
    }
    case RDM:
      accumulator = ramData[ramDataAddr()] & DATA_MASK;
      break;
    case RDR:
      accumulator = 0;
      break; // ROM port read (external, returns 0)
    case ADM: {
      const std::uint16_t SUM =
          static_cast<std::uint16_t>(accumulator) + ramData[ramDataAddr()] + (carry ? 1 : 0);
      accumulator = static_cast<std::uint8_t>(SUM & DATA_MASK);
      carry = (SUM > 0xF);
      break;
    }
    case RD0:
      accumulator = ramStatus[ramStatusAddr(0)] & DATA_MASK;
      break;
    case RD1:
      accumulator = ramStatus[ramStatusAddr(1)] & DATA_MASK;
      break;
    case RD2:
      accumulator = ramStatus[ramStatusAddr(2)] & DATA_MASK;
      break;
    case RD3:
      accumulator = ramStatus[ramStatusAddr(3)] & DATA_MASK;
      break;
    default:
      break;
    }
  }

  /* ----------------------------- Accumulator Operations ----------------------------- */

  void executeAccumulator(std::uint8_t byte) noexcept {
    switch (byte) {
    case CLB:
      accumulator = 0;
      carry = false;
      break;

    case CLC:
      carry = false;
      break;

    case IAC: {
      const std::uint16_t SUM = static_cast<std::uint16_t>(accumulator) + 1;
      accumulator = static_cast<std::uint8_t>(SUM & DATA_MASK);
      carry = (SUM > 0xF);
      break;
    }

    case CMC:
      carry = !carry;
      break;

    case CMA:
      accumulator = (~accumulator) & DATA_MASK;
      break;

    case RAL: {
      const std::uint8_t OLD_CARRY = carry ? 1 : 0;
      carry = (accumulator & 0x8) != 0;
      accumulator = ((accumulator << 1) | OLD_CARRY) & DATA_MASK;
      break;
    }

    case RAR: {
      const std::uint8_t OLD_CARRY = carry ? 1 : 0;
      carry = (accumulator & 0x1) != 0;
      accumulator = ((accumulator >> 1) | (OLD_CARRY << 3)) & DATA_MASK;
      break;
    }

    case TCC:
      accumulator = carry ? 1 : 0;
      carry = false;
      break;

    case DAC: {
      // Decrement: ACC + 15 (mod 16). Carry = 1 if no borrow (ACC > 0).
      const std::uint16_t DIFF = static_cast<std::uint16_t>(accumulator) + 0xF;
      accumulator = static_cast<std::uint8_t>(DIFF & DATA_MASK);
      carry = (DIFF > 0xF);
      break;
    }

    case TCS:
      // Transfer carry subtract: 10 if carry, 9 if no carry. Clear carry.
      accumulator = carry ? 10 : 9;
      carry = false;
      break;

    case STC:
      carry = true;
      break;

    case DAA: {
      // Decimal adjust: add 6 if ACC > 9 or carry is set.
      if (accumulator > 9 || carry) {
        const std::uint16_t SUM = static_cast<std::uint16_t>(accumulator) + 6;
        accumulator = static_cast<std::uint8_t>(SUM & DATA_MASK);
        if (SUM > 0xF) {
          carry = true;
        }
      }
      break;
    }

    case KBP:
      // Keyboard process: one-hot to binary conversion.
      switch (accumulator) {
      case 0x0:
        accumulator = 0;
        break;
      case 0x1:
        accumulator = 1;
        break;
      case 0x2:
        accumulator = 2;
        break;
      case 0x4:
        accumulator = 3;
        break;
      case 0x8:
        accumulator = 4;
        break;
      default:
        accumulator = 0xF;
        break;
      }
      break;

    case DCL:
      ramBank = accumulator & 0x7;
      break;

    default:
      break;
    }
  }
};

} // namespace sim::electronics::intel4004

#endif // APEX_SIM_ELECTRONICS_CPU_INTEL4004_CPU_HPP
