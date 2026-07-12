# C2000 Encryptor Demo

AES-256-GCM encryptor with CAN internal-loopback demo for LAUNCHXL-
F280049C. Fifth platform in the apex_csf encryptor family.

Single-UART encryptor pipeline plus a CAN loopback channel exercising
the C2000 communications peripherals. Built against the C2000Ware
driverlib API (not the bitfield headers).

## Building

```bash
make release APP=c2000_encryptor_demo
```

## Flashing

```bash
make compose-c2000-flash C2000_FIRMWARE=c2000_encryptor_demo \
  C2000_CCXML=demos/c2000_encryptor_demo/LAUNCHXL_F280049C.ccxml
```

## See Also

- [docs/](docs/) -- runbook and protocol reference.
- [../stm32_encryptor_demo/README.md](../stm32_encryptor_demo/README.md) --
  reference implementation.
