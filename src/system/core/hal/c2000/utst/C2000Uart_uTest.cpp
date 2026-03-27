/**
 * @file C2000Uart_uTest.cpp
 * @brief Unit tests for C2000Uart (mock mode, host-side).
 *
 * Notes:
 *  - Tests run with APEX_HAL_C2000_MOCK defined (no driverlib).
 *  - Verifies construction, init, print, and hex output.
 */

#define APEX_HAL_C2000_MOCK

#include "src/system/core/hal/c2000/inc/C2000Uart.hpp"

#include <gtest/gtest.h>

using apex::hal::c2000::C2000Uart;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default mock constructor initializes to not-initialized */
TEST(C2000Uart, DefaultConstruction) {
  C2000Uart uart;
  EXPECT_FALSE(uart.isInitialized());
}

/* ----------------------------- API Tests ----------------------------- */

/** @test Init sets initialized flag */
TEST(C2000Uart, InitSetsFlag) {
  C2000Uart uart;
  uart.init(115200);
  EXPECT_TRUE(uart.isInitialized());
}

/** @test getch returns 0 in mock mode */
TEST(C2000Uart, GetchReturnsMockValue) {
  C2000Uart uart;
  uart.init(115200);
  EXPECT_EQ(uart.getch(), 0U);
}

/** @test rxReady returns false in mock mode */
TEST(C2000Uart, RxReadyReturnsFalse) {
  C2000Uart uart;
  uart.init(115200);
  EXPECT_FALSE(uart.rxReady());
}

/** @test putch does not crash in mock mode */
TEST(C2000Uart, PutchNoOp) {
  C2000Uart uart;
  uart.init(115200);
  uart.putch(0x41);
}

/** @test print does not crash in mock mode */
TEST(C2000Uart, PrintNoOp) {
  C2000Uart uart;
  uart.init(115200);
  uart.print("Hello");
}

/** @test sendArr does not crash in mock mode */
TEST(C2000Uart, SendArrNoOp) {
  C2000Uart uart;
  uart.init(115200);
  const uint16_t DATA[] = {0x01, 0x02, 0x03};
  uart.sendArr(DATA, 3);
}

/** @test putHex8 does not crash in mock mode */
TEST(C2000Uart, PutHex8NoOp) {
  C2000Uart uart;
  uart.init(115200);
  uart.putHex8(0xAB);
}

/** @test putDec handles zero */
TEST(C2000Uart, PutDecZero) {
  C2000Uart uart;
  uart.init(115200);
  uart.putDec(0);
}

/** @test putDec handles multi-digit values */
TEST(C2000Uart, PutDecMultiDigit) {
  C2000Uart uart;
  uart.init(115200);
  uart.putDec(12345);
}
