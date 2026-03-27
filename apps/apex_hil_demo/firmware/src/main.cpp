/**
 * @file main.cpp
 * @brief STM32 HIL flight controller firmware for NUCLEO-L476RG.
 *
 * Full SLIP/CRC communication over USART1 (ST-Link VCP).
 * Receives VehicleState from host plant, runs flight controller,
 * sends ControlCmd and HeartbeatData back.
 *
 *   - UART2 (VCP): SLIP-framed state/command exchange with host
 *   - LED heartbeat at 2 Hz
 *   - Flight controller at 50 Hz
 *   - DWT overhead profiling at 100 Hz
 *
 * Task model (100 Hz fundamental):
 *   - profilerStartTask: 100 Hz (priority 127, DWT cycle start marker)
 *   - controlTask:        50 Hz (freqN=1, freqD=2)
 *   - heartbeatTask:       1 Hz (freqN=1, freqD=100)
 *   - ledBlinkTask:        2 Hz (freqN=1, freqD=50)
 *   - profilerEndTask:   100 Hz (priority -128, DWT cycle end marker)
 */

#include "FirmwareConfig.hpp"
#include "FlightController.hpp"
#include "HilConfig.hpp"
#include "HilProtocol.hpp"
#include "LiteExecutive.hpp"
#include "Stm32SysTickSource.hpp"
#include "Stm32Uart.hpp"
#include "stm32l4xx_hal.h"

#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"
#include "src/utilities/checksums/crc/inc/Crc.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <string.h>

using namespace appsim;

/* ----------------------------- Peripheral Instances ----------------------------- */

static apex::hal::stm32::Stm32Uart<256, 256> vcpUart(USART1, firmware::HIL_UART_PINS);

/* ----------------------------- Controller ----------------------------- */

static hil::FlightController controller;

/* ----------------------------- Executive Stack ----------------------------- */

static apex::hal::stm32::Stm32SysTickSource tickSource(hil::EXEC_FREQ_HZ);
static executive::lite::LiteExecutive<> exec(&tickSource, hil::EXEC_FREQ_HZ);

/* ----------------------------- DWT Profiling ----------------------------- */

static volatile uint32_t tickStartCycles = 0;
static volatile uint32_t lastOverheadCycles = 0;

/* ----------------------------- SLIP Decode State ----------------------------- */

static apex::protocols::slip::DecodeState slipDecodeState;
static apex::protocols::slip::DecodeConfig slipDecodeCfg;

/* ----------------------------- Communication Buffers ----------------------------- */

/// UART read chunk (polled each controlTask invocation).
static uint8_t rxChunk[64];

/// Accumulated SLIP-decoded frame.
static uint8_t decodeBuf[hil::MAX_FRAME_PAYLOAD];

/// Pre-SLIP TX message buffer (opcode + payload + CRC).
static uint8_t txMsgBuf[hil::MAX_FRAME_PAYLOAD];

/// SLIP-encoded TX buffer.
static uint8_t txSlipBuf[hil::MAX_SLIP_ENCODED];

/* ----------------------------- Communication Stats ----------------------------- */

static uint32_t rxCount = 0;
static uint32_t txCount = 0;
static uint32_t crcErrors = 0;
static uint16_t txSeqNum = 0;
static uint16_t lastRxSeq = 0;

/* ----------------------------- Frame Processing ----------------------------- */

/**
 * @brief Send a SLIP-framed message over VCP UART.
 * @param opcode HIL protocol opcode.
 * @param payload Payload data (may be nullptr).
 * @param payloadLen Payload length in bytes.
 * @note RT-safe: Bounded buffer operations.
 */
static void sendMessage(uint8_t opcode, const void* payload, size_t payloadLen) noexcept {
  size_t pos = 0;
  txMsgBuf[pos++] = opcode;

  if (payload != nullptr && payloadLen > 0) {
    memcpy(txMsgBuf + pos, payload, payloadLen);
    pos += payloadLen;
  }

  // CRC-16/XMODEM over opcode + payload (big-endian)
  apex::checksums::crc::Crc16XmodemBitwise crc;
  uint16_t crcVal = 0;
  crc.calculate(txMsgBuf, pos, crcVal);
  txMsgBuf[pos++] = static_cast<uint8_t>(crcVal >> 8);
  txMsgBuf[pos++] = static_cast<uint8_t>(crcVal & 0xFF);

  // SLIP encode and transmit
  const apex::compat::bytes_span MSG(txMsgBuf, pos);
  auto result = apex::protocols::slip::encode(MSG, txSlipBuf, sizeof(txSlipBuf));

  if (result.status == apex::protocols::slip::Status::OK) {
    vcpUart.write(txSlipBuf, result.bytesProduced);
    ++txCount;
  }
}

