# System Component Library

Component base classes and lifecycle management for the Apex executive framework. Four directory tiers organize the hierarchy:

| Tier | Contents | Instantiable? |
|------|----------|---------------|
| `base/` | Pure virtual `IComponent` interface | No (pure virtual) |
| `core/` | `ComponentCore` shared concrete base (identity, lifecycle, registration state) | No (still abstract) |
| `posix/` | `SystemComponentBase` and POSIX-tier specializations | Yes (full POSIX features) |
| `mcu/` | `McuComponentBase` for bare-metal targets | Yes (static allocation) |

**Namespace:** `system_core::system_component` (POSIX), `system_core::system_component::mcu` (MCU)
**Libraries:** `system_component_base` (INTERFACE), `system_component_core` (INTERFACE), `system_core_system_component` (SHARED, POSIX), `system_component_mcu` (INTERFACE)
**Platform:** Cross-platform (`posix/`: Linux/RTOS, `mcu/`: bare-metal)
**C++ Standard:** C++23

---

## 1. Quick Reference

| Component                  | Type               | Purpose                                                                                 | RT-Safe                                       |
| -------------------------- | ------------------ | --------------------------------------------------------------------------------------- | --------------------------------------------- |
| `IComponent`               | Abstract interface | Minimal pure interface (identity, lifecycle, status)                                    | Queries: Yes, Lifecycle: No                   |
| `ComponentCore`            | Abstract class     | Shared concrete base: identity, lifecycle, registration state (no platform deps)        | Queries: Yes, Lifecycle: No                   |
| `ComponentType`            | Enum               | Component classification (EXECUTIVE, CORE, SW_MODEL, HW_MODEL, SUPPORT, DRIVER)         | Yes                                           |
| `Status`                   | Enum               | Typed status codes with extension marker                                                | Yes                                           |
| `SystemComponentBase`      | Abstract class     | POSIX-tier base on ComponentCore (TPRM, internal bus, logging, data descriptors)        | Queries: Yes, Lifecycle: No                   |
| `SystemComponent<T>`       | Template class     | A/B parameter staging with file loading and hot-reload                                  | `activeParams()`: Yes, `load()`/`apply()`: No |
| `SchedulableComponentBase` | Abstract class     | Base for components with scheduled tasks                                                | Task lookup: Yes, Registration: No            |
| `CoreComponentBase`        | Abstract class     | Base for non-schedulable core components (scheduler, filesystem)                        | Queries: Yes                                  |
| `SimModelBase`             | Abstract class     | Base for simulation models (SW_MODEL)                                                   | Runtime: Yes                                  |
| `SwModelBase`              | Abstract class     | Alias for SimModelBase (SW_MODEL type)                                                  | Runtime: Yes                                  |
| `HwModelBase`              | Abstract class     | Base for hardware emulation models (HW_MODEL)                                           | Runtime: Yes                                  |
| `SupportComponentBase`     | Abstract class     | Base for runtime support services                                                       | Runtime: Yes                                  |
| `DriverBase`               | Abstract class     | Base for real hardware interfaces (DRIVER)                                              | Runtime: Yes                                  |
| `McuComponentBase`        | Abstract class     | Minimal implementation for bare-metal MCUs                                              | Queries: Yes, Lifecycle: No                   |
| `PackedTprm`               | Struct             | TPRM file reader (archive extraction, entry lookup)                                     | No (file I/O)                                 |
| `ComponentRegistry`        | Class              | Component lookup by fullUid, componentId, or name                                       | Yes (read-only queries)                       |
| `SystemComponentTlm`       | Struct             | Telemetry snapshot for component state export                                           | Yes                                           |
| `DataCategory`             | Enum               | Semantic categories for data blocks (STATIC_PARAM, TUNABLE_PARAM, STATE, INPUT, OUTPUT) | Yes                                           |
| `ModelData`                | Template class     | Typed container for model data with category-based access control                       | Yes                                           |
| `DataTarget`               | Struct             | Runtime byte-range addressing for registered data blocks                                | Yes                                           |

| Question                                          | Answer                                       |
| ------------------------------------------------- | -------------------------------------------- |
| What is the universal component interface?        | `IComponent`                                 |
| How do I create a schedulable model?              | Inherit `SimModelBase` or `HwModelBase`      |
| How do I add tunable parameters with hot-reload?  | `SystemComponent<TParams>`                   |
| How do I get RT-safe parameter access?            | `activeParams()` (atomic pointer load, ~9ns) |
| How do I build for bare-metal MCUs?               | Inherit `McuComponentBase`                  |
| What status codes can init/load return?           | `Status` enum (SUCCESS through EOE marker)   |
| How do I classify data blocks semantically?       | `DataCategory` enum                          |
| How do I wrap typed data with category semantics? | `ModelData<T, Category>`                     |
| How do I address a byte range in registered data? | `DataTarget` struct                          |

