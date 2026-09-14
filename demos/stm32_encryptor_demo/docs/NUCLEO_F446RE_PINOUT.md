# NUCLEO-F446RE Arduino Header Pinout

Board: **NUCLEO-F446RE** (STM32F446RE, ARM Cortex-M4 @ 180 MHz)

A Nucleo-64 board on the same MB1136 carrier as the NUCLEO-L476RG: the
Arduino header positions and connector numbers are the same, and so is the
Arduino-label-to-pin mapping below. Only the MCU behind the pins differs.

- **Arduino headers** (female sockets) -- single-row inserts matching the Arduino Uno layout
- **Morpho headers** (male pins) -- dual-row pins exposing nearly every STM32 pin

The pinout covers the **Arduino headers only** (CN5, CN6, CN8, CN9).

## CN6 -- Power (Left)

| Pin | Label | STM32 | Function |
| --- | ----- | ----- | -------- |
| 1   | NC    | -     | -        |
| 2   | IOREF | -     | 3.3V Ref |
| 3   | RESET | NRST  | Reset    |
| 4   | +3.3V | -     | 3.3V     |
| 5   | +5V   | -     | 5V out   |
| 6   | GND   | -     | Ground   |
| 7   | GND   | -     | Ground   |
| 8   | VIN   | -     | Power in |

## CN8 -- Analog (Left)

| Pin | Label | STM32 | Function                     |
| --- | ----- | ----- | ---------------------------- |
| 1   | A0    | PA0   | ADC1_IN0                     |
| 2   | A1    | PA1   | ADC1_IN1                     |
| 3   | A2    | PA4   | ADC1_IN4                     |
| 4   | A3    | PB0   | ADC1_IN8                     |
| 5   | A4    | PC1   | ADC1_IN11 / I2C via PB9 (SB) |
| 6   | A5    | PC0   | ADC1_IN10 / I2C via PB8 (SB) |

## CN5 -- Digital (Right, Top)

| Pin | Label | STM32 | Function                   |
| --- | ----- | ----- | -------------------------- |
| 1   | D8    | PA9   | USART1_TX (AF7)            |
| 2   | D9    | PC7   | TIM3_CH2                   |
| 3   | D10   | PB6   | SPI1_CS / CAN2_TX (AF9)    |
| 4   | D11   | PA7   | SPI1_MOSI                  |
| 5   | D12   | PA6   | SPI1_MISO                  |
| 6   | D13   | PA5   | SPI1_SCK / LD2 (green LED) |
| 7   | GND   | -     | Ground                     |
| 8   | AREF  | -     | AVDD                       |
| 9   | D14   | PB9   | I2C1_SDA / CAN1_TX (AF9)   |
| 10  | D15   | PB8   | I2C1_SCL / CAN1_RX (AF9)   |

## CN9 -- Digital (Right, Bottom)

| Pin | Label | STM32 | Function             |
| --- | ----- | ----- | -------------------- |
| 1   | D0    | PA3   | USART2_RX (AF7), VCP |
| 2   | D1    | PA2   | USART2_TX (AF7), VCP |
| 3   | D2    | PA10  | USART1_RX (AF7)      |
| 4   | D3    | PB3   | TIM2_CH2             |
| 5   | D4    | PB5   | CAN2_RX (AF9)        |
| 6   | D5    | PB4   | TIM3_CH1             |
| 7   | D6    | PB10  | TIM2_CH3             |
| 8   | D7    | PA8   | -                    |

## USART Pins Summary

| Peripheral        | TX       | TX Header | RX        | RX Header | Encryptor demo use             |
| ----------------- | -------- | --------- | --------- | --------- | ------------------------------ |
| USART2 (VCP)      | PA2 / D1 | CN9 pin 2 | PA3 / D0  | CN9 pin 1 | Shared data + command channel  |
| USART1 (External) | PA9 / D8 | CN5 pin 1 | PA10 / D2 | CN9 pin 3 | Free (FTDI wiring as the L476) |

The VCP pins are also on the Arduino header (D0/D1), so nothing may drive
them while the ST-Link's virtual COM port is in use.

## CAN Pins Summary

| Peripheral | TX        | TX Header | RX        | RX Header  | Notes                 |
| ---------- | --------- | --------- | --------- | ---------- | --------------------- |
| CAN1       | PB9 / D14 | CN5 pin 9 | PB8 / D15 | CN5 pin 10 | AF9, shared with I2C1 |
| CAN2       | PB6 / D10 | CN5 pin 3 | PB5 / D4  | CN9 pin 5  | AF9                   |
| CAN1 (alt) | PA12      | Morpho    | PA11      | Morpho     | AF9, USB OTG FS pins  |

A real CAN bus needs an external transceiver (3.3 V, e.g. SN65HVD230-class)
between the MCU pins and CANH/CANL. The driver's loopback and silent-loopback
modes work with nothing wired.

## Board Specifications

| Parameter         | Value                                          |
| ----------------- | ---------------------------------------------- |
| MCU               | STM32F446RET6 (LQFP64)                         |
| Core              | ARM Cortex-M4 with FPU                         |
| Max Clock         | 180 MHz (over-drive)                           |
| Flash             | 512 KB (sectors: 4 x 16 KB, 64 KB, 3 x 128 KB) |
| SRAM              | 128 KB (112 KB SRAM1 + 16 KB SRAM2)            |
| Operating Voltage | 1.7V -- 3.6V                                   |
| Debugger          | On-board ST-LINK/V2-1                          |
| USART/UART        | 4x USART, 2x UART                              |
| SPI               | 4x SPI                                         |
| I2C               | 4x I2C (legacy CCR peripheral)                 |
| CAN               | 2x CAN 2.0B                                    |
| USB               | USB OTG FS + OTG HS                            |

## Reference

| Document                                                   | Link                                                                                                    |
| ---------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| NUCLEO-F446RE Product Page                                 | <https://www.st.com/en/evaluation-tools/nucleo-f446re.html>                                             |
| UM1724 User Manual (board pinout, schematics)              | <https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf> |
| STM32F446RE Product Page                                   | <https://www.st.com/en/microcontrollers-microprocessors/stm32f446re.html>                               |
| Pin map source (variant_NUCLEO_F446RE.h, PeripheralPins.c) | <https://github.com/stm32duino/Arduino_Core_STM32/tree/main/variants/STM32F4xx/F446R(C-E)T>             |
