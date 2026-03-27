# Data Infrastructure

**Namespace:** `system_core::data`
**Library:** `system_core_data`
**Platform:** Cross-platform
**C++ Standard:** C++17

Static-sized data infrastructure for real-time models and system components.
Two layers: data proxy infrastructure (typed containers, endianness, fault
injection) and runtime operations (watchpoints, actions, sequences, event
notifications, and the ActionInterface orchestrator).

---

## 1. Quick Reference

**Data Proxy Infrastructure:**

| Header                    | Purpose                                            | RT-Safe |
| ------------------------- | -------------------------------------------------- | ------- |
| `DataCategory.hpp`        | Semantic categories for data blocks                | Yes     |
| `ModelData.hpp`           | Typed container with category-based access control | Yes     |
| `FaultInjectionProxy.hpp` | AND/XOR mask queue for fault injection             | Yes     |
| `EndiannessProxy.hpp`     | Byte-swap support for cross-platform data          | Yes     |
| `MasterDataProxy.hpp`     | Orchestrates endianness + fault transformations    | Yes     |

**Runtime Operations:**

| Header                  | Purpose                                              | RT-Safe |
| ----------------------- | ---------------------------------------------------- | ------- |
| `DataTarget.hpp`        | Byte-range addressing into registered data blocks    | Yes     |
| `DataWatchpoint.hpp`    | Conditional data triggers with type-aware comparison | Yes     |
| `DataAction.hpp`        | Byte-level AND/XOR masks, commands, arm control      | Yes     |
| `DataSequence.hpp`      | Ordered action lists with RTS/ATS timing             | Yes     |
| `EventNotification.hpp` | Immediate callbacks on watchpoint events             | Yes     |
| `ActionInterface.hpp`   | Orchestrator: evaluate-dispatch-execute pipeline     | Yes     |

| Question                                                | Answer                                     |
| ------------------------------------------------------- | ------------------------------------------ |
| How do I create typed data blocks with categories?      | `ModelData<T, Category>`                   |
| How do I byte-swap structs for cross-platform data?     | `EndiannessProxy` or `MasterDataProxy`     |
| How do I inject faults at the byte level?               | `FaultInjectionProxy` or `MasterDataProxy` |
| How do I trigger actions when data crosses a threshold? | `DataWatchpoint` + `ActionInterface`       |
| How do I run timed action sequences?                    | `DataSequence` (RTS or ATS)                |
| How do I orchestrate per-cycle watchpoint evaluation?   | `ActionInterface::processCycle()`          |
| How do I address a specific byte range in a data block? | `DataTarget`                               |

### Quick Example

```cpp
#include "src/system/core/data/inc/MasterDataProxy.hpp"

namespace data = system_core::data;

struct Packet {
  std::uint32_t timestamp;
  std::uint16_t sensorId;
  std::uint16_t value;
};

// No transformations (passthrough)
Packet input{0x12345678, 0x0001, 0x00FF};
data::MasterDataProxy<Packet, false, false> proxy(&input);
proxy.resolve();
// proxy.output() points to input (zero-copy)

// With endianness swap
data::MasterDataProxy<Packet, true, false> swapProxy(&input);
swapProxy.resolve();
// swapProxy.output()->timestamp == 0x78563412
```

---

## 2. When to Use

| Scenario                                                | Use This Library?                                |
| ------------------------------------------------------- | ------------------------------------------------ |
| Typed data blocks for model I/O with category semantics | Yes -- `ModelData<T, Category>`                  |
| Byte-swap for cross-platform telemetry                  | Yes -- `EndiannessProxy` / `MasterDataProxy`     |
| Runtime fault injection (bit-flip, zero, mask)          | Yes -- `FaultInjectionProxy` / `MasterDataProxy` |
| Conditional data triggers (value > threshold)           | Yes -- `DataWatchpoint`                          |
| Runtime action sequences (RTS/ATS)                      | Yes -- `DataSequence`                            |
| Per-cycle watchpoint-action-sequence orchestration      | Yes -- `ActionInterface`                         |
| Commanding components via opcode dispatch               | Yes -- `DataAction` (COMMAND type)               |
| TPRM parameter loading and hot-reload                   | No -- use `SystemComponent<T>`                   |
| Thread pool or task scheduling                          | No -- use `schedulable` / `scheduler`            |

**Design intent:** All-RT-safe data infrastructure. No dynamic allocation, all operations bounded O(1) or O(sizeof(T)), all functions noexcept. ActionInterface processes the full watchpoint-action-sequence pipeline in a single `processCycle()` call per scheduler frame.

