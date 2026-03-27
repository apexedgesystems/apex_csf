# How to Run: C2000 Encryptor

Step-by-step commands to build, flash, and verify the AES-256-GCM encryptor
firmware on the LAUNCHXL-F280049C.

---

## Prerequisites

### Hardware

- LAUNCHXL-F280049C plugged in via USB
- Host machine running Linux (tested on Ubuntu 22.04+)

### Docker

```bash
make docker-dev-c2000
```

### udev Rules

The XDS110 debug probe needs USB write access for JTAG programming. Add to
`/etc/udev/rules.d/99-microcontrollers.rules`:

```
# TI C2000 F280049C - Serial ports
SUBSYSTEM=="tty", ENV{ID_USB_INTERFACE_NUM}=="00", ATTRS{idVendor}=="0451", ATTRS{idProduct}=="bef3", SYMLINK+="c2000_0"
SUBSYSTEM=="tty", ENV{ID_USB_INTERFACE_NUM}=="03", ATTRS{idVendor}=="0451", ATTRS{idProduct}=="bef3", SYMLINK+="c2000_0_aux"

# TI XDS110 USB Debug Probe - JTAG/flash access
SUBSYSTEMS=="usb", ATTRS{idVendor}=="0451", ATTRS{idProduct}=="bef3", MODE:="0666"

# TI XDS110 DFU mode (firmware update only)
SUBSYSTEMS=="usb", ATTRS{idVendor}=="1cbe", ATTRS{idProduct}=="00ff", MODE:="0666"
```

Then reload: `sudo udevadm control --reload-rules && sudo udevadm trigger`

### Python

```bash
pip install pyserial cryptography
```

### XDS110 Firmware (One-Time)

UniFlash 9.x requires XDS110 probe firmware v3.0.0.36+. Factory LaunchPads
ship with v2.3.x. If flashing fails with an XDS110 error, update the probe:

```bash
# Extract update tool from Docker image
docker cp $(docker create apex.dev.c2000):/opt/ti/uniflash/deskdb/content/TICloudAgent/linux/ccs_base/common/uscif/xds110/xdsdfu /tmp/xdsdfu
docker cp $(docker create apex.dev.c2000):/opt/ti/uniflash/deskdb/content/TICloudAgent/linux/ccs_base/common/uscif/xds110/firmware_3.0.0.36.bin /tmp/firmware.bin
chmod +x /tmp/xdsdfu

# Switch to DFU mode, wait for re-enumeration, flash firmware
/tmp/xdsdfu -m && sleep 2 && /tmp/xdsdfu -f /tmp/firmware.bin -r
```

Unplug and replug the board after the update.

---

## 1. Build

```bash
make release APP=c2000_encryptor_demo
```

Output:

- `build/c2000/firmware/c2000_encryptor_demo.elf`
- `build/c2000/firmware/c2000_encryptor_demo.hex`
- `build/release/c2000_encryptor_demo.tar.gz` (release tarball)

---

## 2. Flash

```bash
make compose-c2000-flash \
  C2000_FIRMWARE=c2000_encryptor_demo \
  C2000_CCXML=apps/c2000_encryptor_demo/LAUNCHXL_F280049C.ccxml
```

Expected:

```
Running...
Success
[c2000-flash] c2000_encryptor_demo flashed
```

LED5 (green) should begin blinking after flash (~2 Hz heartbeat).

---

## 3. Run Checkout

```bash
python3 apps/c2000_encryptor_demo/scripts/serial_checkout.py
```

### Expected Output

```
C2000 Encryptor Checkout - /dev/c2000_0 @ 115200
==================================================

1. Connection
  [PASS] Echo test (0x00 -> OK)

2. Encryption
  [PASS] Encrypt 4B
  [PASS] Encrypt 8B text
  [PASS] Encrypt 8B binary

3. Nonce Tracking
  [PASS] Nonces unique

4. CAN Loopback
  [PASS] CAN loopback #1
  [PASS] CAN loopback #2
  [PASS] CAN loopback #3

5. CAN Status
  [PASS] CAN status report  (CAN: OK TXOK CNT=3)

6. Rejection
  [PASS] Reject len > 128

7. Idle
  [PASS] No spurious output

==================================================
Results: 11 passed, 0 failed, 0 skipped (11 total)
```

All 11 tests should pass.

### Checkout Options

| Flag                  | Description                   |
| --------------------- | ----------------------------- |
| `--port /dev/ttyACM0` | Override serial port          |
| `--verbose`           | Show detailed output per test |

---

## 4. Manual Testing

### Echo Test

```bash
python3 -c "
import serial, time
ser = serial.Serial('/dev/c2000_0', 115200, timeout=2)
time.sleep(1)
ser.write(b'\x00')
time.sleep(0.3)
print(ser.read(ser.in_waiting))
ser.close()
"
```

Expected: `b'OK\r\n'`

### Encrypt and Verify

```bash
python3 -c "
import serial, time
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

ser = serial.Serial('/dev/c2000_0', 115200, timeout=10)
time.sleep(1)
ser.reset_input_buffer()

key = bytes(range(32))
msg = b'test'
ser.write(bytes([len(msg)]) + msg)
time.sleep(5)
data = ser.read(ser.in_waiting)

ct = data[1:5]
tag = data[5:21]
nonce = data[21:33]

pt = AESGCM(key).decrypt(nonce, ct + tag, None)
print(f'Decrypted: {pt}')
ser.close()
"
```

Expected: `Decrypted: b'test'`

### CAN Loopback

```bash
python3 -c "
import serial, time
ser = serial.Serial('/dev/c2000_0', 115200, timeout=3)
time.sleep(1)
ser.write(b'\xff')
time.sleep(1)
print(ser.read(ser.in_waiting).decode())
ser.close()
"
```

Expected: `CAN LOOPBACK PASS #1 TX:DEADBEEFCAFE0000 RX:DEADBEEFCAFE0000`

---

## Troubleshooting

| Symptom                      | Fix                                                   |
| ---------------------------- | ----------------------------------------------------- |
| `dslite.sh` not found        | Run `make docker-dev-c2000` to build the Docker image |
| Flash fails: XDS110 error    | Update probe firmware (see Prerequisites above)       |
| `/dev/c2000_0` missing       | Check USB connection, verify udev rules installed     |
| LED not blinking after flash | Verify `Running... Success` in flash output           |
| Encrypt timeout on >16B      | Normal -- unoptimized GF(2^128) multiply is slow      |
| CAN loopback `NO_RX`         | Reflash firmware to reset CAN controller state        |

---

## Protocol Reference

See [ENCRYPTOR_DESIGN.md](ENCRYPTOR_DESIGN.md) for the full serial protocol
specification, encryption details, and CAN configuration.
