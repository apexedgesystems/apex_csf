# Arduino Nano ESP32 Pinout

Board: **Arduino Nano ESP32** (ABX00083 with headers / ABX00092 without)
MCU: **ESP32-S3** (u-blox NORA-W106), Xtensa LX7 dual-core @ 240 MHz

## Pin Assignments

```
+------+------+--------+----------+-------------------+
| GPIO | Pin  | Func   | Periph   | Connected To      |
+------+------+--------+----------+-------------------+
| GP44 | D0   | RX     | UART0    | FTDI TX (data)    |
| GP43 | D1   | TX     | UART0    | FTDI RX (data)    |
| GP48 | D13  | Output | GPIO     | RGB LED (WS2812)  |
| GP19 | N/A  | USB D- | USB CDC  | Host (command)    |
| GP20 | N/A  | USB D+ | USB CDC  | Host (command)    |
+------+------+--------+----------+-------------------+
```

D0/D1 use UART0 for the data channel (FTDI adapter). Command channel uses
native USB CDC over the USB-C connector, same pattern as the Pico's native
USB and the STM32 Nucleo's ST-Link VCP.

GPIO48 drives the onboard WS2812 RGB LED (requires `led_strip` component;
simple GPIO toggle will not work).

## Physical Pin Layout (Top View)

Each header is independently numbered 1-15. Pin 1 is at the USB end.

```
                       USB-C  <-- command channel (USB CDC)
                 +-----[===]-----+
  [LED] D13/SCK | L1          R1 | D12/CIPO
          +3V3  | L2          R2 | D11/COPI
         BOOT0  | L3          R3 | D10/CS
            A0  | L4          R4 | D9
            A1  | L5          R5 | D8
            A2  | L6          R6 | D7
            A3  | L7          R7 | D6
       A4/SDA   | L8          R8 | D5
       A5/SCL   | L9          R9 | D4
            A6  | L10        R10 | D3
            A7  | L11        R11 | D2
          VBUS  | L12        R12 | GND
         BOOT1  | L13        R13 | RST
  [GND]   GND  | L14        R14 | D0/RX  [DATA]
          VIN   | L15        R15 | D1/TX  [DATA]
                 +---------------+
```

## Wiring Diagram

### Data Channel (UART0 via FTDI)

```
ESP32 D0/RX (R14, GPIO44) <----- FTDI FT232RL TXD
ESP32 D1/TX (R15, GPIO43) -----> FTDI FT232RL RXD
ESP32 GND   (L14)         ------- FTDI GND
```

FTDI adapter appears as `/dev/ftdi_0` or `/dev/ttyUSB0` on the host.

### Command Channel (USB CDC)

```
ESP32 USB-C -----> Host USB port
```

Native USB CDC appears as `/dev/esp32_0` or `/dev/ttyACM*` on the host.
Same role as the Pico's native USB and the STM32 Nucleo's ST-Link VCP:
built-in USB serial that requires no external adapter.

## Header Pin Tables

