# Pico Encryptor Breadboard Setup

Wiring guide for the Raspberry Pi Pico encryptor on a standard breadboard.
The Pico uses two channels: UART0 (data via FTDI adapter) and native USB CDC
(commands via Pico's Micro-B connector).

## Hardware Required

- [x] Raspberry Pi Pico (RP2040)
- [x] DSD TECH SH-U09C5 USB-to-TTL adapter (FTDI FT232RL) -- set to **3.3V**
- [x] USB Micro-B cable (for Pico command channel + power)
- [x] Breadboard (standard 830-point)
- [x] Dupont jumper wires (M-M, 3 needed)

## Breadboard Layout

Pico placed with pins 1-20 on one side, pins 21-40 on the other. Pin 1 (GP0)
starts at row 1 of the breadboard. USB connector faces away from the board.

```
                        USB  <-- command channel (to host)
                  +-----[===]-----+
Row 1   [DATA] GP0  | 1           40 | VBUS        Row 1
Row 2   [DATA] GP1  | 2           39 | VSYS        Row 2
Row 3   [GND]  GND  | 3           38 | GND         Row 3
Row 4          GP2  | 4           37 | 3V3_EN      Row 4
Row 5          GP3  | 5           36 | 3V3         Row 5
...                  ...             ...
Row 20         GP15 | 20          21 | GP16        Row 20
                  +---------------+
```

Pins 1 and 2 are on column C. Pin 40 side starts at column H (row 1).

## Wiring (3 wires)

Connect the FTDI adapter to the Pico UART0 pins:

| FTDI Pin | FTDI Label | Wire | Pico Pin | Pico GPIO | Breadboard   |
| -------- | ---------- | ---- | -------- | --------- | ------------ |
| 3        | TXD        | ->   | Pin 2    | GP1 (RX)  | Row 2, Col C |
| 4        | RXD        | <-   | Pin 1    | GP0 (TX)  | Row 1, Col C |
| 2        | GND        | --   | Pin 3    | GND       | Row 3, Col C |

**Signal direction:**

- FTDI TXD (output) connects to Pico GP1 (UART0 RX input)
- FTDI RXD (input) connects to Pico GP0 (UART0 TX output)
- GND is shared reference

**Do not connect FTDI VCC (pin 1).** The Pico is powered via its own USB
connector. Connecting both power sources can damage the board.

## Channel Summary

| Channel | Interface              | Host Device                      | Baud                 |
| ------- | ---------------------- | -------------------------------- | -------------------- |
| Data    | UART0 (GP0 TX, GP1 RX) | `/dev/ftdi_0` (FTDI adapter)     | 115200 8N1           |
| Command | USB CDC (native)       | `/dev/pico_active` (USB Micro-B) | N/A (full-speed USB) |

Both USB cables connect to the host: one for the FTDI adapter (data channel),
one for the Pico itself (command channel + power).

## Verification

After flashing firmware and connecting both cables:

```bash
# Both devices should appear
ls -la /dev/ftdi_0 /dev/pico_active

# Run serial checkout
python3 apps/pico_encryptor_demo/scripts/serial_checkout.py
```

The Pico's onboard LED (GP25) should blink at 2 Hz as a heartbeat indicator.

## FTDI Adapter Details

See [SH_U09C5_PINOUT.md](SH_U09C5_PINOUT.md) for full adapter pinout and
specifications.

## Comparison with STM32 Setup

The Pico setup mirrors the STM32 encryptor:

| Aspect          | STM32 (NUCLEO-L476RG)  | Pico (RP2040)                 |
| --------------- | ---------------------- | ----------------------------- |
| Data channel    | USART1 via FTDI        | UART0 via FTDI (same adapter) |
| Command channel | USART2 via ST-Link VCP | USB CDC (native)              |
| FTDI voltage    | 3.3V                   | 3.3V                          |
| FTDI wires      | 3 (TX, RX, GND)        | 3 (TX, RX, GND)               |
| Power           | USB-C (ST-Link)        | USB Micro-B (native)          |
