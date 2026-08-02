# System Component Library

Component base classes and lifecycle management for the Apex executive framework. Four directory tiers organize the hierarchy:

| Tier     | Contents                                                                       | Instantiable?             |
| -------- | ------------------------------------------------------------------------------ | ------------------------- |
| `base/`  | Pure virtual `IComponent` interface                                            | No (pure virtual)         |
| `core/`  | `ComponentCore` shared concrete base (identity, lifecycle, registration state) | No (still abstract)       |
| `posix/` | `SystemComponentBase` and POSIX-tier specializations                           | Yes (full POSIX features) |
| `mcu/`   | `McuComponentBase` for bare-metal targets                                      | Yes (static allocation)   |

**Namespace:** `system_core::system_component` (POSIX), `system_core::system_component::mcu` (MCU)
**Libraries:** `system_component_base` (INTERFACE), `system_component_core` (INTERFACE), `system_core_system_component` (SHARED, POSIX), `system_component_mcu` (INTERFACE)
**Platform:** Cross-platform (`posix/`: Linux/RTOS, `mcu/`: bare-metal)
**C++ Standard:** C++23

---

## 1. Quick Reference

| Component                  | Type               | Purpose                                                                                 | RT-Safe                                 |
| -------------------------- | ------------------ | --------------------------------------------------------------------------------------- | --------------------------------------- |
| `IComponent`               | Abstract interface | Minimal pure interface (identity, lifecycle, status)                                    | Queries: Yes, Lifecycle: No             |
| `ComponentCore`            | Abstract class     | Shared concrete base: identity, lifecycle, registration state (no platform deps)        | Queries: Yes, Lifecycle: No             |
| `ComponentType`            | Enum               | Component classification (EXECUTIVE, CORE, SW_MODEL, HW_MODEL, SUPPORT, DRIVER)         | Yes                                     |
| `Status`                   | Enum               | Typed status codes with extension marker                                                | Yes                                     |
| `SystemComponentBase`      | Abstract class     | POSIX-tier base on ComponentCore (TPRM, internal bus, logging, data descriptors)        | Queries: Yes, Lifecycle: No             |
| `ParamBank<T>`             | Template class     | A/B parameter staging owned as a member (file loading, hot-reload, rollback)            | `active()`: Yes, `load()`/`apply()`: No |
| `SchedulableComponentBase` | Abstract class     | Base for components with scheduled tasks                                                | Task lookup: Yes, Registration: No      |
| `CoreComponentBase`        | Abstract class     | Base for non-schedulable core components (scheduler, filesystem)                        | Queries: Yes                            |
| `SwModelBase`              | Abstract class     | Base for software/environment simulation models (SW_MODEL)                              | Runtime: Yes                            |
| `HwModelBase`              | Abstract class     | Base for hardware emulation models (HW_MODEL)                                           | Runtime: Yes                            |
| `SupportComponentBase`     | Abstract class     | Base for runtime support services                                                       | Runtime: Yes                            |
| `DriverBase`               | Abstract class     | Base for real hardware interfaces (DRIVER)                                              | Runtime: Yes                            |
| `McuComponentBase`         | Abstract class     | Minimal implementation for bare-metal MCUs                                              | Queries: Yes, Lifecycle: No             |
| `PackedTprm`               | Struct             | TPRM file reader (archive extraction, entry lookup)                                     | No (file I/O)                           |
| `TprmPayload`              | Functions          | v3 payload prelude verification (magic/version/size/fullUid/CRC, distinct rejects)      | No (file I/O)                           |
| `ComponentRegistry`        | Class              | Component lookup by fullUid, componentId, or name                                       | Yes (read-only queries)                 |
| `SystemComponentTlm`       | Struct             | Telemetry snapshot for component state export                                           | Yes                                     |
| `DataCategory`             | Enum               | Semantic categories for data blocks (STATIC_PARAM, TUNABLE_PARAM, STATE, INPUT, OUTPUT) | Yes                                     |
| `ModelData`                | Template class     | Typed container for model data with category-based access control                       | Yes                                     |
| `DataTarget`               | Struct             | Runtime byte-range addressing for registered data blocks                                | Yes                                     |

| Question                                          | Answer                                     |
| ------------------------------------------------- | ------------------------------------------ |
| What is the universal component interface?        | `IComponent`                               |
| How do I create a schedulable model?              | Inherit `SwModelBase` or `HwModelBase`     |
| How do I add tunable parameters with hot-reload?  | Own a `ParamBank<TParams>` member          |
| How do I get RT-safe parameter access?            | `bank.active()` (seqlock-validated copy)   |
| How do I build for bare-metal MCUs?               | Inherit `McuComponentBase`                 |
| What status codes can init/load return?           | `Status` enum (SUCCESS through EOE marker) |
| How do I classify data blocks semantically?       | `DataCategory` enum                        |
| How do I wrap typed data with category semantics? | `ModelData<T, Category>`                   |
| How do I address a byte range in registered data? | `DataTarget` struct                        |

