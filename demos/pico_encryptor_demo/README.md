# Pico Encryptor Demo

AES-256-GCM encryption firmware for Raspberry Pi Pico (RP2040). Third
platform in the apex_csf encryptor family (STM32, Arduino, Pico, ESP32,
C2000).

UART0 (FTDI): SLIP + CRC + AES-256-GCM data channel.
UART1 (USB-UART): SLIP + CRC command channel (key management, stats, IV).
LED heartbeat at 2 Hz, data at 100 Hz, commands at 20 Hz.
Flash-backed key store.

## Building

```bash
docker compose run --rm -T dev-pico make pico
```

## Flashing

```bash
make compose-pico-flash PICO_FIRMWARE=pico_encryptor_demo
```

## See Also

- [docs/](docs/) -- runbook and protocol reference.
- [../stm32_encryptor_demo/README.md](../stm32_encryptor_demo/README.md) --
  reference implementation.
