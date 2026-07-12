# stm32_frames_selftest

On-target golden self-test for the frames library (NUCLEO-L476RG): runs the
frame math ON the MCU and checks it against the values the hosted suite
proves on every PR -- the cross-target analogue of the cross-team
golden-vector pattern. First real firmware consumer of the tier-S math
stack (quaternion, vecmat, celestial, frames through the BAREMETAL
traversal).

## What it checks

Both `float` (the FPU path control code uses) and `double` (soft-float):

1. Transform point/vector split (lever arm applies to positions only)
2. Hand-derived quarter-turn-with-offset golden case
3. Graph resolve against explicit hand composition (3-hop chain)
4. The CG-relative resolution chain with a live CG shift (the frames
   ticket's acceptance criterion, on target)
5. Euler-321 round-trip

Plus, double-only: catalog rotation closure over the rung-1 model's period
(millimeter tolerance at Earth radius).

## Build and run

```bash
docker compose run --rm -T dev-stm32 make stm32
make stm32-flash FW=stm32_frames_selftest
```

Open the ST-Link VCP at 115200 8N1: one PASS/FAIL line per check and a
final `RESULT: PASS NN/NN`. LED (PA5): solid = pass, fast blink = fail.
Footprint: ~31 KB flash, ~4 KB RAM.