---

## 3. Performance

### ActionInterface Pipeline

| Operation                                         | Median (us) | Calls/s | CV%   |
| ------------------------------------------------- | ----------- | ------- | ----- |
| Empty baseline (no armed watchpoints)             | 0.044       | 22.7M   | 11.6% |
| Watchpoints steady-state (8 armed, no fires)      | 0.203       | 4.9M    | 24.7% |
| Watchpoints firing (8 armed, all fire)            | 0.115       | 8.7M    | 0.7%  |
| Sequences ticking (4 running)                     | 0.050       | 20.2M   | 14.0% |
| Action processing (16 queued)                     | 0.467       | 2.1M    | 2.0%  |
| Full pipeline (watchpoints + sequences + actions) | 0.380       | 2.6M    | 1.1%  |

### Isolated Sub-Pipelines

| Sub-Pipeline                    | Median (us) | Calls/s | CV%  |
| ------------------------------- | ----------- | ------- | ---- |
| `evaluateWatchpoints` (8 armed) | 0.171       | 5.9M    | 0.8% |
| `tickSequences` (4 running)     | 0.028       | 36.1M   | 1.1% |
| `processActions` (16 queued)    | 0.266       | 3.8M    | 1.9% |

### Data Proxy Operations

| Operation                                  | Size | Median (us) | Calls/s | CV%   |
| ------------------------------------------ | ---- | ----------- | ------- | ----- |
| FaultInjection apply                       | 32B  | 0.026       | 38.5M   | 0.5%  |
| FaultInjection mask scaling                | 4B   | 0.012       | 81.3M   | 1.7%  |
| FaultInjection mask scaling                | 8B   | 0.021       | 48.5M   | 3.7%  |
| FaultInjection mask scaling                | 16B  | 0.012       | 81.3M   | 1.7%  |
| FaultInjection mask scaling                | 24B  | 0.015       | 64.5M   | 1.6%  |
| FaultInjection mask scaling                | 32B  | 0.021       | 46.5M   | 0.6%  |
| Endianness scalar swap (u16)               | 2B   | 0.009       | 116.3M  | 5.6%  |
| Endianness scalar swap (u32)               | 4B   | 0.009       | 114.9M  | 6.2%  |
| Endianness scalar swap (u64)               | 8B   | 0.011       | 92.6M   | 1.5%  |
| Endianness struct swap (32B)               | 32B  | 0.026       | 38.0M   | 23.8% |
| Endianness struct swap (64B)               | 64B  | 0.053       | 18.7M   | 4.4%  |
| Endianness struct swap (128B)              | 128B | 0.110       | 9.1M    | 9.6%  |
| MasterDataProxy passthrough                | 32B  | 0.015       | 66.2M   | 2.4%  |
| MasterDataProxy swap                       | 32B  | 0.023       | 43.5M   | 3.3%  |
| MasterDataProxy fault overhead             | 32B  | 0.034       | 29.2M   | 1.2%  |
| MasterDataProxy full pipeline (swap+fault) | 32B  | 0.042       | 23.9M   | 1.3%  |
| MasterDataProxy full pipeline (swap+fault) | 64B  | 0.072       | 13.9M   | 15.6% |
| MasterDataProxy full pipeline (swap+fault) | 128B | 0.126       | 8.0M    | 3.1%  |

### Profiler Analysis (gperftools)

**ActionInterface FullPipeline (425 samples):**

| Function               | Self-Time | Type                                |
| ---------------------- | --------- | ----------------------------------- |
| `applyDataWrite`       | 13.9%     | CPU-bound (mask application)        |
| `detail::compareTyped` | 8.9%      | CPU-bound (watchpoint comparison)   |
| `evaluateGroup`        | 8.2%      | CPU-bound (AND logic)               |
| `resolveTarget`        | 7.3%      | CPU-bound (delegate + bounds check) |
| `evaluateEdge`         | 6.8%      | CPU-bound (edge detection)          |
| `evaluatePredicate`    | 6.8%      | CPU-bound (typed comparison)        |
| `processActions`       | 6.8%      | CPU-bound (queue iteration)         |

**MasterDataProxy FullPipeline (130 samples):**

