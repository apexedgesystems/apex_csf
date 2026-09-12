# NUCLEO-F767ZI Header Pinout

Board: **NUCLEO-F767ZI** (STM32F767ZI, ARM Cortex-M7 @ 216 MHz)

A Nucleo-144 board (MB1137). Its Zio connectors (CN7, CN8, CN9, CN10) carry
an Arduino Uno V3 subset plus many more signals; the ST Morpho headers
(CN11, CN12) expose nearly every pin. The pinout below lists the Arduino
labels a hookup uses, by Arduino label and STM32 pin; connector pin numbers
are in the user manual.

## Arduino Uno V3 Subset (Zio)

| Label | STM32 | Function                 |
| ----- | ----- | ------------------------ |
| D0    | PG9   | USART6_RX (AF8)          |
| D1    | PG14  | USART6_TX (AF8)          |
| D2    | PF15  | -                        |
| D3    | PE13  | TIM1_CH3                 |
| D4    | PF14  | -                        |
| D5    | PE11  | TIM1_CH2                 |
| D6    | PE9   | TIM1_CH1                 |
| D7    | PF13  | -                        |
| D8    | PF12  | -                        |
| D9    | PD15  | TIM4_CH4                 |
| D10   | PD14  | TIM4_CH3 / SPI1_CS       |
| D11   | PA7   | SPI1_MOSI                |
| D12   | PA6   | SPI1_MISO                |
| D13   | PA5   | SPI1_SCK                 |
| D14   | PB9   | I2C1_SDA / CAN1_TX (AF9) |
| D15   | PB8   | I2C1_SCL / CAN1_RX (AF9) |
| A0    | PA3   | ADC1_IN3                 |
| A1    | PC0   | ADC1_IN10                |
| A2    | PC3   | ADC1_IN13                |
| A3    | PF3   | ADC3_IN9                 |
| A4    | PF5   | ADC3_IN15                |
| A5    | PF10  | ADC3_IN8                 |

## LEDs and Button

| Item | STM32 | Notes                          |
| ---- | ----- | ------------------------------ |
| LD1  | PB0   | Green; the encryptor heartbeat |
| LD2  | PB7   | Blue                           |
| LD3  | PB14  | Red                            |
| USER | PC13  | Blue button                    |

## USART Pins Summary

| Peripheral   | TX        | RX       | Encryptor demo use                  |
| ------------ | --------- | -------- | ----------------------------------- |
| USART3 (VCP) | PD8       | PD9      | Shared data + command channel (AF7) |
| USART6       | PG14 / D1 | PG9 / D0 | Free (Arduino header, AF8)          |

PA9/PA10 serve the USB user connector (CN13) on this board and are not the
USART1 header pins they are on the Nucleo-64 boards.

## CAN Pins Summary

| Peripheral | TX         | RX         | AF   | Notes                            |
| ---------- | ---------- | ---------- | ---- | -------------------------------- |
| CAN1       | PB9 / D14  | PB8 / D15  | AF9  | Arduino header, shared with I2C1 |
| CAN1 (alt) | PD1 / D66  | PD0 / D67  | AF9  | Zio                              |
| CAN1 (alt) | PA12       | PA11       | AF9  | USB OTG FS pins, avoid           |
| CAN2       | PB6 / D26  | PB5 / D22  | AF9  | Zio                              |
| CAN2 (alt) | PB13 / D18 | PB12 / D19 | AF9  | Zio                              |
| CAN3       | PA15 / D20 | PA8 / D90  | AF11 | Zio / Morpho                     |
| CAN3 (alt) | PB4 / D25  | PB3 / D23  | AF11 | Zio                              |

A real CAN bus needs an external transceiver (3.3 V, e.g. SN65HVD230-class)
between the MCU pins and CANH/CANL. The driver's loopback and silent-loopback
modes work with nothing wired.

## Board Specifications

| Parameter         | Value                                                     |
| ----------------- | --------------------------------------------------------- |
| MCU               | STM32F767ZIT6 (LQFP144)                                   |
| Core              | ARM Cortex-M7 with double-precision FPU                   |
| Max Clock         | 216 MHz (over-drive)                                      |
| Flash             | 2 MB (sectors: 4 x 32 KB, 128 KB, 7 x 256 KB single-bank) |
| SRAM              | 512 KB (128 KB DTCM + 368 KB SRAM1 + 16 KB SRAM2)         |
| Operating Voltage | 1.7V -- 3.6V                                              |
| Debugger          | On-board ST-LINK/V2-1                                     |
| USART/UART        | 4x USART, 4x UART                                         |
| SPI               | 6x SPI                                                    |
| I2C               | 4x I2C                                                    |
| CAN               | 3x CAN 2.0B                                               |
| Ethernet          | 10/100 MAC with on-board PHY (RJ45)                       |
| USB               | USB OTG FS (CN13) + OTG HS                                |

## Reference

| Document                                                   | Link                                                                                                            |
| ---------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| NUCLEO-F767ZI Product Page                                 | <https://www.st.com/en/evaluation-tools/nucleo-f767zi.html>                                                     |
| UM1974 User Manual (board pinout, schematics)              | <https://www.st.com/resource/en/user_manual/um1974-stm32-nucleo144-boards-mb1137-stmicroelectronics.pdf>        |
| STM32F767ZI Product Page                                   | <https://www.st.com/en/microcontrollers-microprocessors/stm32f767zi.html>                                       |
| Pin map source (variant_NUCLEO_F767ZI.h, PeripheralPins.c) | <https://github.com/stm32duino/Arduino_Core_STM32/tree/main/variants/STM32F7xx/F765Z(G-I)T_F767Z(G-I)T_F777ZIT> |
