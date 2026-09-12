# STM32 Encryptor Demo

AES-256-GCM encryption firmware for the NUCLEO-L476RG, NUCLEO-F767ZI,
and NUCLEO-F446RE. First platform in the apex_csf encryptor family
(STM32, Arduino, Pico, ESP32, C2000).

SLIP + CRC + AES-256-GCM data channel; SLIP + CRC command channel (key
management, stats, IV). On the L476RG the channels are two UARTs (FTDI
data, ST-Link VCP command); on the F767ZI and F446RE both share the
ST-Link VCP with a channel prefix byte. LED heartbeat at 2 Hz, data at
100 Hz. Flash-backed key store.

The board is selected by `APEX_STM32_BOARD` (`nucleo_l476rg` default,
`nucleo_f767zi`, `nucleo_f446re`); bare-metal (default) and FreeRTOS
modes by `APEX_USE_FREERTOS`.

## Building

Release package (what a deployment ships):

```bash
make release APP=stm32_encryptor_demo
```

Artifacts stage under `build/release/stm32_encryptor_demo/stm32/firmware/`
with a `build/release/stm32_encryptor_demo.tar.gz` tarball.

FreeRTOS variant (development build of the same preset):

```bash
make compose-stm32 CMAKE_EXTRA_ARGS="-DAPEX_USE_FREERTOS=ON"
```

Another board (a build directory holds one board; remove it when switching):

```bash
rm -rf build/mcu-stm32-relwithdebinfo
make release APP=stm32_encryptor_demo CMAKE_EXTRA_ARGS="-DAPEX_STM32_BOARD=nucleo_f767zi"
make release APP=stm32_encryptor_demo CMAKE_EXTRA_ARGS="-DAPEX_STM32_BOARD=nucleo_f446re"
```

## Flashing

```bash
make compose-stm32-flash STM32_FIRMWARE=stm32_encryptor_demo
```

When the board hangs off the Raspberry Pi rig instead of the development
machine, copy the binary over and flash with the Pi's `st-flash`; see
[docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md).

## See Also

- [docs/](docs/) -- runbook, design notes, and protocol reference.