| Function                     | Self-Time | Type                                 |
| ---------------------------- | --------- | ------------------------------------ |
| `endianSwap` (ADL)           | 21.5%     | CPU-bound (field-by-field byte swap) |
| `byteswapIeee`               | 16.2%     | CPU-bound (IEEE float swap)          |
| `byteswap`                   | 10.0%     | CPU-bound (integer byte swap)        |
| `MasterDataProxy::resolve`   | 10.0%     | CPU-bound (orchestration)            |
| `swapBytes`                  | 8.5%      | CPU-bound (scalar swap)              |
| `FaultInjectionProxy::apply` | 6.2%      | CPU-bound (mask apply)               |

### Memory Footprint

| Component                          | Stack                                     | Heap |
| ---------------------------------- | ----------------------------------------- | ---- |
| `ActionInterface`                  | ~2.4KB (fixed tables)                     | 0    |
| `DataWatchpoint`                   | ~32B                                      | 0    |
| `DataAction`                       | ~80B                                      | 0    |
| `DataSequence`                     | ~680B (8 steps x ~85B)                    | 0    |
| `MasterDataProxy<T, true, true>`   | sizeof(T) + ~160B (overlay + fault proxy) | 0    |
| `MasterDataProxy<T, false, false>` | ~8B (pointer only)                        | 0    |
| `FaultInjectionProxy`              | ~144B (4 masks x 36B)                     | 0    |
| `ModelData<T>`                     | sizeof(T)                                 | 0    |

---

## 4. Design Principles

### RT-Safety

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

All modules in this library are **RT-safe**:

- No dynamic allocation (no `std::vector`, `std::string`, `std::queue`)
- All operations bounded O(1) or O(sizeof(T))
- All functions marked `noexcept`
- Trivially copyable types only

### Static Sizing

All data structures use fixed-size storage:

```cpp
// Circular buffer instead of std::queue
std::array<MaskEntry, FAULT_MAX_MASKS> masks_;

// Fixed-size mask storage
std::array<std::uint8_t, FAULT_MAX_MASK_LEN> andMask;
```

### Zero-Overhead Abstraction

Template parameters enable compile-time feature selection:

```cpp
// No transformations - output() returns input pointer directly
MasterDataProxy<T, false, false> passthrough(&data);

// With swap - internal overlay buffer used
MasterDataProxy<T, true, false> swapping(&data);

// [[no_unique_address]] eliminates storage for disabled features
[[no_unique_address]] std::conditional_t<FaultsEnabled,
    FaultInjectionProxy, std::monostate> fault_;
```

---

## 5. Module Reference

### DataCategory

**Header:** `DataCategory.hpp`
**Purpose:** Semantic categories for model and component data blocks.

#### Key Types

```cpp
enum class DataCategory : std::uint8_t {
  STATIC_PARAM = 0,  ///< Read-only constants (e.g., physical constants)
  TUNABLE_PARAM = 1, ///< Runtime-adjustable parameters (e.g., gains)
  STATE = 2,         ///< Internal model state (e.g., integrator values)
  INPUT = 3,         ///< External data fed to model (e.g., sensor readings)
  OUTPUT = 4         ///< Data produced by model (e.g., computed values)
};
```

#### API

```cpp
[[nodiscard]] const char* toString(DataCategory cat) noexcept;
[[nodiscard]] bool isParam(DataCategory cat) noexcept;
[[nodiscard]] bool isReadOnly(DataCategory cat) noexcept;
[[nodiscard]] bool isModelInput(DataCategory cat) noexcept;
[[nodiscard]] bool isModelOutput(DataCategory cat) noexcept;
```

#### Usage

```cpp
namespace data = system_core::data;

data::DataCategory cat = data::DataCategory::STATIC_PARAM;
if (data::isReadOnly(cat)) {
  // Prevent writes to this data block
}
```

---

### ModelData

**Header:** `ModelData.hpp`
**Purpose:** Typed container associating data with a semantic category.

#### Key Types

```cpp
template <typename T, DataCategory Category>
class ModelData;  ///< Primary template

// Convenience aliases
template <typename T> using StaticParam = ModelData<T, DataCategory::STATIC_PARAM>;
template <typename T> using TunableParam = ModelData<T, DataCategory::TUNABLE_PARAM>;
template <typename T> using State = ModelData<T, DataCategory::STATE>;
template <typename T> using Input = ModelData<T, DataCategory::INPUT>;
template <typename T> using Output = ModelData<T, DataCategory::OUTPUT>;
```

#### API

