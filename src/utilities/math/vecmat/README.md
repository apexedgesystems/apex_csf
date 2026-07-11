# Vecmat: Fixed-Size Vector/Matrix Math

**Namespace:** `apex::math::vecmat`
**Platform:** Hosted + bare-metal (BAREMETAL interface library)
**C++ Standard:** C++17 floor

Free functions over raw arrays -- `T[3]` vectors and row-major `T[9]`
matrices -- for the small dense algebra a vehicle pays per tick: cross
products, inertia multiply/solve, DCM construction. The MCU-clean sibling of
the BLAS-backed `linalg` (dynamic arrays, host-only); the substrate the
frames library composes.

## Design

- **Raw-array views, caller-owned storage** -- the same discipline as the
  quaternion library: a state-block field, a wire-frame slot, and a local
  array all work directly; there is no vector type to adopt.
- **Freestanding-clean**: scalar math routes through `apex::compat`
  (`compat_math.hpp`); C-header spellings; no heap/exceptions/RTTI; every
  function `noexcept`, `constexpr` where libm-free.
- **Status codes** where an operation can refuse (`normalizeInto` on a zero
  vector, `inverseInto`/`solveInto` on a singular matrix): `uint8_t` values
  of `VecmatStatus.hpp`'s `Status`, with `ok()`/`failed()` helpers.
- **Aliasing**: element-wise ops (`add`, `sub`, `scale`, `normalizeInto`)
  tolerate aliased operands; `cross`, `transposeInto`, `multiplyVec`,
  `multiplyMat`, `inverseInto` require distinct output (documented per
  function).

## Conventions (stated once, referenced by consumers)

- Euler 3-2-1: rotate about Z (yaw), then Y (pitch), then X (roll); radians.
- DCM maps BODY to INERTIAL: `v_i = R * v_b`, `R = Rz(yaw) Ry(pitch) Rx(roll)`;
  row-major, matching `Quaternion<T>::toRotationMatrixInto`.
- Wind axes: `F_wind = (-D, Y, -L)`; `dcmWindToBodyInto(alpha, beta)` builds
  `R_bw` with `F_body = R_bw * F_wind`.
- Quaternion <-> DCM conversion lives with the quaternion library; this
  library owns the angle-parameterized forms.

## Module reference

| Header             | Provides                                                                                                             |
| ------------------ | -------------------------------------------------------------------------------------------------------------------- |
| `Vec3Ops.hpp`      | `set/copy/add/sub/scale`, `dot`, `cross`, `normSq/norm`, `normalizeInto`                                             |
| `Mat3Ops.hpp`      | `identity`, `transposeInto`, `multiplyVec`, `multiplyMat`, `det`, `inverseInto` (adjugate), `solveInto`              |
| `Rotations.hpp`    | `dcmFromEuler321Into`, `euler321FromDcmInto` (gimbal clamp), `dcmFromAxisAngleInto` (Rodrigues), `dcmWindToBodyInto` |
| `VecmatStatus.hpp` | `Status` (`uint8_t`), `ok()`, `failed()`                                                                             |

## Performance

Single-call costs (hosted x86-64):

| Operation                          | ns  |
| ---------------------------------- | --- |
| cross + dot                        | 12  |
| inertia solve (inverse + multiply) | 27  |
| dcmFromEuler321                    | 37  |

## Verification

Typed unit tests (float + double) include a three-way cross-implementation
equivalence check -- vecmat DCMs against linalg `Rotations` and against
`Quaternion<T>` -- so a migration from either implementation is
value-identical by construction, plus orthonormality, Euler round-trips with
the gimbal clamp, Rodrigues vs quaternion angle-axis, inertia-like solve
recovery, and the wind-axes closed form with its zero-angle reduction.
Headers compile on arm-none-eabi and avr-g++ at C++17 under
`-fno-exceptions -fno-rtti`.
