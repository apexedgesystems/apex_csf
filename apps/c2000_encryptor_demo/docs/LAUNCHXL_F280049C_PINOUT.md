# LAUNCHXL-F280049C Pinout

Board: **LAUNCHXL-F280049C** (TMS320F280049C, C28x DSP @ 100 MHz)

The board has two BoosterPack connector sets (J1/J3 left, J2/J4 right) and
dedicated headers for CAN (J14) and power. The BoosterPack layout follows
the TI 40-pin standard.

---

## Pins Used by c2000_encryptor_demo

| GPIO | Function   | Header | Pin | Direction | Notes                     |
| ---- | ---------- | ------ | --- | --------- | ------------------------- |
| 28   | SCI-A RX   | J4     | 3   | Input     | XDS110 backchannel UART   |
| 29   | SCI-A TX   | J4     | 4   | Output    | XDS110 backchannel UART   |
| 32   | CAN-A TX   | J14    | -   | Output    | Onboard transceiver (U10) |
| 33   | CAN-A RX   | J14    | -   | Input     | Onboard transceiver (U10) |
| 34   | GPIO (LD5) | J8     | 8   | Output    | Heartbeat LED (green)     |

---

## J14 -- CAN Header

3-pin header connected to CAN-A via onboard SN65HVD234 transceiver (U10).

| Pin | Signal | Notes                |
| --- | ------ | -------------------- |
| 1   | CAN-H  | Differential high    |
| 2   | CAN-L  | Differential low     |
| 3   | GND    | Bus ground reference |

---

## J1 -- BoosterPack Left Top (10 pins)

| Pin | Label  | GPIO | Mux Options     |
| --- | ------ | ---- | --------------- |
| 1   | +3.3V  | -    | Power           |
| 2   | AIO242 | 242  | Analog          |
| 3   | GPIO13 | 13   | EPWM7B, CANB_TX |
| 4   | GPIO12 | 12   | EPWM7A, CANB_RX |
| 5   | GPIO40 | 40   | EPWM2B          |
| 6   | GPIO39 | 39   | EPWM2A          |
| 7   | GPIO56 | 56   | SPIA_CLK        |
| 8   | GPIO16 | 16   | SPIA_SOMI       |
| 9   | GPIO18 | 18   | SPIA_CLK, X2    |
| 10  | GPIO24 | 24   | SPIB_SIMO       |

## J3 -- BoosterPack Left Bottom (10 pins)

| Pin | Label  | GPIO | Mux Options         |
| --- | ------ | ---- | ------------------- |
| 1   | +5V    | -    | Power               |
| 2   | GND    | -    | Ground              |
| 3   | AIO224 | 224  | Analog              |
| 4   | AIO226 | 226  | Analog              |
| 5   | AIO228 | 228  | Analog              |
| 6   | AIO230 | 230  | Analog              |
| 7   | GPIO8  | 8    | EPWM5A, **CANB_TX** |
| 8   | GPIO9  | 9    | EPWM5B              |
| 9   | GPIO10 | 10   | EPWM6A, **CANB_RX** |
| 10  | GPIO11 | 11   | EPWM6B              |

## J2 -- BoosterPack Right Top (10 pins)

| Pin | Label  | GPIO | Mux Options     |
| --- | ------ | ---- | --------------- |
| 1   | GND    | -    | Ground          |
| 2   | GPIO15 | 15   | EPWM8B, SCIA_TX |
| 3   | GPIO14 | 14   | EPWM8A, SCIA_RX |
| 4   | GPIO58 | 58   | LINA_TX         |
| 5   | GPIO59 | 59   | LINA_RX         |
| 6   | RESET  | -    | Board reset     |
| 7   | GPIO4  | 4    | EPWM3A          |
| 8   | GPIO5  | 5    | EPWM3B          |
| 9   | GPIO0  | 0    | EPWM1A          |
| 10  | GPIO1  | 1    | EPWM1B          |

## J4 -- BoosterPack Right Bottom (10 pins)

| Pin | Label      | GPIO | Mux Options               |
| --- | ---------- | ---- | ------------------------- |
| 1   | GPIO22     | 22   | SPIB_CLK                  |
| 2   | GPIO35     | 35   | I2CA_SDA, LINA_RX         |
| 3   | **GPIO28** | 28   | **SCIA_RX** (backchannel) |
| 4   | **GPIO29** | 29   | **SCIA_TX** (backchannel) |
| 5   | GPIO25     | 25   | SPIB_SOMI                 |
| 6   | GPIO26     | 26   | SPIB_CS                   |
| 7   | GPIO37     | 37   | I2CA_SCL, LINA_TX         |
| 8   | **GPIO34** | 34   | **LD5 (green LED)**       |
| 9   | GPIO2      | 2    | EPWM2A                    |
| 10  | GPIO3      | 3    | EPWM2B                    |

---

## J8 -- BoosterPack Extended (Right)

| Pin | Label  | GPIO | Mux Options                |
| --- | ------ | ---- | -------------------------- |
| 5   | GPIO31 | 31   | **CANA_TX** / LD2 (shared) |
| 6   | GPIO23 | 23   | EPWM12A                    |

**Note:** GPIO31 is dual-mapped to CAN-A TX and LD2 (red LED). When CAN-A is
active, LD2 cannot be used as a GPIO output. The encryptor uses only GPIO34
(LD5) for LED indication to avoid this conflict.

---

## LEDs

| Label | GPIO | Color | Notes                             |
| ----- | ---- | ----- | --------------------------------- |
| LD1   | -    | Red   | Power indicator (always on)       |
| LD2   | 31   | Red   | Shared with CAN-A TX              |
| LD3   | 34   | Blue  | Directly driven                   |
| LD4   | -    | Red   | Power indicator                   |
| LD5   | 34   | Green | **Used by encryptor** (heartbeat) |

**Note:** LD3 and LD5 share GPIO34 but show different colors depending on
board revision.

---

## XDS110 Debug Probe (USB)

The onboard XDS110 provides JTAG/SWD debugging and two USB CDC serial ports:

| USB Interface | udev Symlink       | Purpose                              |
| ------------- | ------------------ | ------------------------------------ |
| 0             | `/dev/c2000_0`     | SCI-A backchannel (application UART) |
| 3             | `/dev/c2000_0_aux` | Auxiliary debug (not used by app)    |

Both ports enumerate as `/dev/ttyACMx`. The udev rules in the project create
stable symlinks based on the XDS110 serial number.

---

## Power

| Source       | Voltage | Current | Notes                                     |
| ------------ | ------- | ------- | ----------------------------------------- |
| USB (XDS110) | 5V      | 500 mA  | Primary power source                      |
| +3.3V rail   | 3.3V    | -       | Regulated from USB, available on J1 pin 1 |
| +5V rail     | 5V      | -       | Direct from USB, available on J3 pin 1    |
