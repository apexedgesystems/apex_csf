# Verification Fixtures

Reference SPICE netlists and expected outputs for device model verification.

---

## Purpose

These fixtures provide golden reference data for verifying device models
(MosfetLevel1, DiodeShockley, etc.) against industry-standard SPICE
implementations.

---

## Generating Reference Data

### Install ngspice (Ubuntu/Debian)

```bash
sudo apt-get install ngspice
```

### Run SPICE Netlist

```bash
# DC operating point
ngspice -b mosfet_level1_verification.sp -o mosfet_level1_verification.txt

# View results
cat mosfet_level1_verification.txt
```

### Extract Key Values

Look for the operating point results in the output:

```
Operating Point

V(drain)   = 5.000000e+00
V(gate)    = 2.000000e+00
I(VDS)     = -1.800000e-03
```

These values become the expected results in verification tests.

---

## Fixture Files

| File                             | Purpose                      |
| -------------------------------- | ---------------------------- |
| `mosfet_level1_verification.sp`  | Simple NMOS I-V test         |
| `diode_shockley_verification.sp` | Diode forward/reverse bias   |
| `bjt_ebersmoll_verification.sp`  | BJT common-emitter amplifier |

---

## Verification Test Pattern

```cpp
/** @test Verify MosfetLevel1 against ngspice reference data */
TEST(MosfetLevel1, VerifyAgainstNgspiceFixture)
{
  // 1. Known circuit parameters (from .sp file)
  MosfetLevel1Params params;
  params.vto    = 0.7;      // VTO from .model
  params.kp     = 100e-6;   // KP from .model
  params.lambda = 0.02;     // LAMBDA from .model
  params.w      = 10e-6;    // W from M1
  params.l      = 1e-6;     // L from M1

  // 2. Known voltages (from VDS/VGS)
  double vgs = 2.0;  // Gate-source voltage
  double vds = 5.0;  // Drain-source voltage
  double vbs = 0.0;  // Bulk-source voltage

  // 3. Expected current (from ngspice output)
  double ngspiceIds = 1.8e-3;  // I(VDS) = -1.8mA (negative = into drain)

  // 4. Calculate current with our model
  MosfetLevel1 mosfet(params);
  double ourIds = mosfet.current(vgs, vds, vbs);

  // 5. Compare (1% tolerance for single device)
  EXPECT_NEAR(ourIds, ngspiceIds, ngspiceIds * 0.01);
}
```

---

## Why Fixtures Instead of Runtime Integration?

**Pros:**

- No libngspice dependency (easier CI/CD)
- Fast tests (no runtime SPICE calls)
- Version-controlled reference data
- Deterministic (same results every run)

**Cons:**

- Manual ngspice runs required
- Reference data can become stale
- Limited to pre-generated test cases

**Recommendation:** Use fixtures for CI/CD, runtime integration for development.

---

## See Also

- **NgspiceWrapper:** `../inc/NgspiceWrapper.hpp` - Runtime integration option
- **Device Models:** `../../../devices/nonlinear/` - Models being verified
