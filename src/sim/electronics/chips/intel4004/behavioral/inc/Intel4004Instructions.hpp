#ifndef APEX_INTEL4004INSTRUCTIONS_HPP
#define APEX_INTEL4004INSTRUCTIONS_HPP
/**
 * @file Intel4004Instructions.hpp
 * @brief Instruction encoding for the Intel 4004 CPU.
 *
 * The 4004 has 46 instructions: 41 one-byte and 5 two-byte.
 *
 * Instruction word format (upper nibble = group):
 * - 0x0: NOP
 * - 0x1: JCN cc,addr    (2-byte: jump on condition)
 * - 0x2: FIM Pp,data    (2-byte: fetch immediate, even bit 0)
 *         SRC Pp         (1-byte: send register control, odd bit 0)
 * - 0x3: FIN Pp         (1-byte: fetch indirect, even bit 0)
 *         JIN Pp         (1-byte: jump indirect, odd bit 0)
 * - 0x4: JUN addr       (2-byte: jump unconditional, 12-bit)
 * - 0x5: JMS addr       (2-byte: jump to subroutine, 12-bit)
 * - 0x6: INC Rr         (1-byte: increment register)
 * - 0x7: ISZ Rr,addr    (2-byte: increment and skip if zero)
 * - 0x8: ADD Rr         (1-byte: add register to accumulator)
 * - 0x9: SUB Rr         (1-byte: subtract register with borrow)
 * - 0xA: LD Rr          (1-byte: load register to accumulator)
 * - 0xB: XCH Rr         (1-byte: exchange register and accumulator)
 * - 0xC: BBL d          (1-byte: branch back and load)
 * - 0xD: LDM d          (1-byte: load immediate to accumulator)
 * - 0xE: I/O operations (1-byte: RAM/ROM read/write)
 * - 0xF: Accumulator    (1-byte: CLB, CLC, IAC, CMC, CMA, ...)
 *
 * All encoding helpers are RT-safe: pure constexpr arithmetic.
 */

#include <cstdint>