```cpp
// Construction
ModelData() noexcept;                     ///< Value-initialize
explicit ModelData(const T& v) noexcept;  ///< Initialize with value

// Read access (always available)
[[nodiscard]] const T& get() const noexcept;
[[nodiscard]] const T* operator->() const noexcept;
[[nodiscard]] const T& operator*() const noexcept;
[[nodiscard]] const T* ptr() const noexcept;

// Write access (compile error for STATIC_PARAM)
[[nodiscard]] T& get() noexcept;
[[nodiscard]] T* operator->() noexcept;
void set(const T& v) noexcept;
[[nodiscard]] T* ptr() noexcept;

// Category queries (constexpr)
[[nodiscard]] static constexpr DataCategory category() noexcept;
[[nodiscard]] static constexpr bool isReadOnly() noexcept;
[[nodiscard]] static constexpr bool isParam() noexcept;
[[nodiscard]] static constexpr bool isModelInput() noexcept;
[[nodiscard]] static constexpr bool isModelOutput() noexcept;
[[nodiscard]] static constexpr bool isState() noexcept;
[[nodiscard]] static constexpr std::size_t size() noexcept;
```

#### Usage

```cpp
namespace data = system_core::data;

struct PhysicalConstants {
  double gravity;
  double airDensity;
};

struct ControlGains {
  double kp, ki, kd;
};

// Immutable after construction
data::StaticParam<PhysicalConstants> constants{{9.81, 1.225}};
double g = constants->gravity;  // OK: read access
// constants->gravity = 10.0;   // Compile error: STATIC_PARAM

// Mutable at runtime
data::TunableParam<ControlGains> gains{{1.0, 0.1, 0.01}};
gains->kp = 2.0;  // OK: write access

// Internal model state
data::State<double> position{0.0};
position.set(100.0);  // OK: write access
```

---

### FaultInjectionProxy

**Header:** `FaultInjectionProxy.hpp`
**Purpose:** Static-sized queue of AND/XOR masks for fault injection testing.

#### Constants

```cpp
constexpr std::size_t FAULT_MAX_MASKS = 4;     ///< Max queued masks
constexpr std::size_t FAULT_MAX_MASK_LEN = 32; ///< Max bytes per mask
```

#### Key Types

```cpp
enum class FaultStatus : std::uint8_t {
  SUCCESS = 0,
  ERROR_EMPTY = 1,     ///< No mask queued
  ERROR_PARAM = 2,     ///< Null data or zero size
  ERROR_FULL = 3,      ///< Queue full
  ERROR_TOO_LONG = 4,  ///< Mask exceeds MAX_MASK_LEN
  ERROR_BOUNDS = 5     ///< Mask extends beyond buffer
};
```

#### API

```cpp
// Queue operations
[[nodiscard]] FaultStatus push(index, andMask, xorMask, len) noexcept;
[[nodiscard]] FaultStatus pushZeroMask(index, len) noexcept;  // Force to 0x00
[[nodiscard]] FaultStatus pushHighMask(index, len) noexcept;  // Force to 0xFF
[[nodiscard]] FaultStatus pushFlipMask(index, len) noexcept;  // Invert bits
void pop() noexcept;
void clear() noexcept;

// Query
[[nodiscard]] bool empty() const noexcept;
[[nodiscard]] std::size_t size() const noexcept;

// Application
[[nodiscard]] FaultStatus apply(data, dataSize) noexcept;
[[nodiscard]] FaultStatus applyAndPop(data, dataSize) noexcept;
```

#### Usage

```cpp
namespace data = system_core::data;

data::FaultInjectionProxy proxy;

// Zero first 4 bytes of a packet
proxy.pushZeroMask(0, 4);

std::array<std::uint8_t, 16> data = {0xFF, 0xFF, 0xFF, 0xFF, /*...*/};
auto status = proxy.apply(data.data(), data.size());
// data[0..3] now 0x00
```

---

### EndiannessProxy

**Header:** `EndiannessProxy.hpp`
**Purpose:** Compile-time endianness conversion between input and output buffers.

#### Key Types

```cpp
enum class EndianStatus : std::uint8_t {
  SUCCESS = 0
};
```

#### API

```cpp
// Scalar byte swap
template <typename T>
[[nodiscard]] T swapBytes(T v) noexcept;

// Proxy class
template <typename T, bool SwapRequired>
class EndiannessProxy {
  EndiannessProxy(const T* in, T* out) noexcept;
  EndianStatus resolve() noexcept;
  [[nodiscard]] const T* input() const noexcept;
  [[nodiscard]] T* output() noexcept;
  [[nodiscard]] static constexpr bool swapRequired() noexcept;
};
```

#### Usage - Scalar Types