---

## 2. When to Use

| Scenario                                       | Use This Library?                              |
| ---------------------------------------------- | ---------------------------------------------- |
| Create a schedulable model for the executive   | Yes -- inherit `SimModelBase` or `HwModelBase` |
| Create a core infrastructure component         | Yes -- inherit `CoreComponentBase`             |
| Create a hardware driver component             | Yes -- inherit `DriverBase`                    |
| Add tunable parameters with hot-reload         | Yes -- `SystemComponent<TParams>`              |
| Need component identity (componentId, fullUid) | Yes -- `IComponent` interface                  |
| Build for bare-metal MCU with McuExecutive    | Yes -- `McuComponentBase`                     |
| Task scheduling configuration (freq, priority) | No -- scheduler owns config                    |
| Component-to-component messaging               | No -- use `IInternalBus` (separate library)    |

**Design intent:** Four-tier component hierarchy. `IComponent` is the universal contract (no heavy deps). `ComponentCore` adds the concrete identity / lifecycle / registration state shared by every implementation, with no platform deps. `SystemComponentBase` (POSIX tier) extends ComponentCore with TPRM, logging, data descriptors, and internal bus access. `McuComponentBase` (MCU tier) extends ComponentCore with static-allocation contracts. The shared ComponentCore lets `ComponentRegistry` accept either tier, so MCU components register through the same call path POSIX components use. A/B parameter staging enables lock-free RT parameter access with zero-allocation hot-reload.

---

## 3. Performance

### Parameter Access and Lifecycle

| Operation                           | Param Size | Median (us) | Calls/s | CV%   |
| ----------------------------------- | ---------- | ----------- | ------- | ----- |
| `activeParams()`                    | 24B        | 0.009       | 109.9M  | 4.1%  |
| `activeParams()`                    | 88B        | 0.009       | 107.5M  | 1.4%  |
| `activeParams()`                    | 320B       | 0.010       | 101.0M  | 3.7%  |
| `load()`                            | 24B        | 0.014       | 70.4M   | 6.8%  |
| `load()`                            | 88B        | 0.015       | 65.8M   | 1.5%  |
| `load()`                            | 320B       | 0.017       | 59.2M   | 1.5%  |
| `load()` + `apply()`                | 24B        | 0.028       | 36.1M   | 1.0%  |
| `rollback()` + `load()` + `apply()` | 24B        | 0.038       | 26.2M   | 32.7% |
| Full init cycle                     | 24B        | 0.065       | 15.5M   | 1.3%  |

### Parameter Size Scaling

| Param Size | `activeParams()` Median (ns) | Calls/s |
| ---------- | ---------------------------- | ------- |
| 24B        | 9.1                          | 109.9M  |
| 88B        | 9.3                          | 107.5M  |
| 320B       | 9.9                          | 101.0M  |

`activeParams()` is O(1) regardless of parameter struct size (single atomic pointer load).

### Profiler Analysis (gperftools)

**RollbackSmall (36 samples):**

| Function                    | Self-Time | Type                          |
| --------------------------- | --------- | ----------------------------- |
| `SystemComponent::apply`    | 27.8%     | CPU-bound (A/B bank swap)     |
| `SystemComponent::rollback` | 19.4%     | CPU-bound (bank restore)      |
| `SystemComponent::load`     | 13.9%     | CPU-bound (memcpy + validate) |
| `std::atomic::load`         | 11.1%     | CPU-bound (pointer read)      |
| `std::atomic::store`        | 8.3%      | CPU-bound (pointer write)     |

**FullInitCycle (68 samples):**

| Function                                   | Self-Time | Type                          |
| ------------------------------------------ | --------- | ----------------------------- |
| `SystemComponentBase::SystemComponentBase` | 17.6%     | CPU-bound (constructor)       |
| `SystemComponent::load`                    | 14.7%     | CPU-bound (memcpy + validate) |
| `SystemComponent::preInit`                 | 11.8%     | CPU-bound (staged -> active)  |
| `SystemComponent::SystemComponent`         | 7.4%      | CPU-bound (base init)         |

### Memory Footprint

| Component             | Stack                                | Heap                    |
| --------------------- | ------------------------------------ | ----------------------- |
| `IComponent`          | 8B (vtable)                          | 0                       |
| `McuComponentBase`   | ~24B (vtable + state)                | 0                       |
| `SystemComponentBase` | ~120B (vtable + state + descriptors) | Log pointer (shared)    |
| `SystemComponent<T>`  | ~120B + 2 \* sizeof(T) (A/B banks)   | 0 (banks inline)        |
| `PackedTprm`          | ~32B                                 | File buffer (transient) |

