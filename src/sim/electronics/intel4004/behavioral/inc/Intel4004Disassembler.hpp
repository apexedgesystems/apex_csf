#ifndef APEX_SIM_ELECTRONICS_CPU_INTEL4004_DISASSEMBLER_HPP
#define APEX_SIM_ELECTRONICS_CPU_INTEL4004_DISASSEMBLER_HPP
/**
 * @file Intel4004Disassembler.hpp
 * @brief Inverse of Intel4004Instructions.hpp: byte stream -> mnemonic.
 *
 * Disassembles a single 4004 instruction starting at the given byte
 * pointer. Handles both single-byte and two-byte instructions; the
 * returned length tells the caller how far to advance.
 *
 * The output mnemonic format matches Intel's 4004 datasheet table 8-18:
 *
 *   FIM P0, 0x35      (2-byte: fetch immediate)
 *   JCN C2, 0x10      (2-byte: jump on condition)
 *   JUN 0x123         (2-byte: jump unconditional, 12-bit)
 *   JMS 0x123         (2-byte: jump to subroutine, 12-bit)
 *   ISZ R3, 0x10      (2-byte: increment and skip if zero)
 *   LDM 5             (1-byte: load immediate)
 *   ADD R3            (1-byte: register op)
 *   IAC               (1-byte: accumulator op)
 *
 * Returns `{"<unknown 0xNN>", 1}` for unrecognized bytes.
 */

#include "src/sim/electronics/intel4004/behavioral/inc/Intel4004Instructions.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace sim::electronics::intel4004 {

/**
 * @brief A single disassembled instruction: mnemonic text + byte length.
 */
struct DisassembledInstruction {
  std::string mnemonic; ///< Human-readable mnemonic (e.g. "FIM P0, 0x35").
  std::size_t length;   ///< Number of bytes consumed (1 or 2).
};

/* ----------------------------- File Helpers ----------------------------- */

/// Mnemonic for the F-group accumulator op (byte 0xF0..0xFF).
inline std::string_view accGroupMnemonic(std::uint8_t opa) noexcept {
  switch (opa) {
  case 0x0: return "CLB";
  case 0x1: return "CLC";
  case 0x2: return "IAC";
  case 0x3: return "CMC";
  case 0x4: return "CMA";
  case 0x5: return "RAL";
  case 0x6: return "RAR";
  case 0x7: return "TCC";
  case 0x8: return "DAC";
  case 0x9: return "TCS";
  case 0xA: return "STC";
  case 0xB: return "DAA";
  case 0xC: return "KBP";
  case 0xD: return "DCL";
  default:  return "F?";
  }
}

/// Mnemonic for the E-group I/O and RAM ops (byte 0xE0..0xEF).
inline std::string_view ioRamMnemonic(std::uint8_t opa) noexcept {
  switch (opa) {
  case 0x0: return "WRM";
  case 0x1: return "WMP";
  case 0x2: return "WRR";
  case 0x3: return "WPM";
  case 0x4: return "WR0";
  case 0x5: return "WR1";
  case 0x6: return "WR2";
  case 0x7: return "WR3";
  case 0x8: return "SBM";
  case 0x9: return "RDM";
  case 0xA: return "RDR";
  case 0xB: return "ADM";
  case 0xC: return "RD0";
  case 0xD: return "RD1";
  case 0xE: return "RD2";
  case 0xF: return "RD3";
  default:  return "E?";
  }
}

/* ----------------------------- API ----------------------------- */

/**
 * @brief Disassemble one instruction starting at `bytes[0]`.
 *
 * Two-byte instructions (JCN, FIM, JUN, JMS, ISZ) consume two bytes if
 * `remaining >= 2`. If only one byte is available the function still
 * returns a meaningful single-byte mnemonic (with the second byte
 * reported as `??`) and length 1, so the caller can advance.
 *
 * @param bytes     Pointer to the next instruction byte.
 * @param remaining Number of bytes available from `bytes`.
 * @return DisassembledInstruction with mnemonic + length consumed.
 *
 * @note RT-safe: pure arithmetic + string formatting.
 */
inline DisassembledInstruction disassemble(const std::uint8_t* bytes,
                                           std::size_t remaining) {
  if (remaining == 0) return {"<empty>", 0};
  const std::uint8_t BYTE = bytes[0];
  const std::uint8_t GROUP = (BYTE >> 4) & 0xF;
  const std::uint8_t OPA = BYTE & 0xF;
  const bool HAVE_NEXT = remaining >= 2;
  const std::uint8_t NEXT = HAVE_NEXT ? bytes[1] : 0;

  switch (GROUP) {
  case 0x0:
    return {"NOP", 1};
  case 0x1: { // JCN cc, addr
    if (!HAVE_NEXT) return {fmt::format("JCN C{:X}, ??", OPA), 1};
    return {fmt::format("JCN C{:X}, 0x{:02X}", OPA, NEXT), 2};
  }
  case 0x2: { // FIM (even) or SRC (odd)
    if ((OPA & 1) == 0) {
      if (!HAVE_NEXT) return {fmt::format("FIM P{}, ??", OPA >> 1), 1};
      return {fmt::format("FIM P{}, 0x{:02X}", OPA >> 1, NEXT), 2};
    }
    return {fmt::format("SRC P{}", OPA >> 1), 1};
  }
  case 0x3: { // FIN (even) or JIN (odd)
    if ((OPA & 1) == 0) return {fmt::format("FIN P{}", OPA >> 1), 1};
    return {fmt::format("JIN P{}", OPA >> 1), 1};
  }
  case 0x4: { // JUN addr (12-bit)
    if (!HAVE_NEXT) return {fmt::format("JUN ??"), 1};
    const unsigned ADDR = (static_cast<unsigned>(OPA) << 8) | NEXT;
    return {fmt::format("JUN 0x{:03X}", ADDR), 2};
  }
  case 0x5: { // JMS addr (12-bit)
    if (!HAVE_NEXT) return {fmt::format("JMS ??"), 1};
    const unsigned ADDR = (static_cast<unsigned>(OPA) << 8) | NEXT;
    return {fmt::format("JMS 0x{:03X}", ADDR), 2};
  }
  case 0x6:
    return {fmt::format("INC R{}", OPA), 1};
  case 0x7: { // ISZ Rn, addr
    if (!HAVE_NEXT) return {fmt::format("ISZ R{}, ??", OPA), 1};
    return {fmt::format("ISZ R{}, 0x{:02X}", OPA, NEXT), 2};
  }
  case 0x8:
    return {fmt::format("ADD R{}", OPA), 1};
  case 0x9:
    return {fmt::format("SUB R{}", OPA), 1};
  case 0xA:
    return {fmt::format("LD R{}", OPA), 1};
  case 0xB:
    return {fmt::format("XCH R{}", OPA), 1};
  case 0xC:
    return {fmt::format("BBL {}", OPA), 1};
  case 0xD:
    return {fmt::format("LDM {}", OPA), 1};
  case 0xE:
    return {std::string(ioRamMnemonic(OPA)), 1};
  case 0xF:
    return {std::string(accGroupMnemonic(OPA)), 1};
  default:
    return {fmt::format("<unknown 0x{:02X}>", BYTE), 1};
  }
}

} // namespace sim::electronics::intel4004

#endif // APEX_SIM_ELECTRONICS_CPU_INTEL4004_DISASSEMBLER_HPP