```cpp
namespace data = system_core::data;

std::uint32_t in = 0x12345678;
std::uint32_t out = 0;

data::EndiannessProxy<std::uint32_t, true> proxy(&in, &out);
proxy.resolve();
// out == 0x78563412
```

#### Usage - Struct Types (ADL)

For struct types, provide a free function `endianSwap` that will be found via ADL:

```cpp
struct MyPacket {
  std::uint32_t field32;
  std::uint16_t field16;
};

// Found via ADL when EndiannessProxy resolves
void endianSwap(const MyPacket& in, MyPacket& out) noexcept {
  out.field32 = system_core::data::swapBytes(in.field32);
  out.field16 = system_core::data::swapBytes(in.field16);
}

MyPacket pktIn{0x12345678, 0xABCD};
MyPacket pktOut{};

data::EndiannessProxy<MyPacket, true> proxy(&pktIn, &pktOut);
proxy.resolve();
// pktOut.field32 == 0x78563412
// pktOut.field16 == 0xCDAB
```

---

### MasterDataProxy

**Header:** `MasterDataProxy.hpp`
**Purpose:** Orchestrates data transformations (endianness, fault injection) into a single interface.

#### Key Types

```cpp
enum class ProxySlot : std::uint8_t {
  MASTER = 0,  ///< Master orchestration status
  ENDIAN = 1,  ///< Endianness proxy status
  FAULT = 2    ///< Fault injection proxy status
};

enum class MasterStatus : std::uint8_t {
  SUCCESS = 0,
  ERROR_PARAM = 1,    ///< Null input pointer
  ERROR_PROXIES = 2   ///< One or more proxies failed
};
```

#### API

```cpp
template <typename T, bool SwapRequired, bool FaultsEnabled>
class MasterDataProxy {
  explicit MasterDataProxy(const T* input) noexcept;

  // Fault injection (only when FaultsEnabled=true)
  void setFaultsEnabled(bool enabled) noexcept;
  [[nodiscard]] bool faultsEnabled() const noexcept;
  [[nodiscard]] FaultStatus pushMask(...) noexcept;
  [[nodiscard]] FaultStatus pushZeroMask(index, len) noexcept;
  [[nodiscard]] FaultStatus pushHighMask(index, len) noexcept;
  [[nodiscard]] FaultStatus pushFlipMask(index, len) noexcept;
  void popMask() noexcept;
  void clearMasks() noexcept;
  [[nodiscard]] std::size_t maskCount() const noexcept;

  // Resolution
  MasterStatus resolve() noexcept;

  // Accessors
  [[nodiscard]] const T* input() const noexcept;
  [[nodiscard]] T* output() noexcept;
  [[nodiscard]] const T* output() const noexcept;
  [[nodiscard]] std::uint8_t* outputBytes() noexcept;
  [[nodiscard]] static constexpr std::size_t size() noexcept;

  // Status
  [[nodiscard]] MasterStatus masterStatus() const noexcept;
  [[nodiscard]] std::uint8_t proxyStatus(ProxySlot slot) const noexcept;
  [[nodiscard]] bool anyProxyFailed() const noexcept;

  // Compile-time queries
  [[nodiscard]] static constexpr bool swapRequired() noexcept;
  [[nodiscard]] static constexpr bool faultsSupported() noexcept;
  [[nodiscard]] static constexpr bool needsOverlay() noexcept;
};
```

#### Usage

```cpp
namespace data = system_core::data;

struct Telemetry {
  std::uint32_t timestamp;
  std::uint16_t sensorId;
  std::uint16_t value;
};

Telemetry packet{0x12345678, 0x0001, 0x00FF};

// Swap + fault injection enabled
data::MasterDataProxy<Telemetry, true, true> proxy(&packet);

// Queue a fault: zero the timestamp field
proxy.pushZeroMask(0, 4);
proxy.setFaultsEnabled(true);

auto status = proxy.resolve();
if (status == data::MasterStatus::SUCCESS) {
  // Output has swapped bytes with first 4 bytes zeroed
  const auto* out = proxy.output();
  // out->timestamp == 0x00000000 (zeroed after swap)
}
```

---

### DataTarget

**Header:** `DataTarget.hpp`
**Purpose:** Runtime addressing for registered data blocks.

#### Key Types

```cpp
struct DataTarget {
  std::uint32_t fullUid{0};    ///< Component fullUid (from registry).
  DataCategory category{};     ///< Which data block within the component.
  std::uint16_t byteOffset{0}; ///< Starting byte within the data block.
  std::uint8_t byteLen{0};     ///< Bytes affected (0 = whole block).
};
```

#### API

