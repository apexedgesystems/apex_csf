# NUCLEO-L476RG Arduino Header Pinout

Board: **NUCLEO-L476RG** (STM32L476RG, ARM Cortex-M4 @ 80 MHz)

The board has two sets of headers on each side:

- **Arduino headers** (female sockets) -- single-row inserts matching Arduino Uno layout
- **Morpho headers** (male pins) -- dual-row pins exposing nearly every STM32 pin

The pinout below covers the **Arduino headers only** (CN5, CN6, CN8, CN9).
Use the female sockets, not the Morpho pin rows.

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

| Pin | Label | STM32   | Function              |
| --- | ----- | ------- | --------------------- |
| 1   | A0    | PA0     | ADC12_IN5             |
| 2   | A1    | PA1     | ADC12_IN6             |
| 3   | A2    | PA4     | ADC12_IN9             |
| 4   | A3    | PB0     | ADC12_IN15            |
| 5   | A4    | PC1/PB9 | ADC123_IN2 / I2C1_SDA |
| 6   | A5    | PC0/PB8 | ADC123_IN1 / I2C1_SCL |

## CN5 -- Digital (Right, Top)

| Pin | Label | STM32 | Function                   |
| --- | ----- | ----- | -------------------------- |
| 1   | D8    | PA9   | USART1_TX (AF7)            |
| 2   | D9    | PC7   | TIM3_CH2                   |
| 3   | D10   | PB6   | TIM4_CH1 / SPI1_CS         |
| 4   | D11   | PA7   | TIM17_CH1 / SPI1_MOSI      |
| 5   | D12   | PA6   | SPI1_MISO                  |
| 6   | D13   | PA5   | SPI1_SCK / LD2 (green LED) |
| 7   | GND   | -     | Ground                     |
| 8   | AREF  | -     | AVDD                       |
| 9   | D14   | PB9   | I2C1_SDA / CAN1_TX (AF9)   |
| 10  | D15   | PB8   | I2C1_SCL / CAN1_RX (AF9)   |

## CN9 -- Digital (Right, Bottom)

| Pin | Label | STM32 | Function        |
| --- | ----- | ----- | --------------- |
| 1   | D0    | PA3   | USART2_RX (AF7) |
| 2   | D1    | PA2   | USART2_TX (AF7) |
| 3   | D2    | PA10  | USART1_RX (AF7) |
| 4   | D3    | PB3   | TIM2_CH2        |
| 5   | D4    | PB5   | -               |
| 6   | D5    | PB4   | TIM3_CH1        |
| 7   | D6    | PB10  | TIM2_CH3        |
| 8   | D7    | PA8   | -               |

## USART Pins Summary

| Peripheral        | TX       | TX Header | RX        | RX Header |
| ----------------- | -------- | --------- | --------- | --------- |
| USART2 (VCP)      | PA2 / D1 | CN9 pin 2 | PA3 / D0  | CN9 pin 1 |
| USART1 (External) | PA9 / D8 | CN5 pin 1 | PA10 / D2 | CN9 pin 3 |

## CAN Pins Summary

| Peripheral | TX        | TX Header | RX        | RX Header  | Notes                 |
| ---------- | --------- | --------- | --------- | ---------- | --------------------- |
| CAN1       | PB9 / D14 | CN5 pin 9 | PB8 / D15 | CN5 pin 10 | AF9, shared with I2C1 |

CAN1 requires an external transceiver (e.g., SN65HVD230) to connect to a
differential CAN bus (CANH/CANL). Internal loopback mode works without a transceiver.

## Flow Control (Morpho Headers Only)

These pins are **not** on the Arduino headers. Access via Morpho connector CN10.

| Peripheral | Pin  | STM32 | AF  |
| ---------- | ---- | ----- | --- |
| USART1_CTS | CN10 | PA11  | AF7 |
| USART1_RTS | CN10 | PA12  | AF7 |

## Board Specifications

| Parameter         | Value                              |
| ----------------- | ---------------------------------- |
| MCU               | STM32L476RGT6 (LQFP64)             |
| Core              | ARM Cortex-M4 with FPU             |
| Max Clock         | 80 MHz                             |
| Flash             | 1 MB                               |
| SRAM              | 128 KB (96 KB SRAM1 + 32 KB SRAM2) |
| Operating Voltage | 1.71V -- 3.6V                      |
| Temperature Range | -40 to +85 C                       |
| Debugger          | On-board ST-LINK/V2-1 (USB-C)      |
| USB               | USB OTG full-speed                 |
| USART/UART        | 3x USART, 2x UART, 1x LPUART       |
| SPI               | 3x SPI, 1x Quad SPI                |
| I2C               | 3x I2C                             |
| CAN               | 1x CAN 2.0B                        |
| ADC               | 3x 12-bit ADC (up to 24 channels)  |
| DAC               | 2x 12-bit DAC                      |
| Timers            | 11x (incl. 2x 32-bit)              |

## Reference

| Document                                      | Link                                                                                                    |
| --------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| NUCLEO-L476RG Product Page                    | <https://www.st.com/en/evaluation-tools/nucleo-l476rg.html>                                             |
| UM1724 User Manual (board pinout, schematics) | <https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf> |
| DS10198 Datasheet (MCU specs, AF tables)      | <https://www.st.com/resource/en/datasheet/stm32l476rg.pdf>                                              |
| STM32L476RG Product Page                      | <https://www.st.com/en/microcontrollers-microprocessors/stm32l476rg.html>                               |
