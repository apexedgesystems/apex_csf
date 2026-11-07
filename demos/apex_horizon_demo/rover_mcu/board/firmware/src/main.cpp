/**
 * @file main.cpp
 * @brief rover_mcu firmware: the rover's controller on the NUCLEO-F767ZI.
 *
 * Every STATE_UPDATE from the host (the rover's pose and what it has
 * been commanded) is answered with one CONTROL_CMD (steering angle,
 * throttle, arrival) computed by RoverBoardController, the same law the
 * host's closed-loop tests pin. The three user LEDs show lamp 1; a
 * HEARTBEAT once a second carries the executive's cycle count, the
 * controller's step count and the tick load measured with the DWT
 * cycle counter. The lidar sensor sends a LIDAR_SCAN per sweep on its
 * own UART; every CONTROL_CMD carries what the board last saw (sensor
 * state, scan number, hit bits, closest return). McuExecutive on
 * SysTick at 100 Hz drives it all.
 */

#include "McuExecutive.hpp"
#include "RoverBoard.hpp"
#include "Stm32SysTickSource.hpp"
#include "Stm32Uart.hpp"
#include "demos/apex_horizon_demo/rover_mcu/board/inc/RoverBoardController.hpp"
#include "demos/apex_horizon_demo/rover_mcu/board/inc/RoverBoardLidar.hpp"
#include "demos/apex_horizon_demo/rover_mcu/board/inc/RoverBoardProtocol.hpp"
#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"
#include "stm32f7xx_hal.h"

#include <string.h>

using namespace appsim;
namespace board = appsim::rover_board::board;

/* ----------------------------- Rates ----------------------------- */

static constexpr uint16_t EXEC_FREQ_HZ = 100;
static constexpr uint16_t LINK_HZ = 20; ///< The host's STATE_UPDATE rate (strobe timing).

/* ----------------------------- Peripherals ----------------------------- */

static apex::hal::stm32::Stm32Uart<512, 512> linkUart(board::LINK_UART, board::LINK_UART_PINS);
static apex::hal::stm32::Stm32Uart<256, 64> sensorUart(board::SENSOR_UART, board::SENSOR_UART_PINS);

/* ----------------------------- Controller ----------------------------- */

static rover_board::RoverBoardController controller;
static rover_board::BoardCommand lastCommand;
static rover_board::RoverBoardLidar lidar;

/* ----------------------------- Executive ----------------------------- */

static apex::hal::stm32::Stm32SysTickSource tickSource(EXEC_FREQ_HZ);
static executive::mcu::McuExecutive<> exec(&tickSource, EXEC_FREQ_HZ);

/* ----------------------------- DWT profiling ----------------------------- */

static volatile uint32_t tickStartCycles = 0;
static volatile uint32_t lastOverheadCycles = 0;

/* ----------------------------- Link buffers ----------------------------- */

static apex::protocols::slip::DecodeState slipDecodeState;
static apex::protocols::slip::DecodeConfig slipDecodeCfg;
static uint8_t rxChunk[64];
static uint8_t decodeBuf[rover_board::MAX_FRAME_PAYLOAD];
static uint8_t txFrameBuf[rover_board::MAX_FRAME_PAYLOAD];
static uint8_t txSlipBuf[rover_board::MAX_SLIP_ENCODED];
static uint32_t rxCount = 0;
static uint32_t txCount = 0;
static uint32_t crcErrors = 0;
static uint16_t txSeq = 0;

/* ----------------------------- Sensor buffers ----------------------------- */

static apex::protocols::slip::DecodeState sensorDecodeState;
static uint8_t sensorChunk[64];
static uint8_t sensorDecodeBuf[rover_board::MAX_FRAME_PAYLOAD];

/* ----------------------------- Frames ----------------------------- */

static void sendFrame(rover_board::Opcode opcode, const void* payload, size_t len) noexcept {
  const size_t N = rover_board::buildFrame(opcode, payload, len, txFrameBuf, sizeof(txFrameBuf));
  if (N == 0) {
    return;
  }
  const apex::compat::bytes_span MSG(txFrameBuf, N);
  auto result = apex::protocols::slip::encode(MSG, txSlipBuf, sizeof(txSlipBuf));
  if (result.status == apex::protocols::slip::Status::OK) {
    linkUart.write(txSlipBuf, result.bytesProduced);
    ++txCount;
  }
}