---

## 2. When to Use

| Scenario                                       | Use This Library?                             |
| ---------------------------------------------- | --------------------------------------------- |
| Create a schedulable model for the executive   | Yes -- inherit `SwModelBase` or `HwModelBase` |
| Create a core infrastructure component         | Yes -- inherit `CoreComponentBase`            |
| Create a hardware driver component             | Yes -- inherit `DriverBase`                   |
| Add tunable parameters with hot-reload         | Yes -- own a `ParamBank<TParams>` member      |
| Need component identity (componentId, fullUid) | Yes -- `IComponent` interface                 |
| Build for bare-metal MCU with McuExecutive     | Yes -- `McuComponentBase`                     |
| Task scheduling configuration (freq, priority) | No -- scheduler owns config                   |
| Component-to-component messaging               | No -- use `IInternalBus` (separate library)   |

**Design intent:** Four-tier component hierarchy. `IComponent` is the universal contract (no heavy deps). `ComponentCore` adds the concrete identity / lifecycle / registration state shared by every implementation, with no platform deps. `SystemComponentBase` (POSIX tier) extends ComponentCore with TPRM, logging, data descriptors, and internal bus access. `McuComponentBase` (MCU tier) extends ComponentCore with static-allocation contracts. The shared ComponentCore lets `ComponentRegistry` accept either tier, so MCU components register through the same call path POSIX components use. A/B parameter staging (`ParamBank<TParams>`, owned as a member by any component -- schedulable included) enables lock-free RT parameter access with zero-allocation hot-reload.

---

## 3. Performance

### Parameter Access and Lifecycle

| Operation                   | Param Size | Median (us) | Calls/s | CV%   |
| --------------------------- | ---------- | ----------- | ------- | ----- |
| `active()` (validated copy) | 24B        | 0.033       | 30.1M   | 0.7%  |
| `active()` (validated copy) | 88B        | 0.056       | 17.7M   | 12.1% |
| `active()` (validated copy) | 320B       | 0.143       | 7.0M    | 1.7%  |
| `load()`                    | 24B        | 0.033       | 29.9M   | 3.5%  |
| `load()`                    | 88B        | 0.057       | 17.6M   | 0.8%  |
| `load()`                    | 320B       | 0.134       | 7.5M    | 4.0%  |
| `apply()`                   | 24B        | 0.043       | 23.1M   | 1.5%  |
| `rollback()`                | 24B        | 0.052       | 19.4M   | 0.4%  |
| Full publish cycle          | 24B        | 0.099       | 10.1M   | 1.1%  |

Full publish cycle = construct + `load()` + `publishInitial()` + `active()`
read on a fresh bank. Reads return a seqlock-validated copy, so their
cost scales with sizeof(TParams) -- the price of a read that can never
tear, even for a reader preempted mid-copy across a publish. Under
maximum publish contention `active()` reads 68 ns at 24B (retry-bounded,
never a wait). The torn-read detector (3 reader threads, 200k publishes)
observes zero inconsistent sets, including under single-CPU-pinned
ThreadSanitizer forcing.

### Parameter Size Scaling

| Param Size | `active()` Median (ns) | Calls/s |
| ---------- | ---------------------- | ------- |
| 24B        | 8.3                    | 121.2M  |
| 88B        | 8.5                    | 118.3M  |
| 320B       | 8.4                    | 119.0M  |

`active()` is O(1) regardless of parameter struct size (single atomic pointer load).

### Memory Footprint

| Component             | Stack                                      | Heap                    |
| --------------------- | ------------------------------------------ | ----------------------- |
| `IComponent`          | 8B (vtable)                                | 0                       |
| `McuComponentBase`    | ~24B (vtable + state)                      | 0                       |
| `SystemComponentBase` | ~120B (vtable + state + descriptors)       | Log pointer (shared)    |
| `ParamBank<T>`        | 2 \* sizeof(T) word-atomic banks + seqlock | 0 (banks inline)        |
| `PackedTprm`          | ~32B                                       | File buffer (transient) |

---

## 4. Design Principles

