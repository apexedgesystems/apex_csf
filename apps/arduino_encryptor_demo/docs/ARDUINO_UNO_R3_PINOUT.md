# Arduino Uno R3 Pinout

Board: **Arduino Uno R3** (ATmega328P, AVR 8-bit @ 16 MHz)

The board has three sets of female headers along the edges, matching the standard
Arduino Uno R3 form factor. The USB-B connector provides power and serial
communication via an on-board ATmega16U2 USB-to-UART bridge.

---

## Power Header (Left, Top)

| Pin | Label | Function                                       |
| --- | ----- | ---------------------------------------------- |
| 1   | IOREF | 5V Ref (logic level indicator)                 |
| 2   | RESET | Active-low reset (100 nF to GND internally)    |
| 3   | +3.3V | 3.3V output (50 mA max, from LP2985 regulator) |
| 4   | +5V   | 5V output (from USB or VIN regulator)          |
| 5   | GND   | Ground                                         |
| 6   | GND   | Ground                                         |
| 7   | VIN   | External power input (7-12V recommended)       |

---

## Analog Header (Left, Bottom)

| Pin | Label | ATmega328P | Alternate Functions              |
| --- | ----- | ---------- | -------------------------------- |
| 1   | A0    | PC0 (ADC0) | ADC channel 0                    |
| 2   | A1    | PC1 (ADC1) | ADC channel 1                    |
| 3   | A2    | PC2 (ADC2) | ADC channel 2                    |
| 4   | A3    | PC3 (ADC3) | ADC channel 3                    |
| 5   | A4    | PC4 (ADC4) | ADC channel 4, **I2C SDA** (TWI) |
| 6   | A5    | PC5 (ADC5) | ADC channel 5, **I2C SCL** (TWI) |

---

## Digital Header (Right, Bottom: D0-D7)

| Pin | Label | ATmega328P | Alternate Functions                     |
| --- | ----- | ---------- | --------------------------------------- |
| 1   | D0    | PD0        | **USART0 RXD** (shared with USB-serial) |
| 2   | D1    | PD1        | **USART0 TXD** (shared with USB-serial) |
| 3   | D2    | PD2        | INT0 (external interrupt 0)             |
| 4   | D3    | PD3        | INT1 (external interrupt 1), OC2B (PWM) |
| 5   | D4    | PD4        | T0 (Timer0 external clock)              |
| 6   | D5    | PD5        | OC0B (PWM), T1 (Timer1 external clock)  |
| 7   | D6    | PD6        | OC0A (PWM), AIN0 (analog comparator +)  |
| 8   | D7    | PD7        | AIN1 (analog comparator -)              |

---

## Digital Header (Right, Top: D8-D13)

| Pin | Label | ATmega328P | Alternate Functions               |
| --- | ----- | ---------- | --------------------------------- |
| 1   | D8    | PB0        | ICP1 (Timer1 input capture), CLKO |
| 2   | D9    | PB1        | OC1A (PWM 16-bit)                 |
| 3   | D10   | PB2        | OC1B (PWM 16-bit), **SPI SS**     |
| 4   | D11   | PB3        | OC2A (PWM), **SPI MOSI**          |
| 5   | D12   | PB4        | **SPI MISO**                      |
| 6   | D13   | PB5        | **SPI SCK**, on-board LED         |
| 7   | GND   | -          | Ground                            |
| 8   | AREF  | -          | ADC external reference voltage    |

---

## USART0 Pins

| Signal | Pin | ATmega328P | Header        |
| ------ | --- | ---------- | ------------- |
| RXD    | D0  | PD0        | Digital pin 0 |
| TXD    | D1  | PD1        | Digital pin 1 |

The single hardware USART is shared with the ATmega16U2 USB-to-UART bridge.
The 16U2 drives the same RX/TX lines that appear on D0/D1. External devices
connected to these pins will see traffic from both the USB host and the
ATmega328P. Programming via USB also uses this UART (auto-reset via DTR).

External UART devices on D0/D1 must be disconnected during firmware upload.

---

## SPI Pins

