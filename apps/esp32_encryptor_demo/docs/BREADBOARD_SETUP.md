# ESP32 Encryptor Breadboard Setup

Wiring guide for the Arduino Nano ESP32 encryptor on a standard breadboard.
The ESP32 uses two channels: UART0 (data via FTDI adapter) and native USB CDC
(commands via USB-C connector).

## Hardware Required

- [x] Arduino Nano ESP32 (ESP32-S3, ABX00083 or ABX00092)
- [x] DSD TECH SH-U09C5 USB-to-TTL adapter (FTDI FT232RL) -- set to **3.3V**
- [x] USB-C cable (for ESP32 command channel + power)
- [x] Breadboard (standard 830-point)
- [x] Dupont jumper wires (M-M, 3 needed)

## Breadboard Layout

ESP32 placed at the bottom of the breadboard (rows 49-63), mirrored with
USB-C facing toward the bottom edge (row 63). Left header pins are on
column H, right header pins are on column D.

```
                       USB-C  <-- command channel (to host)
                 +-----[===]-----+
Row 63  [LED] D13/SCK | L1    R1 | D12/CIPO        Row 63
Row 62          +3V3  | L2    R2 | D11/COPI        Row 62
Row 61         BOOT0  | L3    R3 | D10/CS          Row 61
Row 60            A0  | L4    R4 | D9              Row 60
Row 59            A1  | L5    R5 | D8              Row 59
Row 58            A2  | L6    R6 | D7              Row 58
Row 57            A3  | L7    R7 | D6              Row 57
Row 56       A4/SDA   | L8    R8 | D5              Row 56
Row 55       A5/SCL   | L9    R9 | D4              Row 55
Row 54            A6  | L10  R10 | D3              Row 54
Row 53            A7  | L11  R11 | D2              Row 53
Row 52          VBUS  | L12  R12 | GND             Row 52
Row 51         BOOT1  | L13  R13 | RST             Row 51
Row 50   [GND]   GND  | L14  R14 | D0/RX  [DATA]  Row 50
Row 49           VIN  | L15  R15 | D1/TX  [DATA]   Row 49
                 +---------------+
```

Left header (L1-L15) is on column H. Right header (R1-R15) is on column D.
Data channel pins (D0/RX, D1/TX, GND) are at the top of the board section
(row 49-50), opposite the USB connector.

## Wiring (3 wires)

Connect the FTDI adapter to the ESP32 UART0 pins at the bottom of the board:

| FTDI Pin | FTDI Label | Wire | ESP32 Pin | ESP32 GPIO  | Breadboard |
| -------- | ---------- | ---- | --------- | ----------- | ---------- |
| 3        | TXD        | ->   | D0 (R14)  | GPIO44 (RX) | c-50       |
| 4        | RXD        | <-   | D1 (R15)  | GPIO43 (TX) | c-49       |
| 2        | GND        | --   | GND (L14) | --          | i-50       |

Board pins occupy columns D (right header) and H (left header). FTDI
jumper wires connect to adjacent holes in the same row: column C for the
right-side signals, column I for the left-side ground.

**Signal direction:**

- FTDI TXD (output) connects to ESP32 GPIO44 (UART0 RX input)
- FTDI RXD (input) connects to ESP32 GPIO43 (UART0 TX output)
- GND is shared reference

**Do not connect FTDI VCC (pin 1).** The ESP32 is powered via its own USB-C
connector. Connecting both power sources can damage the board.

## Channel Summary

| Channel | Interface                    | Host Device                  | Baud                 |
| ------- | ---------------------------- | ---------------------------- | -------------------- |
| Data    | UART0 (GPIO43 TX, GPIO44 RX) | `/dev/ftdi_0` (FTDI adapter) | 115200 8N1           |
| Command | USB CDC (native USB-C)       | `/dev/esp32_0` (USB)         | N/A (full-speed USB) |

Both USB cables connect to the host: one for the FTDI adapter (data channel),
one for the ESP32 itself (command channel + power).

## Verification

After flashing firmware and connecting both cables:

```bash
# Both devices should appear
ls -la /dev/ftdi_0 /dev/esp32_0

# Run serial checkout
python3 apps/esp32_encryptor_demo/scripts/serial_checkout.py
```

The onboard RGB LED (GPIO48) should pulse green at 2 Hz as a heartbeat.

## FTDI Adapter Details

See [SH_U09C5_PINOUT.md](SH_U09C5_PINOUT.md) for full adapter pinout and
specifications.

## Comparison with Other Platforms

| Aspect          | STM32 (NUCLEO-L476RG)  | Pico (RP2040)    | ESP32-S3 (Nano)       |
| --------------- | ---------------------- | ---------------- | --------------------- |
| Data channel    | USART1 via FTDI        | UART0 via FTDI   | UART0 via FTDI (same) |
| Command channel | USART2 via ST-Link VCP | USB CDC (native) | USB CDC (native)      |
| FTDI voltage    | 3.3V                   | 3.3V             | 3.3V                  |
| FTDI wires      | 3 (TX, RX, GND)        | 3 (TX, RX, GND)  | 3 (TX, RX, GND)       |
| Power           | USB-C (ST-Link)        | USB Micro-B      | USB-C (native)        |
| LED type        | Simple GPIO            | Simple GPIO      | RGB (WS2812)          |