/**
 * @brief Process a complete SLIP-decoded frame.
 *
 * Validates CRC-16/XMODEM and dispatches by opcode. STATE_UPDATE
 * frames feed the flight controller; command opcodes adjust mode.
 *
 * @param data Decoded frame bytes.
 * @param len Frame length (including opcode and CRC).
 * @param ctrl Flight controller instance.
 * @note RT-safe: Bounded execution, no allocation.
 */
static void processFrame(const uint8_t* data, size_t len, hil::FlightController* ctrl) noexcept {
  // Minimum: opcode(1) + CRC(2)
  if (len < 3) {
    return;
  }

  // Validate CRC-16/XMODEM
  const size_t BODY_LEN = len - 2;
  const uint16_t EXPECTED = (static_cast<uint16_t>(data[BODY_LEN]) << 8) | data[BODY_LEN + 1];

  apex::checksums::crc::Crc16XmodemBitwise crc;
  uint16_t computed = 0;
  crc.calculate(data, BODY_LEN, computed);

  if (computed != EXPECTED) {
    ++crcErrors;
    return;
  }

  // Dispatch by opcode
  const auto OPCODE = static_cast<hil::HilOpcode>(data[0]);
  const uint8_t* payload = data + 1;
  const size_t PAYLOAD_LEN = BODY_LEN - 1;

  switch (OPCODE) {
  case hil::HilOpcode::STATE_UPDATE:
    if (PAYLOAD_LEN == sizeof(hil::VehicleState)) {
      hil::VehicleState state;
      memcpy(&state, payload, sizeof(hil::VehicleState));
      lastRxSeq = state.seqNum;
      ctrl->updateState(state);
      ++rxCount;
    }
    break;

  case hil::HilOpcode::CMD_START:
    ctrl->setMode(hil::ControlMode::HOLD_ALT);
    break;

  case hil::HilOpcode::CMD_STOP:
    ctrl->setMode(hil::ControlMode::IDLE);
    break;

  case hil::HilOpcode::CMD_RESET:
    ctrl->init(100.0F);
    ctrl->setMode(hil::ControlMode::HOLD_ALT);
    break;

  case hil::HilOpcode::CMD_SET_MODE:
    if (PAYLOAD_LEN >= 1) {
      ctrl->setMode(static_cast<hil::ControlMode>(payload[0]));
    }
    break;

  case hil::HilOpcode::CMD_SET_TARGET:
    if (PAYLOAD_LEN == sizeof(hil::Vec3f)) {
      hil::Vec3f target;
      memcpy(&target, payload, sizeof(hil::Vec3f));
      ctrl->setTarget(target);
    }
    break;

  default:
    break;
  }
}

/* ----------------------------- Task Functions ----------------------------- */

/**
 * @brief LED blink task at 2 Hz (heartbeat).
 * @param ctx Unused.
 * @note RT-safe: single GPIO toggle.
 */
static void ledBlinkTask(void* /*ctx*/) noexcept {
  HAL_GPIO_TogglePin(firmware::LED_PORT, firmware::LED_PIN);
}

/**
 * @brief Control task at 50 Hz.
 *
 * Reads UART RX, SLIP-decodes frames, validates CRC, extracts
 * VehicleState, runs flight controller, sends ControlCmd back.
 *
 * @param ctx Pointer to FlightController.
 * @note RT-safe: Bounded execution time.
 */
static void controlTask(void* ctx) noexcept {
  auto* ctrl = static_cast<hil::FlightController*>(ctx);

  // Read available bytes from VCP UART
  const size_t AVAIL = vcpUart.read(rxChunk, sizeof(rxChunk));

  if (AVAIL > 0) {
    // Feed into streaming SLIP decoder (may contain multiple frames)
    size_t pos = 0;
    while (pos < AVAIL) {
      const size_t PREV_LEN = slipDecodeState.frameLen;
      const apex::compat::bytes_span INPUT(rxChunk + pos, AVAIL - pos);

      auto result = apex::protocols::slip::decodeChunk(slipDecodeState, slipDecodeCfg, INPUT,
                                                       decodeBuf + PREV_LEN,
                                                       hil::MAX_FRAME_PAYLOAD - PREV_LEN);

      pos += result.bytesConsumed;

      if (result.frameCompleted) {
        const size_t FRAME_LEN = PREV_LEN + result.bytesProduced;
        processFrame(decodeBuf, FRAME_LEN, ctrl);
      }

      if (result.bytesConsumed == 0) {
        break;
      }
    }
  }

  // Compute control, stamp sequence, and send response every tick
  hil::ControlCmd cmd = ctrl->computeControl();
  cmd.seqNum = ++txSeqNum;
  cmd.ackSeq = lastRxSeq;
  sendMessage(static_cast<uint8_t>(hil::HilOpcode::CONTROL_CMD), &cmd, sizeof(hil::ControlCmd));
}

