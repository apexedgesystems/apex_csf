/**
 * @file main.cpp
 * @brief C2000 encryptor firmware for LAUNCHXL-F280049C.
 *
 * AES-256-GCM encryption over SCI-A with CAN internal loopback demo.
 * Uses C2000 HAL classes (C++03 via compat_legacy.hpp) following the
 * same patterns as the STM32/Pico/ESP32/Arduino encryptor apps.
 *
 * Hardware:
 *   - GPIO34 (LD5): Heartbeat blink + activity indicator
 *   - SCI-A (GPIO28 RX, GPIO29 TX): 115200 8N1, XDS110 backchannel
 *   - CAN-A: Internal loopback (external bus requires CAN transceiver)
 *
 * Serial protocol (single UART, length-prefix):
 *   [0x00]             -> "OK\r\n"  (connection test)
 *   [len:1] [pt:len]   -> [len:1] [ct:len] [tag:16] [nonce:12]  (encrypt)
 *   [0xFE]             -> CAN status report (text over SCI)
 *   [0xFF]             -> CAN loopback test (text over SCI)
 *
 * Build:   make release APP=c2000_encryptor_demo
 * Flash:   make compose-c2000-flash C2000_FIRMWARE=c2000_encryptor_demo \
 *            C2000_CCXML=apps/c2000_encryptor_demo/LAUNCHXL_F280049C.ccxml
 * Test:    python3 apps/c2000_encryptor_demo/scripts/serial_checkout.py
 */

#include "C2000Uart.hpp"
#include "aes256gcm.h"
#include "driverlib.h"
#include "device.h"

/* ----------------------------- Constants ----------------------------- */

static const uint16_t MAX_PLAINTEXT = 128;
static const uint16_t CAN_MSG_LEN = 8;

/** Development test key (same sequential pattern as STM32/Pico/ESP32 encryptors). */
static const uint16_t TEST_KEY[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};

/* ----------------------------- Peripheral Instances ----------------------------- */

static apex::hal::c2000::C2000Uart sciUart(SCIA_BASE);

/* ----------------------------- State ----------------------------- */

static uint16_t nonce[12];
static uint16_t rxBuf[MAX_PLAINTEXT];
static uint16_t ctBuf[MAX_PLAINTEXT];
static uint16_t tagBuf[16];
static uint32_t encryptCount = 0;
static uint32_t canMsgCount = 0;

/* ----------------------------- Nonce ----------------------------- */

static void incrementNonce() {
  int i;
  for (i = 11; i >= 0; i--) {
    nonce[i] = (nonce[i] + 1) & 0xFF;
    if (nonce[i] != 0)
      break;
  }
}

/* ----------------------------- CAN ----------------------------- */

static void initCan() {
  GPIO_setPinConfig(GPIO_32_CANA_TX);
  GPIO_setPinConfig(GPIO_33_CANA_RX);

  CAN_initModule(CANA_BASE);
  CAN_setBitRate(CANA_BASE, DEVICE_SYSCLK_FREQ, 500000, 20);
  CAN_enableTestMode(CANA_BASE, CAN_TEST_LBACK);

  CAN_setupMessageObject(CANA_BASE, 1, 0x100, CAN_MSG_FRAME_STD, CAN_MSG_OBJ_TYPE_TX, 0,
                         CAN_MSG_OBJ_NO_FLAGS, CAN_MSG_LEN);

  CAN_setupMessageObject(CANA_BASE, 2, 0x100, CAN_MSG_FRAME_STD, CAN_MSG_OBJ_TYPE_RX, 0x7FF,
                         CAN_MSG_OBJ_USE_ID_FILTER, CAN_MSG_LEN);

  CAN_startModule(CANA_BASE);
}

static void canStatusReport() {
  uint32_t es = CAN_getStatus(CANA_BASE);
  sciUart.print("CAN: ");
  if (es & CAN_STATUS_BUS_OFF)
    sciUart.print("BUS_OFF ");
  else if (es & CAN_STATUS_EPASS)
    sciUart.print("ERR_PASSIVE ");
  else if (es & CAN_STATUS_EWARN)
    sciUart.print("ERR_WARN ");
  else
    sciUart.print("OK ");
  if (es & CAN_STATUS_TXOK)
    sciUart.print("TXOK ");
  if (es & CAN_STATUS_RXOK)
    sciUart.print("RXOK ");
  sciUart.print("CNT=");
  sciUart.putDec(canMsgCount);
  sciUart.print("\r\n");
}