```cpp
[[nodiscard]] bool isWholeBlock(const DataTarget& t) noexcept;
[[nodiscard]] bool isInBounds(const DataTarget& t, std::size_t blockSize) noexcept;
[[nodiscard]] std::size_t effectiveLen(const DataTarget& t, std::size_t blockSize) noexcept;
[[nodiscard]] bool operator==(const DataTarget& a, const DataTarget& b) noexcept;
```

#### Usage

```cpp
namespace data = system_core::data;

// Target altitude field in HilPlantModel OUTPUT
data::DataTarget target{};
target.fullUid = 0x007800;
target.category = data::DataCategory::OUTPUT;
target.byteOffset = 36;  // altitude offset
target.byteLen = 4;      // float = 4 bytes

// Whole-block addressing (byteLen = 0)
data::DataTarget wholeBlock{};
wholeBlock.fullUid = 0x007A00;
wholeBlock.category = data::DataCategory::STATE;
```

---

### DataWatchpoint

**Header:** `DataWatchpoint.hpp`
**Purpose:** Conditional data triggers with type-aware comparison and edge detection.

#### Key Types

```cpp
enum class WatchPredicate : std::uint8_t {
  GT, LT, GE, LE, EQ, NE,     // Typed comparison
  BIT_SET, BIT_CLEAR,          // Bitfield checks
  CHANGED,                     // Value differs from last evaluation
  CUSTOM                       // User-supplied delegate
};

enum class WatchDataType : std::uint8_t {
  UINT8, UINT16, UINT32, UINT64,
  INT8, INT16, INT32, INT64,
  FLOAT32, FLOAT64, RAW
};

struct DataWatchpoint {
  DataTarget target{};
  WatchPredicate predicate{WatchPredicate::EQ};
  WatchDataType dataType{WatchDataType::RAW};
  std::uint16_t eventId{0};
  std::array<std::uint8_t, 8> threshold{};
  WatchAssessDelegate customAssess{};
  bool armed{false};
  std::uint32_t minFireCount{0};  // Debounce threshold
};

struct WatchpointGroup {
  std::array<std::uint8_t, 4> indices{};
  std::uint8_t count{0};
  GroupLogic logic{GroupLogic::AND};
  std::uint16_t eventId{0};
  bool armed{false};
};
```

#### API

```cpp
[[nodiscard]] bool evaluatePredicate(const DataWatchpoint& wp,
                                     const std::uint8_t* data,
                                     std::size_t dataLen) noexcept;
bool evaluateEdge(DataWatchpoint& wp, const std::uint8_t* data,
                  std::size_t dataLen) noexcept;
[[nodiscard]] bool evaluateGroup(const WatchpointGroup& group,
                                  const DataWatchpoint* table,
                                  std::size_t tableSize) noexcept;
bool evaluateGroupEdge(WatchpointGroup& group, const DataWatchpoint* table,
                       std::size_t tableSize) noexcept;
```

#### Usage

```cpp
namespace data = system_core::data;

// Watch altitude > 150.0
data::DataWatchpoint wp{};
wp.target = {0x007800, data::DataCategory::OUTPUT, 36, 4};
wp.predicate = data::WatchPredicate::GT;
wp.dataType = data::WatchDataType::FLOAT32;
wp.eventId = 1;
float threshold = 150.0F;
std::memcpy(wp.threshold.data(), &threshold, 4);
wp.armed = true;
```

---

### DataAction

**Header:** `DataAction.hpp`
**Purpose:** Runtime data operations with timing triggers and lifecycle management.

#### Key Types

```cpp
enum class ActionType : std::uint8_t {
  DATA_WRITE,   // AND/XOR mask applied to target bytes
  COMMAND,      // Opcode + payload routed to component
  ARM_CONTROL   // Arm/disarm a watchpoint, group, or sequence
};

enum class ActionTrigger : std::uint8_t {
  IMMEDIATE,    // Next cycle
  AT_CYCLE,     // At specific cycle count
  AFTER_CYCLES, // N cycles from now
  AT_TIME,      // At absolute sim time (ms)
  ON_EVENT      // When a watchpoint event fires
};

enum class ActionStatus : std::uint8_t {
  SUCCESS, ERROR_FULL, ERROR_PARAM, ERROR_BOUNDS,
  ERROR_TARGET, PENDING, ACTIVE, EXPIRED
};

struct DataAction {
  DataTarget target{};
  ActionType actionType{ActionType::DATA_WRITE};
  ActionTrigger trigger{};
  ActionStatus status{};
  std::uint32_t triggerParam{0};
  std::uint32_t duration{0};       // Cycles to hold (0 = one-shot)
  std::array<std::uint8_t, 32> andMask{};
  std::array<std::uint8_t, 32> xorMask{};
  // COMMAND fields: commandOpcode, commandPayload, commandPayloadLen
  // ARM_CONTROL fields: armTarget, armIndex, armState
};
```

