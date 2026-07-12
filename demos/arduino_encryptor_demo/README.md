# Arduino Encryptor Demo

AES-256-GCM encryption firmware for Arduino Uno R3 (ATmega328P, 8-bit
AVR, 32 KB flash, 2 KB SRAM). Port of the STM32 encryptor to AVR.

Single UART (USART0) multiplexed via a channel prefix byte inside each
SLIP frame:

| Prefix | Channel | Purpose                     |
| ------ | ------- | --------------------------- |
| `0x00` | Data    | Encrypt pipeline            |
| `0x01` | Command | Key management, diagnostics |

No vendor HAL SDK -- register definitions come from `avr-libc`.

## Building

```bash
docker compose run --rm -T dev-arduino make arduino
```

## Flashing

```bash
make compose-arduino-flash ARDUINO_FIRMWARE=arduino_encryptor_demo
```

## See Also

- [docs/](docs/) -- runbook and protocol reference.
- [../stm32_encryptor_demo/README.md](../stm32_encryptor_demo/README.md) --
  reference implementation.
