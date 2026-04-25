# SystemComponent Base Interface

**Namespace:** `system_core::system_component`
**Platform:** Cross-platform (bare-metal compatible)
**C++ Standard:** C++17
**Library:** `system_component_base`

Minimal pure interface layer for system components. Defines the contract that
all component implementations must satisfy. Zero heavy dependencies -- suitable
for bare-metal MCU targets.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [When to Use](#2-when-to-use)
3. [Design Principles](#3-design-principles)
4. [API Reference](#4-api-reference)
5. [Testing](#5-testing)
6. [See Also](#6-see-also)

---

## 1. Quick Reference

| Component     | Header                      | RT-Safe | Description                               |
| ------------- | --------------------------- | ------- | ----------------------------------------- |
| IComponent    | `IComponent.hpp`            | Yes     | Pure virtual interface for all components |
| ComponentType | `ComponentType.hpp`         | Yes     | Component classification enum + helpers   |
| Status        | `SystemComponentStatus.hpp` | Yes     | Base status codes (extensible)            |
| CommandResult | `CommandResult.hpp`         | Yes     | handleCommand() ACK/NAK status codes      |
| TransportKind | `TransportKind.hpp`         | Yes     | HW_MODEL/DRIVER transport classification  |
| TransportLink | `TransportLink.hpp`         | Yes     | Transport provisioning interface          |

| Question                                      | Module                                     |
| --------------------------------------------- | ------------------------------------------ |
| What interface must all components implement? | `IComponent`                               |
| What types are available for classification?  | `ComponentType`                            |
| What status codes does init/reset return?     | `Status`                                   |
| How do I add component-specific status codes? | Extend from `Status::EOE_SYSTEM_COMPONENT` |
| What transports can HW_MODEL components use?  | `TransportKind`                            |

---

## 2. When to Use

This library is the foundation for all component implementations in the system.
Depend on it when:

- Implementing a new system component (`SystemComponentBase`, `McuComponentBase`)
- Writing generic code that operates on any component via `IComponent*`
- Defining component-specific status codes that extend `Status`
- Declaring transport requirements for HW_MODEL/DRIVER pairing

---

## 3. Design Principles

### Zero-Dependency Interface

The base interface has no dependencies beyond `<stdint.h>` and `ComponentType.hpp`.
This allows use on bare-metal MCU targets (STM32, Arduino, Pico) where STL
containers, filesystem, and heap are unavailable.

### RT-Safety

All query methods on `IComponent` are RT-safe (`const noexcept O(1)`).
Only `init()` and `reset()` are NOT RT-safe -- they are boot-time only.

### Extensible Status Codes

`Status::EOE_SYSTEM_COMPONENT` is the sentinel for extending status codes in
derived implementations:

```cpp
enum class MyStatus : uint8_t {
  FIRST_DERIVED_CODE = static_cast<uint8_t>(Status::EOE_SYSTEM_COMPONENT),
  MY_ERROR_A,
  MY_ERROR_B
};
```

### Non-Copyable Components

`IComponent` deletes copy constructor/assignment. Components have identity
(componentId, fullUid) and must not be silently copied. Move is permitted.

---

## 4. API Reference

### IComponent Interface

**Header:** `inc/IComponent.hpp`

All methods are pure virtual. Implementations must provide all of these.

**Identity (RT-safe):**

- `componentId()` - 16-bit component type ID
- `componentName()` - Null-terminated static string
- `componentType()` - `ComponentType` classification
- `label()` - Short diagnostic label for logging

**Lifecycle (NOT RT-safe -- boot-time only):**

- `init()` - Boot-time initialization; returns 0 on success
- `reset()` - Reset component state for re-initialization

**Status (RT-safe):**

- `status()` - Last operation status code (0 = SUCCESS)
- `isInitialized()` - True after successful `init()`

**Registration (RT-safe):**

- `fullUid()` - Full UID = `(componentId << 8) | instanceIndex`
- `instanceIndex()` - Instance index (set by executive during registration)
- `isRegistered()` - True after executive calls `setInstanceIndex()`

### ComponentType

```cpp
enum class ComponentType : uint8_t {
  EXECUTIVE = 0,  // Root executive (componentId=0)
  CORE = 1,       // Core infrastructure
  SW_MODEL = 2,   // Software/environment models
  HW_MODEL = 3,   // Hardware emulation models
  SUPPORT = 4,    // Runtime support services
  DRIVER = 5      // Real hardware interfaces
};

const char* toString(ComponentType type) noexcept;   // RT-safe
const char* logSubdir(ComponentType type) noexcept;  // RT-safe ("core", "models", etc.)
bool isModel(ComponentType type) noexcept;           // SW_MODEL or HW_MODEL
bool isCoreInfra(ComponentType type) noexcept;       // EXECUTIVE or CORE
bool isSchedulable(ComponentType type) noexcept;     // All except EXECUTIVE, CORE
```

### Status Codes

```cpp
enum class Status : uint8_t {
  SUCCESS = 0,
  WARN_NOOP,
  ERROR_PARAM,
  ERROR_ALREADY_INITIALIZED,
  ERROR_NOT_INITIALIZED,
  ERROR_NOT_CONFIGURED,
  ERROR_LOAD_INVALID,
  ERROR_CONFIG_APPLY_FAIL,
  EOE_SYSTEM_COMPONENT  // Sentinel for extension
};

const char* toString(Status s) noexcept;  // RT-safe
```

---

## 5. Testing

| Directory | Type | Tests | Runs with `make test` |
| --------- | ---- | ----- | --------------------- |
| `utst/`   | Unit | 21    | Yes                   |

No performance tests -- this library is a pure interface definition with
no runtime behavior to benchmark.

---

## 6. See Also

- **SystemComponentBase** (`../apex/`) - Full-featured implementation (POSIX, heap)
- **SystemComponentLite** (`../lite/`) - Minimal implementation (static, bare-metal)
- **SystemComponent** (`../../`) - Top-level system component hierarchy