/// A de-SLIPped frame from the host: a STATE_UPDATE is answered at once.
static void processFrame(const uint8_t* data, size_t len) noexcept {
  const rover_board::ParsedFrame F = rover_board::parseFrame(data, len);
  if (!F.ok) {
    ++crcErrors;
    return;
  }
  if (F.opcode == rover_board::Opcode::STATE_UPDATE &&
      F.payload_len == sizeof(rover_board::BoardState)) {
    rover_board::BoardState state;
    memcpy(&state, F.payload, sizeof(state));
    ++rxCount;
    controller.updateState(state);
    lastCommand = controller.step();
    lidar.fill(lastCommand, HAL_GetTick());
    lastCommand.seq_num = ++txSeq;
    sendFrame(rover_board::Opcode::CONTROL_CMD, &lastCommand, sizeof(lastCommand));
  }
}

/* ----------------------------- Tasks ----------------------------- */

/// 100 Hz: drain the link and answer every state frame in it.
static void linkTask(void* /*ctx*/) noexcept {
  const size_t AVAIL = linkUart.read(rxChunk, sizeof(rxChunk));
  size_t pos = 0;
  while (pos < AVAIL) {
    const size_t PREV_LEN = slipDecodeState.frameLen;
    const apex::compat::bytes_span INPUT(rxChunk + pos, AVAIL - pos);
    auto result = apex::protocols::slip::decodeChunk(slipDecodeState, slipDecodeCfg, INPUT,
                                                     decodeBuf + PREV_LEN,
                                                     rover_board::MAX_FRAME_PAYLOAD - PREV_LEN);
    pos += result.bytesConsumed;
    if (result.frameCompleted) {
      processFrame(decodeBuf, PREV_LEN + result.bytesProduced);
    }
    if (result.status == apex::protocols::slip::Status::OUTPUT_FULL) {
      slipDecodeState.reset();
      continue;
    }
    if (result.bytesConsumed == 0) {
      break;
    }
  }
}

/// A de-SLIPped frame from the sensor: a LIDAR_SCAN updates the board's picture.
static void processSensorFrame(const uint8_t* data, size_t len) noexcept {
  const rover_board::ParsedFrame F = rover_board::parseFrame(data, len);
  if (!F.ok || F.opcode != rover_board::Opcode::LIDAR_SCAN ||
      F.payload_len != sizeof(rover_board::LidarScan)) {
    lidar.onBadFrame();
    return;
  }
  rover_board::LidarScan scan;
  memcpy(&scan, F.payload, sizeof(scan));
  lidar.onScan(scan, HAL_GetTick());
}

/// 100 Hz: drain the sensor's UART.
static void sensorTask(void* /*ctx*/) noexcept {
  const size_t AVAIL = sensorUart.read(sensorChunk, sizeof(sensorChunk));
  size_t pos = 0;
  while (pos < AVAIL) {
    const size_t PREV_LEN = sensorDecodeState.frameLen;
    const apex::compat::bytes_span INPUT(sensorChunk + pos, AVAIL - pos);
    auto result = apex::protocols::slip::decodeChunk(sensorDecodeState, slipDecodeCfg, INPUT,
                                                     sensorDecodeBuf + PREV_LEN,
                                                     rover_board::MAX_FRAME_PAYLOAD - PREV_LEN);
    pos += result.bytesConsumed;
    if (result.frameCompleted) {
      processSensorFrame(sensorDecodeBuf, PREV_LEN + result.bytesProduced);
    }
    if (result.status == apex::protocols::slip::Status::OUTPUT_FULL) {
      sensorDecodeState.reset();
      continue;
    }
    if (result.bytesConsumed == 0) {
      break;
    }
  }
}

/// 100 Hz: the user LEDs follow lamp 1 (colour from the last state, on/off from the strobe).
static void ledTask(void* /*ctx*/) noexcept {
  const uint16_t PINS = ((lastCommand.led_bits & 0x01u) != 0u)
                            ? board::pinsForColour(controller.lastLamp1Colour())
                            : 0u;
  HAL_GPIO_WritePin(board::LED_PORT, PINS, GPIO_PIN_SET);
  HAL_GPIO_WritePin(board::LED_PORT, static_cast<uint16_t>(board::LED_ALL_PINS & ~PINS),
                    GPIO_PIN_RESET);
}

