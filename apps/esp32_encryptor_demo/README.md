# ESP32 Encryptor Demo

AES-256-GCM encryption firmware for ESP32-S3 (Arduino Nano ESP32).
Fourth platform in the apex_csf encryptor family (STM32, Arduino, Pico,
ESP32, C2000).

UART0 (FTDI): SLIP + CRC + AES-256-GCM data channel.
USB CDC (native USB): SLIP + CRC command channel.
LED heartbeat at 2 Hz (WS2812 on GPIO48), data at 100 Hz, commands at 20 Hz.
NVS-backed key store.

## Building

```bash
docker compose run --rm -T dev-esp32 make esp32
```

## Flashing

```bash
make compose-esp32-flash \
  ESP32_FIRMWARE=esp32_encryptor_demo ESP32_PORT=/dev/esp32_0
```

## See Also

- [docs/](docs/) -- runbook and protocol reference.
- [../stm32_encryptor_demo/README.md](../stm32_encryptor_demo/README.md) --
  reference implementation.