Mask rule: `byte = (byte & AND[i]) ^ XOR[i]`

Common patterns:

- Zero: AND=0x00, XOR=0x00
- High: AND=0x00, XOR=0xFF
- Flip: AND=0xFF, XOR=0xFF
- Set value: AND=0x00, XOR=value

#### API (Helpers)

```cpp
void initZeroAction(DataAction& action, const DataTarget& target,
                    ActionTrigger trigger, uint32_t triggerParam,
                    uint32_t duration = 0) noexcept;
void initHighAction(...) noexcept;
void initFlipAction(...) noexcept;
void initSetAction(DataAction& action, const DataTarget& target,
                   ActionTrigger trigger, uint32_t triggerParam,
                   const void* value, uint8_t valueLen,
                   uint32_t duration = 0) noexcept;
void initCommandAction(DataAction& action, uint32_t targetUid,
                       ActionTrigger trigger, uint32_t triggerParam,
                       uint16_t opcode, const void* payload = nullptr,
                       uint8_t payloadLen = 0) noexcept;
void initArmControlAction(DataAction& action, ArmControlTarget armTarget,
                          uint8_t index, bool arm,
                          ActionTrigger trigger, uint32_t triggerParam) noexcept;
```

---

### DataSequence

**Header:** `DataSequence.hpp`
**Purpose:** Ordered action lists triggered by watchpoint events.

#### Key Types

```cpp
enum class SequenceType : std::uint8_t {
  RTS,  // Real-Time Sequence: delays in scheduler cycles
  ATS   // Absolute-Time Sequence: delays in milliseconds
};

struct SequenceStep {
  DataAction action{};
  std::uint32_t delayCycles{0};  // Delay before this step
};

struct DataSequence {
  std::array<SequenceStep, 8> steps{};
  std::uint8_t stepCount{0};
  SequenceType type{SequenceType::RTS};
  std::uint16_t eventId{0};
  bool armed{false};
  std::uint8_t repeatMax{0};  // 0=once, 0xFF=forever
};
```

#### API

```cpp
void startSequence(DataSequence& seq) noexcept;
[[nodiscard]] const DataAction* tickSequence(DataSequence& seq,
                                              bool& actionReady) noexcept;
void advanceStep(DataSequence& seq) noexcept;
void resetSequence(DataSequence& seq) noexcept;
[[nodiscard]] bool shouldTrigger(const DataSequence& seq, uint16_t eventId) noexcept;
[[nodiscard]] bool isComplete(const DataSequence& seq) noexcept;
[[nodiscard]] bool isRunning(const DataSequence& seq) noexcept;
```

---

### EventNotification

**Header:** `EventNotification.hpp`
**Purpose:** Immediate callbacks for watchpoint/group triggers.

#### Key Types

```cpp
using EventNotifyDelegate =
    apex::concurrency::Delegate<void, std::uint16_t, std::uint32_t>;

struct EventNotification {
  std::uint16_t eventId{0};
  EventNotifyDelegate callback{};
  bool armed{false};
  std::uint32_t invokeCount{0};
};
```

#### API

```cpp
[[nodiscard]] bool shouldNotify(const EventNotification& note,
                                 uint16_t eventId) noexcept;
void invokeNotification(EventNotification& note, uint16_t eventId,
                        uint32_t fireCount) noexcept;
uint8_t dispatchEvent(EventNotification* table, std::size_t tableSize,
                      uint16_t eventId, uint32_t fireCount) noexcept;
```

---

### ActionInterface

**Header:** `ActionInterface.hpp`
**Purpose:** Orchestrator that drives the evaluate-dispatch-execute pipeline each cycle.

#### Key Types