/// 1 Hz: cycle count, step count, load.
static void heartbeatTask(void* /*ctx*/) noexcept {
  rover_board::BoardHeartbeat hb{};
  hb.cycle_count = static_cast<uint32_t>(exec.cycleCount());
  hb.step_count = controller.stepCount();
  hb.overhead_us = lastOverheadCycles / board::CYCLES_PER_US;
  const uint32_t TICK_US = 1000000u / EXEC_FREQ_HZ;
  const uint32_t LOAD = (hb.overhead_us * 100u) / TICK_US;
  hb.load_pct = static_cast<uint8_t>(LOAD > 100u ? 100u : LOAD);
  sendFrame(rover_board::Opcode::HEARTBEAT, &hb, sizeof(hb));
}

static void profilerStartTask(void* /*ctx*/) noexcept { tickStartCycles = DWT->CYCCNT; }
static void profilerEndTask(void* /*ctx*/) noexcept {
  lastOverheadCycles = DWT->CYCCNT - tickStartCycles;
}

/* ----------------------------- Bring-up ----------------------------- */

static void gpioInit() noexcept {
  board::enableLedClock();
  GPIO_InitTypeDef gpioInit = {};
  gpioInit.Pin = board::LED_ALL_PINS;
  gpioInit.Mode = GPIO_MODE_OUTPUT_PP;
  gpioInit.Pull = GPIO_NOPULL;
  gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(board::LED_PORT, &gpioInit);
}

static void enableDwt() noexcept {
  CoreDebug->DEMCR = CoreDebug->DEMCR | CoreDebug_DEMCR_TRCENA_Msk;
  // The Cortex-M7 locks the DWT after a power-on reset; unlock it, or the
  // cycle counter never runs unless a debugger has unlocked it first.
  DWT->LAR = 0xC5ACCE55U;
  DWT->CYCCNT = 0;
  DWT->CTRL = DWT->CTRL | DWT_CTRL_CYCCNTENA_Msk;
}

int main() {
  HAL_Init();
  board::configureCaches();
  board::configureSystemClock();
  gpioInit();
  enableDwt();

  // Boot: red, green, blue in turn, then dark.
  const uint16_t BOOT[] = {board::LED_RED_PIN, board::LED_GREEN_PIN, board::LED_BLUE_PIN};
  for (uint16_t pin : BOOT) {
    HAL_GPIO_WritePin(board::LED_PORT, pin, GPIO_PIN_SET);
    HAL_Delay(150);
    HAL_GPIO_WritePin(board::LED_PORT, pin, GPIO_PIN_RESET);
  }

  apex::hal::UartConfig uartCfg;
  uartCfg.baudRate = rover_board::BAUD_RATE;
  static_cast<void>(linkUart.init(uartCfg));
  HAL_NVIC_SetPriority(board::LINK_UART_IRQN, 6, 0);
  HAL_NVIC_EnableIRQ(board::LINK_UART_IRQN);
  static_cast<void>(sensorUart.init(uartCfg));
  HAL_NVIC_SetPriority(board::SENSOR_UART_IRQN, 7, 0);
  HAL_NVIC_EnableIRQ(board::SENSOR_UART_IRQN);

  slipDecodeCfg.maxFrameSize = rover_board::MAX_FRAME_PAYLOAD;
  slipDecodeCfg.allowEmptyFrame = false;
  slipDecodeCfg.dropUntilEnd = true;
  slipDecodeCfg.requireTrailingEnd = true;

  rover_board::BoardTunables tunables{};
  tunables.step_hz = LINK_HZ;
  controller.setTunables(tunables);

  exec.addTask({{profilerStartTask, nullptr}, 1, 1, 0, 127, 10});
  exec.addTask({{sensorTask, nullptr}, 1, 1, 0, 5, 4});
  exec.addTask({{linkTask, nullptr}, 1, 1, 0, 0, 1});
  exec.addTask({{ledTask, nullptr}, 1, 1, 0, -10, 2});
  exec.addTask({{heartbeatTask, nullptr}, 1, EXEC_FREQ_HZ, 0, -20, 3});
  exec.addTask({{profilerEndTask, nullptr}, 1, 1, 0, -128, 11});

  static_cast<void>(exec.init());
  static_cast<void>(exec.run());
  for (;;) {
  }
  return 0;
}

/* ----------------------------- Interrupts ----------------------------- */

extern "C" void SysTick_Handler() {
  HAL_IncTick();
  apex::hal::stm32::Stm32SysTickSource::isrCallback();
}

extern "C" void USART3_IRQHandler() { linkUart.irqHandler(); }

extern "C" void USART6_IRQHandler() { sensorUart.irqHandler(); }

extern "C" void HAL_MspInit() {
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
}
