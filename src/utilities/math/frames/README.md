# Frames: Reference Frames and Rigid Transforms

**Namespace:** `apex::math::frames`
**Platform:** Hosted + bare-metal (BAREMETAL interface library)
**C++ Standard:** C++17 floor

Reference-frame machinery for vehicles: the SE(3) `Transform<T>` POD, the
fixed-capacity frame graph, and the standard celestial catalog (ECI/ECEF/
ENU/NED/lunar). Tier S: composes `quaternion`, `vecmat`, `celestial`, and the
concurrency `Delegate` -- the same frame math runs in the sim (`double`) and
on MCU control paths (`float`).

## The semantic contract (everything composes on this)

A `Transform<T>` is the pose of a CHILD frame expressed in its PARENT; it
maps child coordinates into the parent frame:

```
p_parent = R(q) * p_child + t
```

`q` is the child-to-parent rotation (scalar-first Hamilton, unit); `t` is the
child origin in parent coordinates. Flat POD `{T q[4]; T t[3]}` -- identity
by default, trivially copyable, bus/shm-streamable, `rotation()` gives a
`Quaternion<T>` view over the owned storage.

## The point/vector split (load-bearing)

| Call                            | Math               | For                                                                   |
| ------------------------------- | ------------------ | --------------------------------------------------------------------- |
| `transformPointInto(x, p, out)` | rotate + translate | POSITIONS: mount origins, obstacle locations -- the lever arm applies |
| `rotateVectorInto(x, v, out)`   | rotate only        | DIRECTIONS: sensor rays, velocity axes -- no position, no lever arm   |

Feeding a direction through the point path is the classic frame bug this
split exists to prevent; the graph's fluent API keeps the split at the call
site.

## Operations

- `composeInto(a, b, out)` -- apply `b` then `a` (`R_a R_b`, `R_a t_b + t_a`);
  associative; composing a grandchild up a tree is
  `composeInto(parentEdge, childEdge, out)`.
- `inverseInto(x, out)` -- the parent-to-child transform
  (`R^-1`, `-(R^-1 t)`).
- All operations `noexcept`, allocation-free, `uint8_t` status returns
  (`FramesStatus.hpp`; the transform paths are total on valid input -- the
  fallible codes serve the graph).

## The frame graph

`FrameGraph<T, CAPACITY>` owns all frame material: the registry, every edge,
and resolve. Every frame is one edge to a parent; N frames = N definitions;
any-to-any conversion is `resolve(from, to, t)` (compose up to the deepest
common ancestor, down the other side). Multiple roots are first-class (an
Earth-inertial and a Moon-inertial tree can coexist); resolving across
disconnected trees reports `ERROR_NO_PATH`.

Edge kinds: STATIC (stored, updatable via `updateStatic`), TIME_DRIVEN
(provider evaluated at the explicit sim t -- never wall clock), STATE_DRIVEN
(provider fed live state through its context). Providers are the house
`Delegate` (function pointer + context); a provider's failure status
propagates out of `resolve`. Names are logging-only and never on the resolve
path. Storage is a compile-time-capacity node table -- no heap, no pointers
on the resolve path.

The fluent form keeps the point/vector split at the call site:

```cpp
FrameGraph<double> g;
FrameId eci, ecef, body, lidar;
g.addRoot("eci", eci);
g.addTimeDriven(eci, {&earthRotationEdge, &epoch}, "ecef", ecef);
g.addStateDriven(ecef, {&poseEdge, &sixDofState}, "body", body);
g.addStatic(body, mountTransform, "lidar", lidar);

double p_ecef[3], d_body[3];
g.in(ecef).from(lidar).point(p_meas, p_ecef, t);   // position: lever arms
g.in(body).from(lidar).vector(ray_dir, d_body, t); // direction: rotation only
```

## Performance

| Operation                           | ns  |
| ----------------------------------- | --- |
| transformPoint (rotate + translate) | 38  |
| compose (one graph hop)             | 65  |

## Float posture (MCU)

`float` instantiations serve single-precision FPUs for BODY/LOCAL-frame work
(small coordinates). World-frame magnitudes (ECEF ~6.4e6 m) quantize at the
meter in float32 -- keep world-frame resolution in `double` (sim side) and
hand MCU code local-frame quantities. The catalog documentation restates
this where it bites.
