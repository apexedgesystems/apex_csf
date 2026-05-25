# STM32 Encryptor Demo

AES-256-GCM encryption firmware for NUCLEO-L476RG. First platform in
the apex_csf encryptor family (STM32, Arduino, Pico, ESP32, C2000).

UART1 (FTDI): SLIP + CRC + AES-256-GCM data channel.
UART2 (VCP): SLIP + CRC command channel (key management, stats, IV).
LED heartbeat at 2 Hz, data at 100 Hz, commands at 20 Hz.
Flash-backed key store.

Supports bare-metal (default) and FreeRTOS modes via `APEX_USE_FREERTOS`.

## Building

Bare-metal:

```bash
docker compose run --rm -T dev-stm32 make stm32
```

FreeRTOS:

```bash
docker compose run --rm -T dev-stm32 make stm32 CMAKE_EXTRA_ARGS="-DAPEX_USE_FREERTOS=ON"
```

## Flashing

```bash
make compose-stm32-flash STM32_FIRMWARE=stm32_encryptor_demo
```

## See Also

- [docs/](docs/) -- runbook, design notes, and protocol reference.
