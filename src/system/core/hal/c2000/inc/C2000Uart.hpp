#ifndef APEX_HAL_C2000_UART_HPP
#define APEX_HAL_C2000_UART_HPP
/**
 * @file C2000Uart.hpp
 * @brief SCI (UART) driver for TI C2000 F28004x (C++03 compatible).
 *
 * Standalone UART driver using driverlib API. Does not inherit from IUart
 * (C2000 CGT only supports C++03). Same API shape as other platforms.
 *
 * Supported devices: TMS320F28004x (SCI-A, SCI-B)
 */

#include "src/utilities/compatibility/inc/compat_legacy.hpp"

#include <stddef.h>
#include <stdint.h>

#ifndef APEX_HAL_C2000_MOCK
#include "driverlib.h"
#include "device.h"
#endif

namespace apex {
namespace hal {
namespace c2000 {

/* ----------------------------- C2000Uart ----------------------------- */

class C2000Uart {
public:
#ifndef APEX_HAL_C2000_MOCK
  explicit C2000Uart(uint32_t base, uint32_t lspclkHz)
      : base_(base), lspclkHz_(lspclkHz), initialized_(false) {}

  explicit C2000Uart(uint32_t base)
      : base_(base), lspclkHz_(DEVICE_LSPCLK_FREQ), initialized_(false) {}
#else
  C2000Uart() : base_(0), lspclkHz_(25000000U), initialized_(false) {}
#endif

  void init(APEX_MAYBE_UNUSED uint32_t baudRate) {
#ifndef APEX_HAL_C2000_MOCK
    SCI_performSoftwareReset(base_);
    SCI_setConfig(base_, lspclkHz_, baudRate,
                  (SCI_CONFIG_WLEN_8 | SCI_CONFIG_STOP_ONE | SCI_CONFIG_PAR_NONE));
    SCI_resetChannels(base_);
    SCI_enableFIFO(base_);
    SCI_enableModule(base_);
    SCI_performSoftwareReset(base_);
#endif
    initialized_ = true;
  }

  void putch(APEX_MAYBE_UNUSED uint16_t byte) {
#ifndef APEX_HAL_C2000_MOCK
    while (SCI_getTxFIFOStatus(base_) == SCI_FIFO_TX16) {
    }
    SCI_writeCharNonBlocking(base_, byte & 0xFF);
#endif
  }

  uint16_t getch() {
#ifndef APEX_HAL_C2000_MOCK
    while (SCI_getRxFIFOStatus(base_) == SCI_FIFO_RX0) {
    }
    return SCI_readCharNonBlocking(base_) & 0xFF;
#else
    return 0;
#endif
  }

  bool rxReady() {
#ifndef APEX_HAL_C2000_MOCK
    return SCI_getRxFIFOStatus(base_) != SCI_FIFO_RX0;
#else
    return false;
#endif
  }

  void print(const char* str) {
    while (*str) {
      putch((uint16_t)*str);
      str++;
    }
  }

  void sendArr(const uint16_t* data, uint16_t len) {
    uint16_t i;
    for (i = 0; i < len; i++)
      putch(data[i]);
  }

  void putHex8(uint16_t val) {
    const char HEX[] = "0123456789ABCDEF";
    putch(HEX[(val >> 4) & 0xF]);
    putch(HEX[val & 0xF]);
  }

  void putDec(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) {
      putch('0');
      return;
    }
    while (val > 0) {
      buf[i++] = '0' + (val % 10);
      val /= 10;
    }
    while (--i >= 0)
      putch(buf[i]);
  }

  bool isInitialized() const { return initialized_; }

private:
  APEX_MAYBE_UNUSED uint32_t base_;
  APEX_MAYBE_UNUSED uint32_t lspclkHz_;
  bool initialized_;
};

} /* namespace c2000 */
} /* namespace hal */
} /* namespace apex */

#endif /* APEX_HAL_C2000_UART_HPP */
