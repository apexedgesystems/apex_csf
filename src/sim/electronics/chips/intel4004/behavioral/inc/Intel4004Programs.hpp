#ifndef APEX_INTEL4004PROGRAMS_HPP
#define APEX_INTEL4004PROGRAMS_HPP
/**
 * @file Intel4004Programs.hpp
 * @brief Pre-built test programs for the Intel 4004 behavioral model.
 *
 * Each program is a constexpr byte array that can be loaded via loadProgram().
 * Programs are designed for unit testing individual instructions and sequences.
 *
 * @note RT-safe: All data is constexpr.
 */

#include "src/sim/electronics/chips/intel4004/behavioral/inc/Intel4004Instructions.hpp"

#include <array>
#include <cstdint>

namespace sim::electronics::chips::intel4004 {

/* ----------------------------- Test Programs ----------------------------- */

/// NOP chain (8 NOPs). Exercises basic fetch-increment.
inline constexpr std::array<std::uint8_t, 8> PROGRAM_NOP = {NOP, NOP, NOP, NOP, NOP, NOP, NOP, NOP};

/// Load immediate: LDM 5 -> accumulator = 5.
inline constexpr std::array<std::uint8_t, 1> PROGRAM_LDM = {encodeLDM(5)};

/// Add two registers.
/// FIM P0, 0x35 -> R0=3, R1=5; LD R0; ADD R1 -> ACC = 3+5 = 8.
/// Initial carry is 0, so ADD does not include carry input.
inline constexpr std::array<std::uint8_t, 5> PROGRAM_ADD = {
    encodeFIM(0), 0x35, // R0 = 3, R1 = 5
    encodeLD(0),        // ACC = R0 = 3
    encodeADD(1),       // ACC = ACC + R1 + 0 = 3 + 5 = 8
    encodeLDM(0)        // Sentinel
};

/// Subtract two registers.
/// FIM P0, 0x93 -> R0=9, R1=3; LD R0; STC; SUB R1 -> ACC = 9-3 = 6.
/// STC sets carry=1 (no borrow). SUB: ACC + ~R1 + carry = 9 + 12 + 1 = 22 -> ACC=6.
inline constexpr std::array<std::uint8_t, 6> PROGRAM_SUB = {
    encodeFIM(0), 0x93, // R0 = 9, R1 = 3
    encodeLD(0),        // ACC = R0 = 9
    STC,                // Set carry (clear borrow)
    encodeSUB(1),       // ACC = 9 - 3 = 6, carry = 1
    encodeLDM(0)        // Sentinel
};

/// Jump unconditional: LDM 1; JUN 0x000 -> infinite loop at address 0.
/// After 2 iterations, ACC = 1 and PC cycles between 0 and 1.
inline constexpr std::array<std::uint8_t, 4> PROGRAM_JUN = {
    encodeLDM(1),          // ACC = 1
    encodeJUN(0x000), 0x00 // JUN to address 0 (loops back)
};

/// Subroutine call and return.
/// Addr 0: JMS 0x004  (call subroutine at address 4)
/// Addr 2: LDM 0      (after return -- sentinel)
/// Addr 3: NOP         (padding)
/// Addr 4: LDM 7      (subroutine body: set ACC = 7)
/// Addr 5: BBL 3       (return, set ACC = 3)
inline constexpr std::array<std::uint8_t, 6> PROGRAM_SUBROUTINE = {
    encodeJMS(0x004),
    0x04,         // JMS to address 0x004
    encodeLDM(0), // After return: sentinel
    NOP,          // Padding
    encodeLDM(7), // Subroutine: ACC = 7
    encodeBBL(3)  // Return with ACC = 3
};

/// Counting loop using ISZ.
/// FIM P0, 0x00 -> R0=0, R1=0.
/// ISZ R0, 0x02 -> increment R0, jump back if R0 != 0.
/// Loop runs 16 times (R0: 0->1->2->...->15->0), falls through when R0 wraps.
inline constexpr std::array<std::uint8_t, 4> PROGRAM_COUNTING_LOOP = {
    encodeFIM(0), 0x00, // R0 = 0, R1 = 0
    encodeISZ(0), 0x02  // INC R0, jump to 0x02 if R0 != 0
};

/// Accumulator operations chain.
/// LDM 5 -> IAC -> CMA -> RAL -> RAR -> CLB -> STC -> TCC.
/// Expected: ACC=5, ACC=6, ACC=9, ACC=2/CY=1, ACC=9/CY=0, ACC=0/CY=0, CY=1, ACC=1/CY=0.
inline constexpr std::array<std::uint8_t, 8> PROGRAM_ACC_OPS = {
    encodeLDM(5), // ACC = 5 (0101)
    IAC,          // ACC = 6 (0110), carry = 0
    CMA,          // ACC = 9 (1001)
    RAL,          // ACC = 2 (0010), carry = 1 (MSB was 1)
    RAR,          // ACC = 9 (1001), carry = 0 (LSB was 0)
    CLB,          // ACC = 0, carry = 0
    STC,          // carry = 1
    TCC           // ACC = 1, carry = 0
};

/// Conditional jump: test JCN with accumulator-zero condition.
/// Addr 0: LDM 0            (ACC = 0)
/// Addr 1: JCN 4, 0x06      (jump if ACC == 0 -> jumps to addr 6)
/// Addr 3: LDM 1            (skipped)
/// Addr 4: JUN 0x000, 0x00  (skipped -- infinite loop)
/// Addr 6: LDM 2            (ACC = 2, reached via jump)
inline constexpr std::array<std::uint8_t, 7> PROGRAM_JCN = {
    encodeLDM(0), // ACC = 0
    encodeJCN(COND_ACC_ZERO),
    0x06,         // Jump to 0x06 if ACC == 0
    encodeLDM(1), // Should be skipped
    encodeJUN(0x000),
    0x00,        // Infinite loop (should not reach)
    encodeLDM(2) // ACC = 2 (jumped here)
};

/// Exchange register and accumulator.
/// FIM P0, 0x70 -> R0=7, R1=0; LDM 3; XCH R0 -> ACC=7, R0=3.
inline constexpr std::array<std::uint8_t, 5> PROGRAM_XCH = {
    encodeFIM(0), 0x70, // R0 = 7, R1 = 0
    encodeLDM(3),       // ACC = 3
    encodeXCH(0),       // Swap ACC and R0: ACC = 7, R0 = 3
    encodeLDM(0)        // Sentinel
};

/// Increment register.
/// FIM P0, 0xE0 -> R0=14, R1=0; INC R0; INC R0; INC R0.
/// R0: 14 -> 15 -> 0 (wrap) -> 1.
inline constexpr std::array<std::uint8_t, 5> PROGRAM_INC = {
    encodeFIM(0), 0xE0, // R0 = 14, R1 = 0
    encodeINC(0),       // R0 = 15
    encodeINC(0),       // R0 = 0 (4-bit wrap)
    encodeINC(0)        // R0 = 1
};

/// RAM write/read test.
/// FIM P0, 0x00; SRC P0; LDM 5; WRM; LDM 0; RDM -> ACC = 5.
inline constexpr std::array<std::uint8_t, 8> PROGRAM_RAM = {
    encodeFIM(0), 0x00, // P0 = 0x00 (RAM address)
    encodeSRC(0),       // Send address to RAM
    encodeLDM(5),       // ACC = 5
    WRM,                // Write ACC to RAM[0x00]
    encodeLDM(0),       // ACC = 0 (clear)
    RDM,                // Read RAM[0x00] -> ACC = 5
    encodeLDM(0)        // Sentinel
};

} // namespace sim::electronics::chips::intel4004

#endif // APEX_INTEL4004PROGRAMS_HPP
