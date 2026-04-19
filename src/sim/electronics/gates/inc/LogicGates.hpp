#ifndef APEX_SIM_ELECTRONICS_DIGITAL_LOGIC_GATES_HPP
#define APEX_SIM_ELECTRONICS_DIGITAL_LOGIC_GATES_HPP
/**
 * @file LogicGates.hpp
 * @brief User-facing logic gate truth tables.
 *
 * Wraps the existing CMOS composite gates (CmosInverter, CmosNand, CmosNor)
 * with a simple logic-level API for digital design verification.
 *
 * For circuit-level simulation of these gates with actual transistor physics,
 * use the underlying composite gates from devices/composite/.
 *
 * For pure logic verification (truth tables), this header is sufficient.
 *
 * @note RT-safe (constexpr functions, no allocations).
 */

#include "src/sim/electronics/devices/composite/inc/CmosInverter.hpp"
#include "src/sim/electronics/devices/composite/inc/CmosNand.hpp"
#include "src/sim/electronics/devices/composite/inc/CmosNor.hpp"

namespace sim::electronics::gates {

using devices::composite::CmosInverter;
using devices::composite::CmosNand;
using devices::composite::CmosNor;

/* ----------------------------- Truth Tables ----------------------------- */

/// NOT gate truth table.
[[nodiscard]] constexpr int gateNot(int a) noexcept { return CmosInverter::truthTable(a); }

/// NAND gate truth table.
[[nodiscard]] constexpr int gateNand(int a, int b) noexcept { return CmosNand::truthTable(a, b); }

/// NOR gate truth table.
[[nodiscard]] constexpr int gateNor(int a, int b) noexcept { return CmosNor::truthTable(a, b); }

/// AND gate (NAND followed by NOT).
[[nodiscard]] constexpr int gateAnd(int a, int b) noexcept { return gateNot(gateNand(a, b)); }

/// OR gate (NOR followed by NOT).
[[nodiscard]] constexpr int gateOr(int a, int b) noexcept { return gateNot(gateNor(a, b)); }

/// XOR gate (built from NAND gates).
/// XOR(a,b) = (a NAND (a NAND b)) NAND (b NAND (a NAND b))
[[nodiscard]] constexpr int gateXor(int a, int b) noexcept {
  int nab = gateNand(a, b);
  return gateNand(gateNand(a, nab), gateNand(b, nab));
}

/// XNOR gate (XOR followed by NOT).
[[nodiscard]] constexpr int gateXnor(int a, int b) noexcept { return gateNot(gateXor(a, b)); }

/* ----------------------------- Half/Full Adders ----------------------------- */

/// Half adder result (sum, carry).
struct HalfAdderResult {
  int sum;
  int carry;
};

/// Half adder: sum = a XOR b, carry = a AND b
[[nodiscard]] constexpr HalfAdderResult halfAdder(int a, int b) noexcept {
  return {gateXor(a, b), gateAnd(a, b)};
}

/// Full adder result (sum, carry-out).
struct FullAdderResult {
  int sum;
  int cout;
};

/// Full adder: 3-input add (a, b, carry-in).
[[nodiscard]] constexpr FullAdderResult fullAdder(int a, int b, int cin) noexcept {
  HalfAdderResult ha1 = halfAdder(a, b);
  HalfAdderResult ha2 = halfAdder(ha1.sum, cin);
  return {ha2.sum, gateOr(ha1.carry, ha2.carry)};
}

} // namespace sim::electronics::gates

#endif // APEX_SIM_ELECTRONICS_DIGITAL_LOGIC_GATES_HPP