Source: [ABX00083 Datasheet](https://docs.arduino.cc/resources/datasheets/ABX00083-datasheet.pdf)

Left header (pins L1-L15):

| Pin | Arduino Label | ESP32-S3 GPIO | Encryptor Use             |
| --- | ------------- | ------------- | ------------------------- |
| L1  | D13 / SCK     | GPIO48        | RGB LED (WS2812)          |
| L2  | +3V3          | --            | Power output (3.3V)       |
| L3  | BOOT0         | --            | Board reset 0             |
| L4  | A0            | GPIO1         | --                        |
| L5  | A1            | GPIO2         | --                        |
| L6  | A2            | GPIO3         | --                        |
| L7  | A3            | GPIO4         | --                        |
| L8  | A4 / SDA      | GPIO11        | --                        |
| L9  | A5 / SCL      | GPIO12        | --                        |
| L10 | A6            | GPIO13        | --                        |
| L11 | A7            | GPIO14        | --                        |
| L12 | VBUS          | --            | USB power (5V from USB-C) |
| L13 | BOOT1         | --            | Board reset 1             |
| L14 | GND           | --            | **FTDI GND**              |
| L15 | VIN           | --            | External power input      |

Right header (pins R1-R15):

| Pin | Arduino Label | ESP32-S3 GPIO | Encryptor Use           |
| --- | ------------- | ------------- | ----------------------- |
| R1  | D12 / CIPO    | GPIO47        | --                      |
| R2  | D11 / COPI    | GPIO38        | --                      |
| R3  | D10 / CS      | GPIO21        | --                      |
| R4  | D9            | GPIO18        | --                      |
| R5  | D8            | GPIO17        | --                      |
| R6  | D7            | GPIO10        | --                      |
| R7  | D6            | GPIO9         | --                      |
| R8  | D5            | GPIO8         | --                      |
| R9  | D4            | GPIO7         | --                      |
| R10 | D3            | GPIO6         | --                      |
| R11 | D2            | GPIO5         | --                      |
| R12 | GND           | --            | Ground                  |
| R13 | RST           | --            | Reset                   |
| R14 | D0 / RX       | GPIO44        | **UART0 RX (FTDI TXD)** |
| R15 | D1 / TX       | GPIO43        | **UART0 TX (FTDI RXD)** |

## Specifications

| Parameter   | Value                                |
| ----------- | ------------------------------------ |
| MCU         | ESP32-S3 (Xtensa LX7 dual-core)      |
| Clock       | Up to 240 MHz                        |
| SRAM        | 512 KB internal                      |
| Flash       | 16 MB external QSPI                  |
| PSRAM       | 8 MB external                        |
| I/O Voltage | 3.3V (NOT 5V tolerant)               |
| USB         | USB-C (native USB via GPIO19/GPIO20) |
| WiFi        | 802.11 b/g/n (WiFi 4)                |
| Bluetooth   | BLE 5.0                              |
| Form Factor | Arduino Nano (45 x 18 mm)            |

## Restricted Pins

**Strapping pins** (affect boot mode, avoid for general I/O):

- GPIO0: Boot mode select
- GPIO45: VDD_SPI voltage select
- GPIO46: Boot mode / JTAG select

**Flash-connected** (reserved, do not use):

- GPIO26-32: Connected to internal SPI flash

**USB pins** (used by native USB, do not reassign):

- GPIO19: USB D-
- GPIO20: USB D+

## UART Configuration

The ESP32-S3 has 3 UART peripherals. Pin assignment is flexible (GPIO matrix),
but we use the default UART0 pins for simplicity:

| UART  | TX GPIO     | RX GPIO     | Use                 |
| ----- | ----------- | ----------- | ------------------- |
| UART0 | GPIO43 (D1) | GPIO44 (D0) | Data channel (FTDI) |
| UART1 | (available) | (available) | Not used            |
| UART2 | (available) | (available) | Not used            |

## Baud Rate

UART0 runs at 115200 8N1 (matching STM32, Arduino, and Pico encryptors).
USB CDC ignores baud rate settings (full-speed USB).

## Power

The ESP32 is powered via USB-C (VBUS, 5V). The onboard 3.3V regulator
supplies the ESP32-S3 and GPIO. The FTDI adapter has its own power from
its USB connection; only GND and signal lines are shared.

## Reference

| Document                | Link                                                                                    |
| ----------------------- | --------------------------------------------------------------------------------------- |
| ABX00083 Datasheet      | <https://docs.arduino.cc/resources/datasheets/ABX00083-datasheet.pdf>                   |
| ABX00083 Full Pinout    | <https://docs.arduino.cc/resources/pinouts/ABX00083-full-pinout.pdf>                    |
| ABX00083 Schematics     | <https://docs.arduino.cc/resources/schematics/ABX00083-schematics.pdf>                  |
| Arduino Nano ESP32 docs | <https://docs.arduino.cc/nano-esp32>                                                    |
| ESP32-S3 datasheet      | <https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf> |