namespace sim::electronics::chips::intel4004 {

/* ----------------------------- NOP ----------------------------- */

inline constexpr std::uint8_t NOP = 0x00; ///< No operation.

/* ----------------------------- Two-Byte Instruction Encoders ----------------------------- */

/**
 * @brief Encode JCN (jump on condition). Returns first byte.
 * @param cond 4-bit condition code (see COND_* constants).
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeJCN(std::uint8_t cond) {
  return static_cast<std::uint8_t>(0x10 | (cond & 0xF));
}

/**
 * @brief Encode FIM (fetch immediate to register pair). Returns first byte.
 * @param pair Register pair index (0-7).
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeFIM(std::uint8_t pair) {
  return static_cast<std::uint8_t>(0x20 | ((pair & 0x7) << 1));
}

/**
 * @brief Encode JUN (jump unconditional). Returns first byte.
 * @param addr 12-bit target address. Upper 4 bits encoded in first byte.
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeJUN(std::uint16_t addr) {
  return static_cast<std::uint8_t>(0x40 | ((addr >> 8) & 0xF));
}

/**
 * @brief Encode JUN second byte (lower 8 bits of address).
 * @param addr 12-bit target address.
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeJUNLo(std::uint16_t addr) {
  return static_cast<std::uint8_t>(addr & 0xFF);
}

/**
 * @brief Encode JMS (jump to subroutine). Returns first byte.
 * @param addr 12-bit target address.
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeJMS(std::uint16_t addr) {
  return static_cast<std::uint8_t>(0x50 | ((addr >> 8) & 0xF));
}

/**
 * @brief Encode JMS second byte (lower 8 bits of address).
 * @param addr 12-bit target address.
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeJMSLo(std::uint16_t addr) {
  return static_cast<std::uint8_t>(addr & 0xFF);
}

/**
 * @brief Encode ISZ (increment register, skip if zero). Returns first byte.
 * @param reg Register index (0-15).
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeISZ(std::uint8_t reg) {
  return static_cast<std::uint8_t>(0x70 | (reg & 0xF));
}

/* ----------------------------- One-Byte Register Pair Encoders ----------------------------- */

/**
 * @brief Encode SRC (send register control).
 * @param pair Register pair index (0-7).
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeSRC(std::uint8_t pair) {
  return static_cast<std::uint8_t>(0x21 | ((pair & 0x7) << 1));
}

/**
 * @brief Encode FIN (fetch indirect from ROM to register pair).
 * @param pair Register pair index (0-7).
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeFIN(std::uint8_t pair) {
  return static_cast<std::uint8_t>(0x30 | ((pair & 0x7) << 1));
}

/**
 * @brief Encode JIN (jump indirect via register pair).
 * @param pair Register pair index (0-7).
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeJIN(std::uint8_t pair) {
  return static_cast<std::uint8_t>(0x31 | ((pair & 0x7) << 1));
}

/* ----------------------------- One-Byte Register Encoders ----------------------------- */

/**
 * @brief Encode INC (increment register).
 * @param reg Register index (0-15).
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeINC(std::uint8_t reg) {
  return static_cast<std::uint8_t>(0x60 | (reg & 0xF));
}

/**
 * @brief Encode ADD (add register to accumulator with carry).
 * @param reg Register index (0-15).
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeADD(std::uint8_t reg) {
  return static_cast<std::uint8_t>(0x80 | (reg & 0xF));
}

/**
 * @brief Encode SUB (subtract register from accumulator with borrow).
 *
 * SUB uses ones' complement subtraction: ACC + ~Rr + CY.
 * For standalone subtraction, set carry first (STC) so borrow = 0.
 *
 * @param reg Register index (0-15).
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeSUB(std::uint8_t reg) {
  return static_cast<std::uint8_t>(0x90 | (reg & 0xF));
}

/**
 * @brief Encode LD (load register to accumulator).
 * @param reg Register index (0-15).
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeLD(std::uint8_t reg) {
  return static_cast<std::uint8_t>(0xA0 | (reg & 0xF));
}

/**
 * @brief Encode XCH (exchange register and accumulator).
 * @param reg Register index (0-15).
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeXCH(std::uint8_t reg) {
  return static_cast<std::uint8_t>(0xB0 | (reg & 0xF));
}

/**
 * @brief Encode BBL (branch back and load data to accumulator).
 * @param data 4-bit data to load into accumulator on return.
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeBBL(std::uint8_t data) {
  return static_cast<std::uint8_t>(0xC0 | (data & 0xF));
}

/**
 * @brief Encode LDM (load immediate data to accumulator).
 * @param data 4-bit immediate value.
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t encodeLDM(std::uint8_t data) {
  return static_cast<std::uint8_t>(0xD0 | (data & 0xF));
}

/* ----------------------------- I/O Operations ----------------------------- */

inline constexpr std::uint8_t WRM = 0xE0; ///< Write accumulator to RAM data.
inline constexpr std::uint8_t WMP = 0xE1; ///< Write accumulator to RAM output port.
inline constexpr std::uint8_t WRR = 0xE2; ///< Write accumulator to ROM output port.
inline constexpr std::uint8_t WPM = 0xE3; ///< Write accumulator to program RAM.
inline constexpr std::uint8_t WR0 = 0xE4; ///< Write accumulator to RAM status char 0.
inline constexpr std::uint8_t WR1 = 0xE5; ///< Write accumulator to RAM status char 1.
inline constexpr std::uint8_t WR2 = 0xE6; ///< Write accumulator to RAM status char 2.
inline constexpr std::uint8_t WR3 = 0xE7; ///< Write accumulator to RAM status char 3.
inline constexpr std::uint8_t SBM = 0xE8; ///< Subtract RAM data from accumulator.
inline constexpr std::uint8_t RDM = 0xE9; ///< Read RAM data to accumulator.
inline constexpr std::uint8_t RDR = 0xEA; ///< Read ROM port to accumulator.
inline constexpr std::uint8_t ADM = 0xEB; ///< Add RAM data to accumulator.
inline constexpr std::uint8_t RD0 = 0xEC; ///< Read RAM status char 0 to accumulator.
inline constexpr std::uint8_t RD1 = 0xED; ///< Read RAM status char 1 to accumulator.
inline constexpr std::uint8_t RD2 = 0xEE; ///< Read RAM status char 2 to accumulator.
inline constexpr std::uint8_t RD3 = 0xEF; ///< Read RAM status char 3 to accumulator.

/* ----------------------------- Accumulator Operations ----------------------------- */

inline constexpr std::uint8_t CLB = 0xF0; ///< Clear both accumulator and carry.
inline constexpr std::uint8_t CLC = 0xF1; ///< Clear carry.
inline constexpr std::uint8_t IAC = 0xF2; ///< Increment accumulator.
inline constexpr std::uint8_t CMC = 0xF3; ///< Complement carry.
inline constexpr std::uint8_t CMA = 0xF4; ///< Complement accumulator.
inline constexpr std::uint8_t RAL = 0xF5; ///< Rotate accumulator left through carry.
inline constexpr std::uint8_t RAR = 0xF6; ///< Rotate accumulator right through carry.
inline constexpr std::uint8_t TCC = 0xF7; ///< Transfer carry to accumulator, clear carry.
inline constexpr std::uint8_t DAC = 0xF8; ///< Decrement accumulator.
inline constexpr std::uint8_t TCS = 0xF9; ///< Transfer carry subtract, clear carry.
inline constexpr std::uint8_t STC = 0xFA; ///< Set carry.
inline constexpr std::uint8_t DAA = 0xFB; ///< Decimal adjust accumulator.
inline constexpr std::uint8_t KBP = 0xFC; ///< Keyboard process (one-hot to binary).
inline constexpr std::uint8_t DCL = 0xFD; ///< Designate command line (RAM bank select).

/* ----------------------------- JCN Condition Bits ----------------------------- */

inline constexpr std::uint8_t COND_INVERT = 0x8;   ///< Invert condition result.
inline constexpr std::uint8_t COND_ACC_ZERO = 0x4; ///< Test accumulator == 0.
inline constexpr std::uint8_t COND_CARRY = 0x2;    ///< Test carry == 1.
inline constexpr std::uint8_t COND_TEST = 0x1;     ///< Test pin == 0 (active low).

/* ----------------------------- Decode Helpers ----------------------------- */

/**
 * @brief Check if an instruction byte begins a 2-byte instruction.
 * @param byte First instruction byte.
 * @return true if a second byte follows.
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr bool isTwoByteInstruction(std::uint8_t byte) {
  const std::uint8_t GROUP = (byte >> 4) & 0xF;
  switch (GROUP) {
  case 0x1:
    return true; // JCN
  case 0x2:
    return (byte & 0x1) == 0; // FIM (even), SRC is 1-byte (odd)
  // NOLINTNEXTLINE(bugprone-branch-clone): per-opcode ISA rows, kept distinct
  case 0x4:
    return true; // JUN
  case 0x5:
    return true; // JMS
  case 0x7:
    return true; // ISZ
  default:
    return false;
  }
}

/**
 * @brief Extract register index from instruction byte (bits 3:0).
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t decodeRegister(std::uint8_t byte) { return byte & 0xF; }

/**
 * @brief Extract register pair index from instruction byte (bits 3:1).
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t decodeRegisterPair(std::uint8_t byte) { return (byte >> 1) & 0x7; }

/**
 * @brief Extract 4-bit data from instruction byte (bits 3:0).
 * @note RT-safe: Pure arithmetic.
 */
inline constexpr std::uint8_t decodeData4(std::uint8_t byte) { return byte & 0xF; }

} // namespace sim::electronics::chips::intel4004

#endif // APEX_INTEL4004INSTRUCTIONS_HPP
