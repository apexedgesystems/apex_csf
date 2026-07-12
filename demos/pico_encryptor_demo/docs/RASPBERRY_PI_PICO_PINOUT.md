# Raspberry Pi Pico Pinout

## Pin Assignments

```
+-----+------+--------+----------+-------------------+
| GPIO| Pin  | Func   | Periph   | Connected To      |
+-----+------+--------+----------+-------------------+
| GP0 | 1    | TX     | UART0    | FTDI RX (data)    |
| GP1 | 2    | RX     | UART0    | FTDI TX (data)    |
| GP25| N/A  | Output | GPIO     | Onboard LED       |
| D+  | N/A  | USB    | USB CDC  | Host (command)    |
+-----+------+--------+----------+-------------------+
```

GP4/GP5 (UART1) are unused. Command channel uses native USB CDC,
same pattern as the STM32 Nucleo's ST-Link VCP.

GP25 drives the onboard green LED (active high). Not exposed on a physical pin.

## Physical Pin Layout (Top View)

```
                    USB  <-- command channel (USB CDC)
              +-----[===]-----+
 [DATA] GP0  | 1           40 | VBUS
 [DATA] GP1  | 2           39 | VSYS
  [GND] GND  | 3           38 | GND
        GP2  | 4           37 | 3V3_EN
        GP3  | 5           36 | 3V3
        GP4  | 6           35 | ADC_VREF
        GP5  | 7           34 | GP28
        GND  | 8           33 | GND
        GP6  | 9           32 | GP27
        GP7  | 10          31 | GP26
        GP8  | 11          30 | RUN
        GP9  | 12          29 | GP22
        GND  | 13          28 | GND
        GP10 | 14          27 | GP21
        GP11 | 15          26 | GP20
        GP12 | 16          25 | GP19
        GP13 | 17          24 | GP18
        GND  | 18          23 | GND
        GP14 | 19          22 | GP17
        GP15 | 20          21 | GP16
              +---------------+
```

## Wiring Diagram

### Data Channel (UART0 via FTDI)

```
Pico GP0 (Pin 1) -----> FTDI FT232RL RXD
Pico GP1 (Pin 2) <----- FTDI FT232RL TXD
Pico GND (Pin 3) ------- FTDI GND
```

FTDI adapter appears as `/dev/ftdi_0` or `/dev/ttyUSB0` on the host.

### Command Channel (USB CDC)

```
Pico USB (Micro-B) -----> Host USB port
```

Native USB CDC appears as `/dev/pico_active` or `/dev/ttyACM*` on the host.
Same role as the ST-Link VCP on the STM32 Nucleo: built-in USB serial
that requires no external adapter.

## Baud Rate

UART0 runs at 115200 8N1 (matching STM32 and Arduino encryptors).
USB CDC ignores baud rate settings (full-speed USB).

## Power

The Pico is powered via USB (VBUS, 5V). The 3.3V regulator on the board
supplies the RP2040 and GPIO. The FTDI adapter has its own power from
its USB connection; only GND and signal lines are shared.
