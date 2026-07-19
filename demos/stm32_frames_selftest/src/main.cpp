/**
 * @file main.cpp
 * @brief On-target golden self-test for the frames library (NUCLEO-L476RG).
 *
 * Runs the frames math ON THE MCU and compares it against values the hosted
 * test suite proves on every PR -- the cross-target analogue of the
 * cross-team golden-vector pattern: ground truth that is not the platform
 * under test. Checks cover the Transform point/vector split, graph resolve
 * against hand composition, the CG-relative resolution chain with a live CG
 * shift, Euler round-trip, and rotation-period closure -- in float (the FPU
 * path control code will use) and double (soft-float) both.
 *
 * Verdict: USART2 (ST-Link VCP, 115200 8N1) prints one line per check and a
 * final PASS/FAIL summary; the LED is solid on PASS, fast-blinking on FAIL.
 */

#include "src/utilities/math/frames/inc/Catalog.hpp"
#include "src/utilities/math/frames/inc/FrameGraph.hpp"
#include "src/utilities/math/frames/inc/Mount.hpp"
#include "src/utilities/math/frames/inc/Transform.hpp"

#include "Stm32Uart.hpp"
#include "stm32l4xx_hal.h"

#include <stdint.h>
#include <string.h>

namespace fr = apex::math::frames;

/* ----------------------------- Hardware Definitions ----------------------------- */

static constexpr uint16_t LED_PIN = GPIO_PIN_5;
static GPIO_TypeDef* const LED_PORT = GPIOA;

/// USART2 pins: PA2 (TX), PA3 (RX), AF7 -- connected to ST-Link VCP.
static const apex::hal::stm32::Stm32UartPins USART2_PINS = {GPIOA, GPIO_PIN_2, // TX
                                                            GPIOA, GPIO_PIN_3, // RX
                                                            GPIO_AF7_USART2};

static apex::hal::stm32::Stm32Uart<512, 64> vcp(USART2, USART2_PINS);

/* ----------------------------- Reporting ----------------------------- */

static void print(const char* s) noexcept {
  // Blocks until queued: the checks produce bytes far faster than the wire
  // drains them, and a dropped verdict is worse than a slow one here.
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
  size_t n = strlen(s);
  while (n > 0) {
    const size_t W = vcp.write(p, n);
    p += W;
    n -= W;
  }
}

static uint32_t g_run = 0;
static uint32_t g_pass = 0;

template <typename T> static void check(const char* name, T got, T want, T tol) noexcept {
  ++g_run;
  const T D = got > want ? got - want : want - got;
  if (D <= tol) {
    ++g_pass;
    print("PASS ");
  } else {
    print("FAIL ");
  }
  print(name);
  print("\r\n");
}

/* ----------------------------- The checks ----------------------------- */

/// State-driven CG edge over a live mass context (mirrors the hosted test).
template <typename T> struct CgCtx {
  T cg0;
};
template <typename T> static uint8_t cgEdge(void* ctx, T /*t*/, fr::Transform<T>* out) noexcept {
  *out = fr::Transform<T>{};
  out->t[0] = static_cast<const CgCtx<T>*>(ctx)->cg0;
  return 0;
}

template <typename T> static void runSuite(const char* label, T tol) noexcept {
  print("--- ");
  print(label);
  print(" ---\r\n");

  // 1. Point/vector split: identity rotation, offset transform.
  fr::Transform<T> x;
  x.t[0] = T(10);
  const T IN[3] = {T(1), T(2), T(3)};
  T p[3], v[3];
  (void)fr::transformPointInto(x, IN, p);
  (void)fr::rotateVectorInto(x, IN, v);
  check<T>("point_gets_lever_arm", p[0], T(11), tol);
  check<T>("vector_skips_lever_arm", v[0], T(1), tol);

  // 2. Hand-derived quarter turn with offset (hosted golden: (5, 1, 0)).
  fr::Transform<T> q;
  q.rotation().setFromAngleAxis(T(1.5707963267948966), T(0), T(0), T(1));
  q.t[0] = T(5);
  const T PX[3] = {T(1), T(0), T(0)};
  T out[3];
  (void)fr::transformPointInto(q, PX, out);
  check<T>("quarter_turn_x", out[0], T(5), tol * T(4));
  check<T>("quarter_turn_y", out[1], T(1), tol * T(4));

  // 3. Graph resolve == hand composition (3-hop chain).
  // Static: two graphs per suite would overflow the 2 KB stack.
  static fr::FrameGraph<T, 8> g;
  fr::FrameId root = 0, body = 0, pay = 0, sensor = 0;
  (void)g.addRoot("root", root);
  fr::Transform<T> eb, ep, es;
  eb.rotation().setFromEuler321(T(0.2), T(-0.1), T(0.8));
  eb.t[0] = T(100);
  ep.rotation().setFromAngleAxis(T(0.5), T(0), T(1), T(0));
  ep.t[2] = T(-0.4);
  es.t[1] = T(1.5);
  (void)g.addStatic(root, eb, "body", body);
  (void)g.addStatic(body, ep, "payload", pay);
  (void)g.addStatic(pay, es, "sensor", sensor);
  fr::Transform<T> viaGraph, tmp, hand;
  (void)g.resolve(sensor, root, T(0), viaGraph);
  fr::composeInto(eb, ep, tmp);
  fr::composeInto(tmp, es, hand);
  const T PT[3] = {T(0.3), T(-2), T(1)};
  T pg[3], ph[3];
  (void)fr::transformPointInto(viaGraph, PT, pg);
  (void)fr::transformPointInto(hand, PT, ph);
  check<T>("resolve_matches_hand_x", pg[0], ph[0], tol * T(100));
  check<T>("resolve_matches_hand_z", pg[2], ph[2], tol * T(100));

  // 4. The CG-relative acceptance chain with a live CG shift.
  static fr::FrameGraph<T, 8> gb;
  fr::FrameId broot = 0, cg = 0, rf = 0;
  (void)gb.addRoot("body", broot);
  static CgCtx<T> mass; // provider ctx outlives the graph
  mass.cg0 = T(0.20);
  (void)gb.addStateDriven(broot, {&cgEdge<T>, &mass}, "cg", cg);
  fr::Mount<T> m;
  m.leverArmM[0] = T(3.0);
  (void)fr::addMount(gb, broot, m, "rf", rf);
  const T MEAS[3] = {T(25), T(0), T(0)};
  T r0[3], r1[3];
  (void)gb.in(cg).from(rf).point(MEAS, r0, T(0));
  check<T>("cg_chain_before_burn", r0[0], T(27.8), tol * T(10));
  mass.cg0 = T(-0.10);
  (void)gb.in(cg).from(rf).point(MEAS, r1, T(0));
  check<T>("cg_chain_after_burn", r1[0], T(28.1), tol * T(10));

  // 5. Euler-321 round-trip through the transform rotation.
  fr::Transform<T> e;
  e.rotation().setFromEuler321(T(0.3), T(-0.4), T(1.2));
  T roll = 0, pitch = 0, yaw = 0;
  (void)e.rotation().toEuler321Into(roll, pitch, yaw);
  check<T>("euler_roll", roll, T(0.3), tol * T(10));
  check<T>("euler_yaw", yaw, T(1.2), tol * T(10));
}