---

## 4. Design Principles

- **Three-tier hierarchy** -- `IComponent` (universal), `SystemComponentBase` (Linux/RTOS), `McuComponentBase` (MCU)
- **Template method pattern** -- `init()` is non-virtual and calls `preInit()` then `doInit()`
- **Zero-allocation parameter staging** -- A/B banks are inline members, `apply()` is an atomic pointer swap
- **RT-safe queries** -- `activeParams()`, `status()`, `fullUid()`, `label()` are O(1) with no allocation
- **Configuration requirement** -- `init()` requires `isConfigured()` == true (prevents uninitialized operation)
- **Extensible status codes** -- `EOE_SYSTEM_COMPONENT` marker allows derived components to add codes without collision
- **Component type classification** -- `ComponentType` enum drives scheduler, registry, and logging behavior
- **Static-allocation lite path** -- `McuComponentBase` uses no heap, no `std::filesystem`, no `std::thread`
- **Data descriptor registration** -- Components declare data blocks during `doInit()` for registry integration

---

## 5. API Reference

### IComponent (base/)

```cpp
class IComponent {
public:
  /// @note RT-safe: O(1).
  [[nodiscard]] virtual uint16_t componentId() const noexcept = 0;
  [[nodiscard]] virtual const char* componentName() const noexcept = 0;
  [[nodiscard]] virtual ComponentType componentType() const noexcept = 0;
  [[nodiscard]] virtual const char* label() const noexcept = 0;

  /// @note NOT RT-safe: Boot-time only.
  [[nodiscard]] virtual uint8_t init() noexcept = 0;
  virtual void reset() noexcept = 0;

  /// @note RT-safe: O(1).
  [[nodiscard]] virtual uint8_t status() const noexcept = 0;
  [[nodiscard]] virtual bool isInitialized() const noexcept = 0;
  [[nodiscard]] virtual uint32_t fullUid() const noexcept = 0;
  [[nodiscard]] virtual uint8_t instanceIndex() const noexcept = 0;
  [[nodiscard]] virtual bool isRegistered() const noexcept = 0;
};
```

### SystemComponentBase (apex/)

```cpp
class SystemComponentBase : public IComponent {
public:
  /// @note NOT RT-safe: Boot-time initialization (template method).
  [[nodiscard]] uint8_t init() noexcept override;

  /// @note NOT RT-safe: May deallocate.
  void reset() noexcept override;

  /// @note RT-safe: Atomic load / simple read.
  [[nodiscard]] uint8_t status() const noexcept override;
  [[nodiscard]] bool isInitialized() const noexcept override;
  [[nodiscard]] bool isConfigured() const noexcept override;
  [[nodiscard]] const char* lastError() const noexcept;

  /// @note NOT RT-safe: Registration (called by executive).
  void setInstanceIndex(uint8_t idx) noexcept;

  /// @note NOT RT-safe: Called once after bus is wired, before runtime starts.
  virtual void onBusReady() noexcept {}

protected:
  virtual void preInit() noexcept {}
  virtual uint8_t doInit() noexcept = 0;
  virtual void doReset() noexcept {}
  void setConfigured(bool v) noexcept;
};
```

### SystemComponent<TParams> (apex/)

```cpp
template <typename TParams>
class SystemComponent : public SystemComponentBase {
public:
  /// @note NOT RT-safe: Memcpy + validation.
  [[nodiscard]] uint8_t load(const std::filesystem::path& path) noexcept;
  [[nodiscard]] uint8_t load(const TParams& params) noexcept;

  /// @note NOT RT-safe: Atomic pointer swap (control-plane).
  [[nodiscard]] uint8_t apply() noexcept;
  [[nodiscard]] uint8_t rollback() noexcept;

  /// @note RT-safe: Single atomic acquire load (~9ns).
  [[nodiscard]] const TParams& activeParams() const noexcept;

  /// @note RT-safe: Simple pointer read.
  [[nodiscard]] const TParams& stagedParams() const noexcept;
  [[nodiscard]] bool canRollback() const noexcept;
  [[nodiscard]] uint64_t activeGeneration() const noexcept;

protected:
  virtual bool validateParams(const TParams&) const noexcept = 0;
};
```

### McuComponentBase (lite/)

