# DSD TECH SH-U09C5 USB-to-TTL Adapter

Adapter: **DSD TECH SH-U09C5** (FTDI FT232RL chipset)

USB-to-TTL serial converter cable with 6-pin DuPont header. Supports multiple
TTL voltage levels (1.8V, 2.5V, 3.3V, 5V). Active on host as `/dev/ttyUSB0`
(udev symlink: `/dev/ftdi_0`).

## Pin Header

Pin order as printed on the adapter PCB (pin 1 at edge marking):

| Pin | Label | Direction | Description                                   |
| --- | ----- | --------- | --------------------------------------------- |
| 1   | VCC   | Power out | 3.3V or 5V (jumper-selectable on adapter PCB) |
| 2   | GND   | Power     | Ground reference                              |
| 3   | TXD   | Output    | Data from host (adapter) to target (MCU)      |
| 4   | RXD   | Input     | Data from target (MCU) to host (adapter)      |
| 5   | RTS   | Output    | Request To Send (active low)                  |
| 6   | CTS   | Input     | Clear To Send (active low)                    |

## Voltage Selection

The adapter has a jumper or solder bridge to select VCC/logic level output:

- **3.3V** -- Required for RP2040 (3.3V I/O only)
- **5V** -- For 5V-tolerant targets only

## Specifications

| Parameter          | Value                                                      |
| ------------------ | ---------------------------------------------------------- |
| Chipset            | FTDI FT232RL                                               |
| USB                | USB 2.0                                                    |
| Max Baud Rate      | 3 Mbps                                                     |
| TTL Levels         | 1.8V, 2.5V, 3.3V, 5V (selectable)                          |
| USB Vendor:Product | 0403:6001                                                  |
| LED Indicators     | PWR (power), TXD (receiving data), RXD (transmitting data) |
| Connector          | 6-pin DuPont header                                        |

The adapter also exposes additional signals (DSR, RI, DCD, DTR, RESET) on
the PCB beyond the 6-pin cable header.

## Host Detection

```bash
# Verify adapter is connected
lsusb | grep 0403:6001
# Expected: Future Technology Devices International, Ltd FT232 Serial (UART) IC

# Check device node
ls -la /dev/ttyUSB0 /dev/ftdi_0
```

## Reference

| Document                | Link                                                    |
| ----------------------- | ------------------------------------------------------- |
| SH-U09C5 Product Page   | <https://www.deshide.com/product-details_SH-U09C5.html> |
| FT232R Datasheet (FTDI) | <https://ftdichip.com/products/ft232rl/>                |
| FTDI VCP Drivers        | <https://ftdichip.com/drivers/vcp-drivers/>             |