- **Four-tier layout** -- `IComponent` (universal contract), `ComponentCore` (shared concrete state), `SystemComponentBase` (Linux/RTOS), `McuComponentBase` (MCU)
- **Template method pattern** -- `init()` is non-virtual and calls `preInit()` then `doInit()`
- **Zero-allocation parameter staging** -- ParamBank's A/B banks are inline storage, `apply()` is an atomic pointer swap; the bank is a composable member, so schedulable components stage parameters too
- **RT-safe queries** -- `bank.active()`, `status()`, `fullUid()`, `label()` are O(1) with no allocation
- **Configuration requirement** -- `init()` requires `isConfigured()` == true (prevents uninitialized operation)
- **Extensible status codes** -- `EOE_SYSTEM_COMPONENT` marker allows derived components to add codes without collision
- **Component type classification** -- `ComponentType` enum drives scheduler, registry, and logging behavior
- **Static-allocation MCU path** -- `McuComponentBase` uses no heap, no `std::filesystem`, no `std::thread`
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

### SystemComponentBase (posix/)

```cpp
class SystemComponentBase : public ComponentCore {
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

### ParamBank<TParams> (posix/)

```cpp
template <typename TParams>
class ParamBank {
public:
  /// @note NOT RT-safe: Memcpy + caller-supplied validation.
  [[nodiscard]] Status load(const TParams& params, TValidator&& validate) noexcept;
  [[nodiscard]] Status load(const std::filesystem::path& path, TValidator&& validate) noexcept;

  /// @note NOT RT-safe: Atomic pointer swap (control-plane).
  [[nodiscard]] Status publishInitial() noexcept;  // first publish, before RT phase
  [[nodiscard]] Status apply() noexcept;           // hot-reload
  [[nodiscard]] Status rollback() noexcept;        // one level of history

  /// @note RT-safe: seqlock-validated copy; never tears, retries only
  /// during an in-flight publish.
  [[nodiscard]] TParams active() const noexcept;

  /// @note RT-safe: Simple reads.
  [[nodiscard]] const TParams& staged() const noexcept;
  [[nodiscard]] bool canRollback() const noexcept;
  [[nodiscard]] bool isLoaded() const noexcept;
  [[nodiscard]] uint64_t activeGeneration() const noexcept;
};
```

The bank is a member, not a base class: any component -- schedulable
included -- owns one per parameter set and passes its own validator to
load(). A rejected load() cannot be published (apply() refuses), and
staging over the rollback source forfeits the rollback history (two banks
cannot hold three states).

### McuComponentBase (mcu/)

```cpp
class McuComponentBase : public ComponentCore {
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
#include "src/system/core/infrastructure/system_component/posix/inc/ParamBank.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

struct MyParams {
  uint16_t frequency{100};
  uint8_t mode{0};
};

class MyModel : public system_core::system_component::SwModelBase {
public:
  uint16_t componentId() const noexcept override { return 102; }
  const char* componentName() const noexcept override { return "MY_MODEL"; }
  const char* label() const noexcept override { return "MY_MODEL"; }

  uint8_t loadParams(const MyParams& p) noexcept {
    return static_cast<uint8_t>(bank_.load(p, [](const MyParams& m) noexcept {
      return m.frequency > 0 && m.frequency <= 1000;
    }));
  }

protected:
  uint8_t doInit() noexcept override {
    if (bank_.publishInitial() != Status::SUCCESS) {
      return static_cast<uint8_t>(Status::ERROR_NOT_CONFIGURED);
    }
    const auto p = bank_.active();  // RT-safe validated copy
    configureModel(p.frequency, p.mode);
    return 0;
  }

private:
  system_core::system_component::ParamBank<MyParams> bank_;
};
```

The bank composes with any base -- the example is a schedulable SwModel,
which the retired inheritance-based staging could never be.

### Lifecycle: Load, Init, Hot-Reload

```cpp
MyModel comp;

// 1. Stage parameters (from file or struct); component validates
comp.loadParams(MyParams{100, 0});

// 2. Initialize: doInit() publishes the staged set, then configures
comp.init();

// 3. Hot-reload new parameters at runtime (bank-owning component decides
//    when; a rejected load can never be published)
comp.loadParams(MyParams{200, 1});
comp.applyParams();  // bank_.apply(): atomic swap, no re-init

// 4. Rollback if needed (one level; staging again forfeits it)
if (comp.canRollbackParams()) {
  comp.rollbackParams();
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

| Directory     | Type              | Tests | Runs with `make test` |
| ------------- | ----------------- | ----- | --------------------- |
| `base/utst/`  | Unit tests        | 15    | Yes                   |
| `core/utst/`  | Unit tests        | 17    | Yes                   |
| `posix/utst/` | Unit tests        | 114   | Yes                   |
| `mcu/utst/`   | Unit tests        | 10    | Yes                   |
| `posix/ptst/` | Performance tests | 11    | No (manual)           |

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
- `src/utilities/checksums/crc/` -- CRC-32/ISO-HDLC for v3 payload verification