/**
 * @brief Heartbeat task at 1 Hz.
 *
 * Sends HeartbeatData with cycle count, step count, and DWT overhead.
 *
 * @param ctx Unused.
 * @note RT-safe: Bounded execution time.
 */
static void heartbeatTask(void* /*ctx*/) noexcept {
  hil::HeartbeatData hb{};
  hb.cycleCount = static_cast<uint32_t>(exec.cycleCount());
  hb.stepCount = controller.stepCount();
  hb.overheadUs = lastOverheadCycles / firmware::CYCLES_PER_US;

  sendMessage(static_cast<uint8_t>(hil::HilOpcode::HEARTBEAT), &hb, sizeof(hil::HeartbeatData));
}

/**
 * @brief Profiler start task (highest priority, runs first).
 * @param ctx Unused.
 * @note RT-safe: single register read.
 */
static void profilerStartTask(void* /*ctx*/) noexcept { tickStartCycles = DWT->CYCCNT; }

/**
 * @brief Profiler end task (lowest priority, runs last).
 * @param ctx Unused.
 * @note RT-safe: single register read + subtraction.
 */
static void profilerEndTask(void* /*ctx*/) noexcept {
  lastOverheadCycles = DWT->CYCCNT - tickStartCycles;
}

/* ----------------------------- System Initialization ----------------------------- */

/**
 * @brief Configure system clock to 80 MHz using MSI + PLL.
 */
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

/**
 * @brief Initialize GPIO for LED (PA5).
 */
static void GPIO_Init() {
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitTypeDef gpioInit = {};
  gpioInit.Pin = firmware::LED_PIN;
  gpioInit.Mode = GPIO_MODE_OUTPUT_PP;
  gpioInit.Pull = GPIO_NOPULL;
  gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(firmware::LED_PORT, &gpioInit);
}

/**
 * @brief Enable DWT cycle counter for overhead profiling.
 */
static void enableDwt() {
  CoreDebug->DEMCR = CoreDebug->DEMCR | CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL = DWT->CTRL | DWT_CTRL_CYCCNTENA_Msk;
}

/* ----------------------------- Main Application ----------------------------- */

int main() {
  HAL_Init();
  SystemClock_Config();
  GPIO_Init();
  enableDwt();

  // Startup blinks (visual confirmation of init)
  for (int i = 0; i < 6; i++) {
    HAL_GPIO_TogglePin(firmware::LED_PORT, firmware::LED_PIN);
    HAL_Delay(150);
  }

  // Initialize UART (115200 8N1) for VCP communication
  apex::hal::UartConfig uartCfg;
  uartCfg.baudRate = hil::BAUD_RATE;
  static_cast<void>(vcpUart.init(uartCfg));

  // Configure SLIP decoder
  slipDecodeCfg.maxFrameSize = hil::MAX_FRAME_PAYLOAD;
  slipDecodeCfg.allowEmptyFrame = false;
  slipDecodeCfg.dropUntilEnd = true;
  slipDecodeCfg.requireTrailingEnd = true;

  // Initialize flight controller (100m target, auto-start in HOLD_ALT)
  controller.init(100.0F);
  controller.setMode(hil::ControlMode::HOLD_ALT);

  // Register scheduler tasks
  exec.addTask({profilerStartTask, nullptr, 1, 1, 0, 127, 10});
  exec.addTask({controlTask, &controller, 1, hil::CONTROL_FREQ_D, 0, 0, 1});
  exec.addTask({heartbeatTask, nullptr, 1, hil::REPORT_FREQ_D, 0, 0, 2});
  exec.addTask({ledBlinkTask, nullptr, 1, hil::LED_FREQ_D, 0, 0, 3});
  exec.addTask({profilerEndTask, nullptr, 1, 1, 0, -128, 11});

  // Run executive (blocks forever)
  static_cast<void>(exec.init());
  static_cast<void>(exec.run());

  while (1) {
  }
  return 0;
}

/* ----------------------------- Interrupt Handlers ----------------------------- */

extern "C" void SysTick_Handler() {
  HAL_IncTick();
  apex::hal::stm32::Stm32SysTickSource::isrCallback();
}

extern "C" void USART1_IRQHandler() { vcpUart.irqHandler(); }

/* ----------------------------- HAL MSP Callbacks ----------------------------- */

extern "C" void HAL_MspInit() {
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
}