```cpp
struct ResolvedData {
  std::uint8_t* data{nullptr};
  std::size_t size{0};
};

using DataResolveDelegate =
    apex::concurrency::Delegate<ResolvedData, uint32_t, DataCategory>;
using CommandDelegate =
    apex::concurrency::Delegate<void, uint32_t, uint16_t, const uint8_t*, uint8_t>;

struct EngineStats {
  uint32_t totalCycles{0};
  uint32_t watchpointsFired{0};
  uint32_t groupsFired{0};
  uint32_t actionsApplied{0};
  uint32_t commandsRouted{0};
  uint32_t armControlsApplied{0};
  uint32_t sequenceSteps{0};
  uint32_t notificationsInvoked{0};
  uint32_t resolveFailures{0};
};

struct ActionInterface {
  DataResolveDelegate resolver{};
  CommandDelegate commandHandler{};
  std::array<DataWatchpoint, 8> watchpoints{};
  std::array<WatchpointGroup, 4> groups{};
  std::array<DataSequence, 4> sequences{};
  std::array<EventNotification, 8> notifications{};
  std::array<DataAction, 16> actions{};
  uint8_t actionCount{0};
  EngineStats stats{};
};
```

#### API

```cpp
// Main cycle (call once per scheduler frame)
void processCycle(ActionInterface& iface, uint32_t currentCycle) noexcept;

// Action queue
[[nodiscard]] ActionStatus queueAction(ActionInterface& iface,
                                        const DataAction& action) noexcept;
void removeAction(ActionInterface& iface, uint8_t index) noexcept;

// Target resolution
[[nodiscard]] uint8_t* resolveTarget(ActionInterface& iface,
                                      const DataTarget& target,
                                      std::size_t& dataLen) noexcept;

// Sub-pipelines (called by processCycle)
uint8_t evaluateWatchpoints(ActionInterface& iface, uint16_t* firedEvents,
                            std::size_t maxEvents) noexcept;
uint8_t evaluateGroups(ActionInterface& iface, uint16_t* firedEvents,
                       uint8_t currentCount, std::size_t maxEvents) noexcept;
void dispatchEvents(ActionInterface& iface, const uint16_t* firedEvents,
                    uint8_t eventCount) noexcept;
void tickSequences(ActionInterface& iface) noexcept;
void processActions(ActionInterface& iface, uint32_t currentCycle) noexcept;

// Reset
void resetInterface(ActionInterface& iface) noexcept;
```

#### Usage

```cpp
namespace data = system_core::data;

data::ActionInterface engine{};
engine.resolver = {myResolverFn, &registryCtx};

// Configure watchpoint, sequence, etc. (see individual module docs)

// Run each scheduler cycle
data::processCycle(engine, currentCycle);

// Check diagnostics
auto& stats = engine.stats;
// stats.watchpointsFired, stats.actionsApplied, etc.
```

---

## 6. Requirements

### Build Requirements

- **C++ Standard:** C++17 or later
- **Compiler:** GCC 9+, Clang 10+

### Dependencies

| Dependency                | Purpose                                                    |
| ------------------------- | ---------------------------------------------------------- |
| `utilities_compatibility` | `byteswap` polyfill for C++17                              |
| `utilities_concurrency`   | `Delegate` type for callbacks (watchpoints, notifications) |

---

## 7. Testing

| Directory | Type                   | Tests | Runs with `make test` |
| --------- | ---------------------- | ----- | --------------------- |
| `utst/`   | Unit tests             | 263   | Yes                   |
| `ptst/`   | Performance benchmarks | 16    | No (manual)           |

### Test Organization

| Module              | Test File                       | Tests   |
| ------------------- | ------------------------------- | ------- |
| DataCategory        | `DataCategory_uTest.cpp`        | 11      |
| ModelData           | `ModelData_uTest.cpp`           | 26      |
| FaultInjectionProxy | `FaultInjectionProxy_uTest.cpp` | 22      |
| EndiannessProxy     | `EndiannessProxy_uTest.cpp`     | 14      |
| MasterDataProxy     | `MasterDataProxy_uTest.cpp`     | 15      |
| DataTarget          | `DataTarget_uTest.cpp`          | 17      |
| DataWatchpoint      | `DataWatchpoint_uTest.cpp`      | 49      |
| DataAction          | `DataAction_uTest.cpp`          | 30      |
| DataSequence        | `DataSequence_uTest.cpp`        | 38      |
| EventNotification   | `EventNotification_uTest.cpp`   | 12      |
| ActionInterface     | `ActionInterface_uTest.cpp`     | 29      |
| **Total**           |                                 | **263** |

---

## 8. See Also

- `src/utilities/compatibility/` - Byteswap polyfills used by EndiannessProxy
- `src/utilities/concurrency/` - Delegate type used by watchpoints and notifications
- `src/system/core/components/action/` - ActionComponent wrapping ActionInterface for executive integration
- `src/system/core/components/registry/` - Registry that ActionInterface resolves targets against
