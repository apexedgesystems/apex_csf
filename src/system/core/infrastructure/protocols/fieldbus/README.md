# Fieldbus Protocols

**Namespace:** `apex::protocols::fieldbus::*`
**Platform:** Linux
**C++ Standard:** C++17

Industrial fieldbus protocol implementations for real-time and embedded systems. Zero-allocation I/O paths with RT-safe interfaces.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [Protocol Selection Guide](#2-protocol-selection-guide)
3. [Design Principles](#3-design-principles)
4. [Testing](#4-testing)
5. [See Also](#5-see-also)

---

## 1. Quick Reference

| Protocol                 | Namespace                        | Best For                       |
| ------------------------ | -------------------------------- | ------------------------------ |
| [CAN Bus](can/README.md) | `apex::protocols::fieldbus::can` | Automotive, industrial control |

### Headers

```cpp
// CAN Bus
#include "src/system/core/protocols/fieldbus/can/inc/CanDevice.hpp"
#include "src/system/core/protocols/fieldbus/can/inc/CanBusAdapter.hpp"
```

---

## 2. Protocol Selection Guide

### When to Use Fieldbus Protocols

| Scenario                          | Recommended                 |
| --------------------------------- | --------------------------- |
| Vehicle ECU communication         | **CAN**                     |
| Industrial sensor networks        | **CAN**                     |
| Deterministic real-time messaging | **CAN**                     |
| Remote network communication      | TCP/UDP (not fieldbus)      |
| Same-machine IPC                  | Unix sockets (not fieldbus) |

### Fieldbus vs Network Protocols

| Aspect             | Fieldbus (CAN)                    | Network (TCP/UDP)        |
| ------------------ | --------------------------------- | ------------------------ |
| **Physical layer** | Dedicated bus hardware            | Ethernet/IP              |
| **Frame size**     | Fixed small (8 bytes classic CAN) | Variable (up to MTU)     |
| **Latency**        | Deterministic, sub-ms             | Variable, us-ms          |
| **Topology**       | Broadcast bus                     | Point-to-point/multicast |
| **Use case**       | Embedded/automotive               | Internet/datacenter      |

### Decision Tree

```
Do you need industrial/automotive bus communication?
  |
  +-- YES --> Is it CAN bus (SocketCAN)?
  |             |
  |             +-- YES --> CAN Bus library
  |             |
  |             +-- NO --> Future: ModBus, etc.
  |
  +-- NO --> Need network communication?
              |
              +-- YES --> See Network Protocols (TCP/UDP/Unix)
```

---

## 3. Design Principles

### RT-Safety

All fieldbus I/O operations are RT-safe:

| Annotation  | Meaning                                                    |
| ----------- | ---------------------------------------------------------- |
| **RT-SAFE** | No allocation, bounded execution, safe for real-time loops |

### Zero-Allocation Design

- Frame send/receive use caller-provided buffers
- No internal heap allocation on hot paths
- Bounded execution time proportional to frame size

### Consistent Timeout Semantics

All fieldbus protocols follow consistent timeout conventions:

| timeoutMs | Behavior                                   |
| --------- | ------------------------------------------ |
| `< 0`     | Block until I/O is ready                   |
| `== 0`    | Nonblocking poll (immediate return)        |
| `> 0`     | Bounded wait (best-effort up to timeoutMs) |

### Strongly-Typed Status Codes

Each protocol provides compact status enums with clear error categories:

- `SUCCESS` - Operation completed
- `WOULD_BLOCK` - Not ready (nonblocking mode)
- `ERROR_*` - Specific error conditions

---

## 4. Testing

Run all fieldbus tests:

```bash
# Build first
docker compose run --rm -T dev-cuda make debug

# Run all fieldbus tests
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L fieldbus

# Run CAN-specific tests
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -R FieldbusCan
```

### Test Requirements

- Linux with appropriate kernel modules (e.g., `vcan` for CAN)
- NET_ADMIN privileges for virtual interface creation
- Docker environment provides required permissions

---

## 5. See Also

- **[CAN Bus Library](can/README.md)** - SocketCAN implementation
- **[Network Protocols](../network/README.md)** - TCP/UDP/Unix sockets
- **[Framing Protocols](../framing/README.md)** - SLIP/COBS for message boundaries