/// Double-only: catalog rotation closure over the model's own period.
static void runCatalogClosure() noexcept {
  print("--- catalog (double) ---\r\n");
  static fr::Epoch epoch; // provider ctx outlives the graph
  epoch.init(apex::math::celestial::JD_J2000);
  static fr::FrameGraph<double, 8> g;
  fr::CatalogIds ids;
  (void)fr::buildCatalog(g, epoch, ids);
  const double P[3] = {apex::math::celestial::earth::A, 0.0, 0.0};
  const double PERIOD = apex::math::vecmat::TWO_PI / apex::math::celestial::earth::OMEGA;
  double p0[3], p1[3];
  (void)g.in(ids.eci).from(ids.ecef).point(P, p0, 0.0);
  (void)g.in(ids.eci).from(ids.ecef).point(P, p1, PERIOD);
  check<double>("rotation_closure_m", p1[0], p0[0], 1e-3);
}

/* ----------------------------- Interrupt Handlers ----------------------------- */

extern "C" void USART2_IRQHandler() { vcp.irqHandler(); }

/// HAL_Init arms the 1 ms SysTick; without a handler the default trap wedges
/// the core at exception priority and silences every lower-priority ISR.
extern "C" void SysTick_Handler() { HAL_IncTick(); }

/* ----------------------------- System Initialization ----------------------------- */

/** @brief Configure system clock to 80 MHz using MSI + PLL. */
static void SystemClock_Config() {
  RCC_OscInitTypeDef oscInit = {};
  RCC_ClkInitTypeDef clkInit = {};

  oscInit.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  oscInit.MSIState = RCC_MSI_ON;
  oscInit.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  oscInit.MSIClockRange = RCC_MSIRANGE_6; // 4 MHz
  oscInit.PLL.PLLState = RCC_PLL_ON;
  oscInit.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  oscInit.PLL.PLLM = 1;
  oscInit.PLL.PLLN = 40;
  oscInit.PLL.PLLR = 2;
  oscInit.PLL.PLLP = 7;
  oscInit.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&oscInit) != HAL_OK) {
    while (1) {
    }
  }

  clkInit.ClockType =
      RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clkInit.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clkInit.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clkInit.APB1CLKDivider = RCC_HCLK_DIV1;
  clkInit.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clkInit, FLASH_LATENCY_4) != HAL_OK) {
    while (1) {
    }
  }
}

/** @brief Initialize GPIO for the LED (PA5). */
static void GPIO_Init() {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef gpioInit = {};
  gpioInit.Pin = LED_PIN;
  gpioInit.Mode = GPIO_MODE_OUTPUT_PP;
  gpioInit.Pull = GPIO_NOPULL;
  gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_PORT, &gpioInit);
}

/* ----------------------------- Main ----------------------------- */

int main() {
  HAL_Init();
  SystemClock_Config();
  GPIO_Init();

  apex::hal::UartConfig uartCfg;
  uartCfg.baudRate = 115200;
  (void)vcp.init(uartCfg);

  print("\r\nFRAMES SELFTEST (NUCLEO-L476RG)\r\n");
  runSuite<float>("float (FPU)", 1e-4f);
  runSuite<double>("double (soft)", 1e-9);
  runCatalogClosure();

  const bool ALL_PASS = g_run > 0 && g_pass == g_run;
  print(ALL_PASS ? "RESULT: PASS " : "RESULT: FAIL ");
  // Counts as two-digit decimals (avoids printf in the firmware).
  char buf[16] = {};
  buf[0] = static_cast<char>('0' + (g_pass / 10) % 10);
  buf[1] = static_cast<char>('0' + g_pass % 10);
  buf[2] = '/';
  buf[3] = static_cast<char>('0' + (g_run / 10) % 10);
  buf[4] = static_cast<char>('0' + g_run % 10);
  buf[5] = '\r';
  buf[6] = '\n';
  print(buf);

  // LED verdict: solid on pass, fast blink on fail.
  for (;;) {
    if (ALL_PASS) {
      HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
    } else {
      HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
      HAL_Delay(100);
    }
  }
}