static void canLoopbackTest() {
  uint16_t txData[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0x00, 0x00};
  uint16_t rxData[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  int i;

  txData[6] = (canMsgCount >> 8) & 0xFF;
  txData[7] = canMsgCount & 0xFF;

  CAN_sendMessage(CANA_BASE, 1, CAN_MSG_LEN, txData);
  DEVICE_DELAY_US(1000);

  if (CAN_readMessage(CANA_BASE, 2, rxData)) {
    int ok = 1;
    for (i = 0; i < CAN_MSG_LEN; i++) {
      if ((txData[i] & 0xFF) != (rxData[i] & 0xFF)) {
        ok = 0;
        break;
      }
    }
    canMsgCount++;
    sciUart.print("CAN LOOPBACK ");
    if (ok) {
      sciUart.print("PASS #");
      sciUart.putDec(canMsgCount);
      sciUart.print(" TX:");
      for (i = 0; i < CAN_MSG_LEN; i++)
        sciUart.putHex8(txData[i]);
      sciUart.print(" RX:");
      for (i = 0; i < CAN_MSG_LEN; i++)
        sciUart.putHex8(rxData[i]);
    } else {
      sciUart.print("FAIL");
    }
    sciUart.print("\r\n");
  } else {
    sciUart.print("CAN LOOPBACK NO_RX\r\n");
  }
}

/* ----------------------------- GPIO Init ----------------------------- */

static void initGpio() {
  GPIO_setPinConfig(GPIO_34_GPIO34);
  GPIO_setDirectionMode(34, GPIO_DIR_MODE_OUT);
  GPIO_setPadConfig(34, GPIO_PIN_TYPE_STD);

  GPIO_setPinConfig(GPIO_28_SCIA_RX);
  GPIO_setDirectionMode(28, GPIO_DIR_MODE_IN);
  GPIO_setPadConfig(28, GPIO_PIN_TYPE_STD);
  GPIO_setQualificationMode(28, GPIO_QUAL_ASYNC);
  GPIO_setPinConfig(GPIO_29_SCIA_TX);
  GPIO_setDirectionMode(29, GPIO_DIR_MODE_OUT);
  GPIO_setPadConfig(29, GPIO_PIN_TYPE_STD);
}

/* ----------------------------- Main ----------------------------- */

void main(void) {
  int i;

  Device_init();
  Device_initGPIO();
  Interrupt_initModule();
  Interrupt_initVectorTable();

  initGpio();
  sciUart.init(115200);
  initCan();

  for (i = 0; i < 12; i++)
    nonce[i] = 0;

  for (i = 0; i < 6; i++) {
    GPIO_togglePin(34);
    DEVICE_DELAY_US(100000);
  }

  for (;;) {
    uint16_t len;

    if (!sciUart.rxReady()) {
      GPIO_togglePin(34);
      DEVICE_DELAY_US(250000);
      continue;
    }

    len = sciUart.getch();

    if (len == 0) {
      sciUart.print("OK\r\n");
      continue;
    }

    if (len == 0xFE) {
      canStatusReport();
      continue;
    }

    if (len == 0xFF) {
      canLoopbackTest();
      continue;
    }

    if (len > MAX_PLAINTEXT) {
      sciUart.print("ERR:LEN\r\n");
      continue;
    }

    for (i = 0; (uint16_t)i < len; i++) {
      rxBuf[i] = sciUart.getch();
    }

    if (aes256gcm_encrypt(TEST_KEY, nonce, rxBuf, len, 0, 0, ctBuf, tagBuf) == AES_GCM_OK) {
      sciUart.putch(len);
      sciUart.sendArr(ctBuf, len);
      sciUart.sendArr(tagBuf, 16);
      sciUart.sendArr(nonce, 12);
      incrementNonce();
      encryptCount++;
      GPIO_togglePin(34);
    }
  }
}