```cpp
class McuComponentBase : public IComponent {
public:
  /// @note NOT RT-safe: Calls doInit() hook.
  [[nodiscard]] uint8_t init() noexcept override;

  /// @note NOT RT-safe: Calls doReset() hook.
  void reset() noexcept override;

  /// @note RT-safe: O(1).
  [[nodiscard]] uint8_t status() const noexcept override;
  [[nodiscard]] bool isInitialized() const noexcept override;
  [[nodiscard]] const char* lastError() const noexcept;

  /// @note Called by McuExecutive during registration.
  void setInstanceIndex(uint8_t instanceIdx) noexcept;

protected:
  [[nodiscard]] virtual uint8_t doInit() noexcept = 0;
  virtual void doReset() noexcept {}
};
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
  EOE_SYSTEM_COMPONENT  // Extension marker
};
```

---

## 6. Usage Examples

### Simple Component (No Parameters)

```cpp
#include "src/system/core/infrastructure/system_component/posix/inc/CoreComponentBase.hpp"

class MyComponent : public system_core::system_component::CoreComponentBase {
public:
  MyComponent() { setConfigured(true); }

  uint16_t componentId() const noexcept override { return 50; }
  const char* componentName() const noexcept override { return "MY_COMPONENT"; }
  const char* label() const noexcept override { return "MY_COMPONENT"; }

protected:
  uint8_t doInit() noexcept override {
    // Initialize resources
    return 0;
  }
};
```

### Component with Tunable Parameters

```cpp
#include "src/system/core/infrastructure/system_component/posix/inc/SystemComponent.hpp"

struct MyParams {
  uint16_t frequency{100};
  uint8_t mode{0};
};

class MyConfigurable : public system_core::system_component::SystemComponent<MyParams> {
public:
  uint16_t componentId() const noexcept override { return 102; }
  const char* componentName() const noexcept override { return "MY_CONFIGURABLE"; }
  const char* label() const noexcept override { return "MY_CONFIGURABLE"; }
  ComponentType componentType() const noexcept override { return ComponentType::SW_MODEL; }

protected:
  bool validateParams(const MyParams& p) const noexcept override {
    return p.frequency > 0 && p.frequency <= 1000;
  }

  uint8_t doInit() noexcept override {
    auto& p = activeParams();  // RT-safe (~9ns)
    configureHardware(p.frequency, p.mode);
    return 0;
  }
};
```

### Lifecycle: Load, Init, Hot-Reload

```cpp
MyConfigurable comp;

// 1. Load parameters (from file or struct)
comp.load(MyParams{100, 0});  // or: comp.load("config.tprm");

// 2. Initialize
comp.init();  // preInit() applies staged->active, then calls doInit()

// 3. Hot-reload new parameters at runtime
comp.load(MyParams{200, 1});
comp.apply();  // Swaps to new params without re-init

// 4. Rollback if needed
if (comp.canRollback()) {
  comp.rollback();  // Restores previous params
}
```

### Bare-Metal Component (McuComponentBase)

```cpp
#include "src/system/core/infrastructure/system_component/mcu/inc/McuComponentBase.hpp"

class McuSensor : public system_core::system_component::mcu::McuComponentBase {
public:
  uint16_t componentId() const noexcept override { return 200; }
  const char* componentName() const noexcept override { return "MCU_SENSOR"; }
  ComponentType componentType() const noexcept override { return ComponentType::DRIVER; }
  const char* label() const noexcept override { return "MCU_SENSOR"; }

protected:
  uint8_t doInit() noexcept override {
    // Configure ADC registers, etc.
    return 0;
  }
};
```

---

## 7. Testing

### Test Organization

| Directory    | Type              | Tests | Runs with `make test` |
| ------------ | ----------------- | ----- | --------------------- |
| `base/utst/` | Unit tests        | 15    | Yes                   |
| `apex/utst/` | Unit tests        | 49    | Yes                   |
| `lite/utst/` | Unit tests        | 10    | Yes                   |
| `apex/ptst/` | Performance tests | 10    | No (manual)           |

### Test Requirements

- All tests are platform-agnostic (no hardware dependencies)
- Tests verify lifecycle transitions, status codes, A/B staging, rollback
- Tests verify IComponent interface contract across implementations
- Tests verify PackedTprm archive extraction and entry lookup
- Tests verify McuComponentBase lifecycle and registration

---

## 8. See Also

- `src/system/core/infrastructure/schedulable/` -- SchedulableTask used by SchedulableComponentBase
- `src/system/core/infrastructure/logs/` -- SystemLog used by components for diagnostics
- `src/utilities/data_proxy/` -- ByteMaskProxy, EndiannessProxy for data transformation
- `src/utilities/helpers/` -- Files utility for TPRM binary loading