| Signal | Pin | ATmega328P | Header         |
| ------ | --- | ---------- | -------------- |
| SS     | D10 | PB2        | Digital pin 10 |
| MOSI   | D11 | PB3        | Digital pin 11 |
| MISO   | D12 | PB4        | Digital pin 12 |
| SCK    | D13 | PB5        | Digital pin 13 |

D13 (SCK) is shared with the on-board LED. The LED will flicker during SPI
transfers. The 6-pin ICSP header also exposes SPI (directly to ATmega328P, not
through the Arduino headers).

---

## I2C (TWI) Pins

| Signal | Pin | ATmega328P | Header       |
| ------ | --- | ---------- | ------------ |
| SDA    | A4  | PC4        | Analog pin 4 |
| SCL    | A5  | PC5        | Analog pin 5 |

Internal pull-ups available (~20-50 kOhm, weak). External 4.7 kOhm pull-ups
recommended for reliable operation above 100 kHz.

---

## PWM Pins

Six pins support hardware PWM output:

| Pin | Timer  | Channel | Resolution |
| --- | ------ | ------- | ---------- |
| D3  | Timer2 | OC2B    | 8-bit      |
| D5  | Timer0 | OC0B    | 8-bit      |
| D6  | Timer0 | OC0A    | 8-bit      |
| D9  | Timer1 | OC1A    | 16-bit     |
| D10 | Timer1 | OC1B    | 16-bit     |
| D11 | Timer2 | OC2A    | 8-bit      |

---

## External Interrupts

| Pin | Interrupt | ATmega328P |
| --- | --------- | ---------- |
| D2  | INT0      | PD2        |
| D3  | INT1      | PD3        |

All pins also support pin-change interrupts (PCINT0-23), grouped by port:

- Port B (D8-D13): PCINT0-5
- Port C (A0-A5): PCINT8-13
- Port D (D0-D7): PCINT16-23

---

## ICSP Header (6-pin, center of board)

| Pin | Signal | ATmega328P |
| --- | ------ | ---------- |
| 1   | MISO   | PB4        |
| 2   | VCC    | +5V        |
| 3   | SCK    | PB5        |
| 4   | MOSI   | PB3        |
| 5   | RESET  | PC6        |
| 6   | GND    | Ground     |

Used for ISP programming (bypasses bootloader) and SPI communication. Same
SPI signals as D11/D12/D13 but directly routed.

---

## Board Specifications

| Parameter           | Value                                             |
| ------------------- | ------------------------------------------------- |
| MCU                 | ATmega328P (PDIP-28)                              |
| Core                | AVR 8-bit RISC                                    |
| Clock               | 16 MHz (external ceramic resonator)               |
| Flash               | 32 KB (0.5 KB used by Optiboot bootloader)        |
| SRAM                | 2 KB (2048 bytes)                                 |
| EEPROM              | 1 KB (1024 bytes)                                 |
| Operating Voltage   | 5V                                                |
| I/O Voltage         | 5V (not 3.3V tolerant inputs -- 5V logic only)    |
| Digital I/O Pins    | 14 (6 with PWM)                                   |
| Analog Input Pins   | 6 (10-bit ADC)                                    |
| I/O Current per Pin | 20 mA (40 mA absolute max)                        |
| USB                 | USB-B via ATmega16U2 bridge                       |
| USART               | 1x USART (shared with USB bridge)                 |
| SPI                 | 1x SPI master/slave                               |
| I2C                 | 1x TWI (Two-Wire Interface)                       |
| Timers              | Timer0 (8-bit), Timer1 (16-bit), Timer2 (8-bit)   |
| External Interrupts | 2 (INT0, INT1) + 23 pin-change                    |
| Watchdog            | Programmable WDT with internal 128 kHz oscillator |
| Programming         | USB (Optiboot), ICSP (ISP), or JTAG (debugWIRE)   |

---

## Reference

| Document                    | Link                                                                                                               |
| --------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| Arduino Uno R3 Product Page | <https://store.arduino.cc/products/arduino-uno-rev3>                                                               |
| ATmega328P Datasheet        | <https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf> |
| Arduino Uno R3 Schematic    | <https://www.arduino.cc/en/uploads/Main/Arduino_Uno_Rev3-schematic.pdf>                                            |
