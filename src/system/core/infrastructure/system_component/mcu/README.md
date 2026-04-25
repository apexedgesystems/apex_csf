# SystemComponent MCU

**Namespace:** `system_core::system_component::mcu`
**Platform:** Cross-platform (bare-metal compatible)
**C++ Standard:** C++17
**Library:** `system_component_mcu`

Minimal IComponent implementation for resource-constrained MCU targets. Static allocation only, no heap, no std::filesystem, no std::thread.

---

## 1. Quick Reference

| Component          | Header                 | RT-Safe      | Description                               |
| ------------------ | ---------------------- | ------------ | ----------------------------------------- |
| `McuComponentBase` | `McuComponentBase.hpp` | Queries only | Minimal IComponent with static allocation |

| Question                                            | Answer                                             |
| --------------------------------------------------- | -------------------------------------------------- |
| What is the minimal component base for MCU targets? | `McuComponentBase`                                 |
| How do I implement a bare-metal component?          | Derive from `McuComponentBase`, implement hooks    |
| What is the full UID formula?                       | `(componentId << 8) \| instanceIndex`              |
| What lifecycle hooks must I implement?              | `doInit()` (required), `doReset()` (optional)      |
| How does this differ from SystemComponentBase?      | No TPRM, no registry, no command queue, no logging |

---

## 2. When to Use

| Scenario                                      | Use This Library?               |
| --------------------------------------------- | ------------------------------- |
| Bare-metal MCU component                      | Yes                             |
| Component with McuExecutive                   | Yes                             |
| Need component identity (fullUid, status)     | Yes                             |
| Need registry integration or data descriptors | No -- use `SystemComponentBase` |
| Need internal bus or command queue            | No -- use `SystemComponentBase` |
| Need TPRM file loading                        | No -- use `SystemComponentBase` |

**Design intent:** Provides IComponent compliance with zero dynamic allocation. All queries are O(1) const noexcept. Init and reset are boot-time only.

---

## 3. Design Principles

- **Static allocation** -- No heap, no dynamic containers, no std::string
- **IComponent compliant** -- Satisfies the full interface contract
- **Idempotent init** -- Second init() call is a no-op returning success
- **Hook pattern** -- Derived classes implement `doInit()` / `doReset()`, base handles state
- **RT-safe queries** -- status(), isInitialized(), fullUid(), instanceIndex() are all O(1) const noexcept
- **MCU-sized types** -- Uses `uint8_t`/`uint32_t` directly, no `std::size_t` overhead

---

## 4. API Reference

### McuComponentBase (abstract)

```cpp
/// @note NOT RT-safe: May configure hardware.
[[nodiscard]] uint8_t init() noexcept;

/// @note NOT RT-safe: May perform cleanup.
void reset() noexcept;

/// @note RT-safe: O(1).
[[nodiscard]] uint8_t status() const noexcept;
[[nodiscard]] bool isInitialized() const noexcept;
[[nodiscard]] uint32_t fullUid() const noexcept;
[[nodiscard]] uint8_t instanceIndex() const noexcept;
[[nodiscard]] bool isRegistered() const noexcept;
[[nodiscard]] const char* lastError() const noexcept;

/// @note RT-safe: O(1), called by executive during registration.
void setInstanceIndex(uint8_t instanceIdx) noexcept;
```

### Hooks (derived classes implement)

```cpp
/// @note Required. Return 0 on success, non-zero on failure.
[[nodiscard]] virtual uint8_t doInit() noexcept = 0;

/// @note Optional. Called by reset() before clearing state.
virtual void doReset() noexcept {}
```

### Protected Mutators

```cpp
void setStatus(uint8_t s) noexcept;
void setLastError(const char* err) noexcept;
```

---

## 5. Usage Examples

### Implementing a Bare-Metal Component

```cpp
#include "src/system/core/infrastructure/system_component/mcu/inc/McuComponentBase.hpp"

class LedDriver : public system_core::system_component::mcu::McuComponentBase {
public:
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 0x0010; }
  [[nodiscard]] const char* componentName() const noexcept override { return "LedDriver"; }
  [[nodiscard]] system_core::system_component::ComponentType
  componentType() const noexcept override {
    return system_core::system_component::ComponentType::HW_DRIVER;
  }
  [[nodiscard]] const char* label() const noexcept override { return "LED"; }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    // Configure GPIO for LED output
    return 0;  // Success
  }
};
```

### Registration and Lifecycle

```cpp
LedDriver led;
led.setInstanceIndex(0);  // Executive assigns index

std::uint8_t result = led.init();
if (result != 0) {
  const char* err = led.lastError();  // Diagnostic context
}

// Runtime queries (RT-safe)
std::uint32_t uid = led.fullUid();     // (0x0010 << 8) | 0 = 0x001000
bool ready       = led.isInitialized();

// Re-initialization
led.reset();
result = led.init();
```

---

## 6. Testing

| Directory | Type       | Tests | Runs with `make test` |
| --------- | ---------- | ----- | --------------------- |
| `utst/`   | Unit tests | 10    | Yes                   |

Tests verify default construction state, init success/failure/idempotence, reset
lifecycle, registration with fullUid computation, and IComponent interface compliance.
All tests use a concrete test fixture class.

---

## 7. See Also

- `src/system/core/infrastructure/system_component/base/` -- IComponent interface and base types
- `src/system/core/infrastructure/system_component/posix/` -- Full-featured SystemComponentBase for Linux/RTOS
- `src/system/core/executive/mcu/` -- McuExecutive for MCU deployment
