# Data Infrastructure Design

This document describes the design for the system-wide data infrastructure that
provides typed, static-sized wrappers for model and component data with optional
runtime transformations (endianness, fault injection, future encryption) and
runtime data operations (watchpoints, actions, sequences).

## Goals

1. **Static sizing** - No dynamic allocation (no std::vector, std::string, std::queue)
2. **RT-safe** - Bounded O(1) operations after initialization
3. **Zero-overhead when unused** - Compile-time proxy selection via `if constexpr`
4. **User-defined structs** - Infrastructure wraps externally-defined POD types
5. **Semantic categories** - Distinguish params, state, inputs, outputs
6. **Runtime data operations** - Watchpoints, timed actions, and sequences on
   any registered data block

## Data Categories

Five categories of data, each with different access patterns:

| Category        | Description                   | Mutability                 | Typical Use                |
| --------------- | ----------------------------- | -------------------------- | -------------------------- |
| `STATIC_PARAM`  | Constants driving the model   | Read-only                  | Physical constants, config |
| `TUNABLE_PARAM` | Runtime-adjustable parameters | External write             | Gains, thresholds          |
| `STATE`         | Internal model state          | Model read/write           | Integrator state, history  |
| `INPUT`         | Real-time data fed to model   | External write, model read | Sensor data, commands      |
| `OUTPUT`        | Real-time data from model     | Model write, external read | Computed values, telemetry |

## Architecture

### Layered Design

```
Layer 1: DataCategory + ModelData<T>          Storage + semantics
Layer 2: FaultInjectionProxy                  Byte-level mask operations
Layer 3: EndiannessProxy                      Byte-swap
Layer 4: MasterDataProxy                      Orchestration of Layer 2+3
Layer 5: DataTarget + DataAction              Runtime addressing + operations
Layer 6: DataWatchpoint                       Conditional triggers
Layer 7: Sequences (rts/, ats/)               Ordered action lists
```

Layers 1-4 are **compile-time proxy infrastructure** -- they define what data
IS (endianness, encryption, integrity). Layers 5-7 are **runtime operations**
-- they define what is DONE TO data under certain conditions.

### Proxy vs. Runtime Boundary

The proxy interface (MasterDataProxy chain) handles transformations that are
**intrinsic to the data's identity**. The runtime system handles things
**done to** the data from outside.

| Candidate       | IS or DOES?                          | Where               |
| --------------- | ------------------------------------ | ------------------- |
| Endianness swap | IS (wire format is big-endian)       | Proxy (Layer 3)     |
| Encryption      | IS (data is encrypted in transit)    | Proxy (future slot) |
| CRC attachment  | IS (data carries integrity check)    | Proxy (future slot) |
| Fault injection | DOES (test scenario zeros a field)   | Runtime (Layer 5)   |
| Watchpoint      | DOES (monitor a value for threshold) | Runtime (Layer 6)   |
| Timed override  | DOES (force a value for N cycles)    | Runtime (Layer 5)   |
| Sequence        | DOES (ordered list of actions)       | Runtime (Layer 7)   |

**Decision criterion:** "Does this describe what the data IS, or what someone
wants to DO to it?"

- **Proxy:** Always applies, every cycle, as a property of the data path.
  Configured once at init. Adds a slot to MasterDataProxy (slots 3-7 reserved).
- **Runtime:** Conditional, situational, loaded at runtime. Managed by the
  DataProxyManager component. Uses FaultInjectionProxy as the byte-level
  mechanism but provides policy (when, where, why).

### Compile-Time Proxy Architecture

```
User-defined POD struct
         |
         v
+-----------------------------------------+
|  ModelData<T, Category>                 |
|  +-----------------------------------+  |
|  |  T data_                          |  |  <-- Actual storage (owned)
|  +-----------------------------------+  |
|  +-----------------------------------+  |
|  |  MasterDataProxy<T, ...>          |  |  <-- Optional transformations
|  |  +-- EndiannessProxy<T>           |  |      (what data IS)
|  |  +-- FaultInjectionProxy          |  |
|  |  +-- [EncryptionProxy] (future)   |  |
|  |  +-- [ChecksumProxy] (future)     |  |
|  +-----------------------------------+  |
+-----------------------------------------+
```

### Runtime Operations Architecture

```
Registry (frozen, RT-safe queries)
  |
  +-- getBytes(fullUid, category) --> raw byte pointer
  |
  v
+-----------------------------------------+
|  DataProxyManager (SUPPORT component)   |
|  +-----------------------------------+  |
|  |  ActionQueue                      |  |  <-- Timed/cycle-based actions
|  |  +-- DataAction entries           |  |
|  +-----------------------------------+  |
|  +-----------------------------------+  |
|  |  WatchpointTable                  |  |  <-- Conditional triggers
|  |  +-- DataWatchpoint entries       |  |
|  +-----------------------------------+  |
|  +-----------------------------------+  |
|  |  Sequences (from rts/, ats/)      |  |  <-- Ordered action lists
|  |  +-- SequenceStep arrays          |  |
|  +-----------------------------------+  |
+-----------------------------------------+
         |
         v
  FaultInjectionProxy::apply()  <-- Byte-level mechanism (reused)
```

## Component Design

### 1. DataCategory (enum)

```cpp
enum class DataCategory : std::uint8_t {
  STATIC_PARAM,   // Read-only constants
  TUNABLE_PARAM,  // Runtime-adjustable parameters
  STATE,          // Internal model state
  INPUT,          // External data fed to model
  OUTPUT          // Data produced by model
};
```

### 2. EndiannessProxy<T, SwapRequired>

Handles byte-swapping for cross-platform data exchange.

- Raw pointer instead of shared_ptr
- No allocation - operates on external storage
- Compile-time swap decision

```cpp
template <typename T, bool SwapRequired>
class EndiannessProxy {
public:
  explicit EndiannessProxy(const T* in, T* out) noexcept;
  void resolve() noexcept;  // Copy or swap
private:
  const T* in_;
  T* out_;
};
```

### 3. FaultInjectionProxy (static-sized)

Queue of AND/XOR masks for fault injection.

- Fixed-size mask storage: `std::array<MaskEntry, MAX_MASKS>`
- No std::queue or std::vector
- Circular buffer semantics

```cpp
struct MaskEntry {
  std::size_t index;                      // Starting byte index
  std::array<std::uint8_t, MAX_MASK_LEN> andMask;
  std::array<std::uint8_t, MAX_MASK_LEN> xorMask;
  std::uint8_t len;                       // Actual mask length (0 = empty)
};

class FaultInjectionProxy {
public:
  static constexpr std::size_t MAX_MASKS = 4;
  static constexpr std::size_t MAX_MASK_LEN = 32;

  FaultStatus push(std::size_t index, const std::uint8_t* andMask,
                   const std::uint8_t* xorMask, std::uint8_t len) noexcept;
  void pop() noexcept;
  void clear() noexcept;
  FaultStatus apply(std::uint8_t* data, std::size_t dataSize) noexcept;

private:
  std::array<MaskEntry, MAX_MASKS> masks_{};
  std::uint8_t head_ = 0;
  std::uint8_t count_ = 0;
};
```

### 4. MasterDataProxy<T, SwapRequired, FaultsEnabled>

Orchestrates compile-time transformations. No virtuals, no factory.

- Raw pointers instead of shared_ptr
- Composes EndiannessProxy and FaultInjectionProxy directly
- Static status table with 8 slots (3 used, 5 reserved for future proxies)

```cpp
template <typename T, bool SwapRequired, bool FaultsEnabled>
class MasterDataProxy {
public:
  explicit MasterDataProxy(const T* input) noexcept;

  void setFaultsEnabled(bool enabled) noexcept;
  MasterStatus resolve() noexcept;

  T* output() noexcept;
  const T* input() const noexcept;

private:
  const T* in_;
  T* out_;
  T overlay_;  // Only used if SwapRequired || FaultsEnabled

  EndiannessProxy<T, SwapRequired> endian_;
  [[no_unique_address]] std::conditional_t<FaultsEnabled,
      FaultInjectionProxy, std::monostate> fault_;

  std::array<std::uint8_t, 8> status_{};
  bool faultsEnabled_ = false;
};
```

### 5. ModelData<T, Category>

The main wrapper that models use.

```cpp
template <typename T, DataCategory Cat>
class ModelData {
  static_assert(std::is_trivially_copyable_v<T>,
                "T must be trivially copyable");
public:
  T& get() noexcept;
  const T& get() const noexcept;

  const T* ptr() const noexcept;
  static constexpr std::size_t size() noexcept { return sizeof(T); }
  static constexpr DataCategory category() noexcept { return Cat; }

private:
  T data_{};
};

// Aliases
template <typename T> using StaticParam  = ModelData<T, DataCategory::STATIC_PARAM>;
template <typename T> using TunableParam = ModelData<T, DataCategory::TUNABLE_PARAM>;
template <typename T> using State        = ModelData<T, DataCategory::STATE>;
template <typename T> using Input        = ModelData<T, DataCategory::INPUT>;
template <typename T> using Output       = ModelData<T, DataCategory::OUTPUT>;
```

### 6. DataTarget (runtime data addressing)

Identifies a byte range within any registered data block. This is the
fundamental addressing primitive for all runtime operations.

```cpp
struct DataTarget {
  std::uint32_t fullUid;      // Component fullUid (from registry)
  DataCategory category;      // Which data block
  std::uint16_t byteOffset;   // Starting byte within block
  std::uint8_t byteLen;       // How many bytes affected (0 = whole block)
};
```

### 7. DataAction (runtime byte operation)

Combines a DataTarget with AND/XOR mask semantics from FaultInjectionProxy.
Adds trigger timing (immediate, at-cycle, at-time, on-event) and command
dispatch for cross-bus operations.

Two operation modes:

- **DATA_WRITE**: Byte-level AND/XOR mask applied to target bytes.
- **COMMAND**: Opcode + payload routed to target component via bus.

```cpp
enum class ActionType : std::uint8_t {
  DATA_WRITE,   // AND/XOR mask applied to target bytes
  COMMAND       // Opcode + payload routed to target component
};

enum class ActionTrigger : std::uint8_t {
  IMMEDIATE,    // Execute on next manager cycle
  AT_CYCLE,     // At specific scheduler cycle count
  AFTER_CYCLES, // N cycles from now (relative)
  AT_TIME,      // At absolute simulation time (fixed-point seconds)
  ON_EVENT      // When a watchpoint fires
};

struct DataAction {
  DataTarget target;
  ActionType actionType;
  ActionTrigger trigger;
  ActionStatus status;
  std::uint32_t triggerParam;   // Cycle count, time, or eventId
  std::uint32_t duration;       // Cycles to hold (0 = one-shot). DATA_WRITE only.
  std::uint32_t cyclesRemaining;

  // DATA_WRITE fields
  std::array<std::uint8_t, FAULT_MAX_MASK_LEN> andMask;
  std::array<std::uint8_t, FAULT_MAX_MASK_LEN> xorMask;

  // COMMAND fields
  std::uint16_t commandOpcode;
  std::uint8_t commandPayloadLen;
  std::array<std::uint8_t, COMMAND_PAYLOAD_MAX> commandPayload;
};
```

### 8. DataWatchpoint (conditional trigger)

Watches a byte range and evaluates a predicate each cycle. Fires an event
ID when the predicate transitions from false to true (edge-triggered).

Supports custom delegates via `WatchAssessDelegate` for arbitrary predicate
logic without allocation (uses `Delegate<bool, const uint8_t*, size_t>`
from the concurrency library).

```cpp
enum class WatchPredicate : std::uint8_t {
  GT, LT, GE, LE, EQ, NE,  // Type-aware comparison
  BIT_SET, BIT_CLEAR,       // Bitfield checks
  CHANGED,                   // Value differs from last check
  CUSTOM                     // User-supplied delegate
};

using WatchAssessDelegate =
    apex::concurrency::Delegate<bool, const std::uint8_t*, std::size_t>;

struct DataWatchpoint {
  DataTarget target;
  WatchPredicate predicate;
  WatchDataType dataType;
  std::uint16_t eventId;
  std::array<std::uint8_t, 8> threshold;
  std::array<std::uint8_t, 8> lastValue;
  WatchAssessDelegate customAssess;   // For CUSTOM predicate
  bool armed;
  bool lastResult;
  std::uint32_t fireCount;
};
```

### 9. WatchpointGroup (multi-variable assessment)

Combines multiple watchpoint results with AND/OR logic or a custom
delegate for cross-variable equations (e.g., `var1 + 3*var2 > 50`).

```cpp
enum class GroupLogic : std::uint8_t { AND, OR };

using GroupAssessDelegate =
    apex::concurrency::Delegate<bool, const bool*, std::uint8_t>;

struct WatchpointGroup {
  std::array<std::uint8_t, 4> indices;  // Watchpoint table indices
  std::uint8_t count;
  GroupLogic logic;
  GroupAssessDelegate customAssess;      // Overrides logic when set
  std::uint16_t eventId;
  bool armed;
  bool lastResult;
  std::uint32_t fireCount;
};
```

### 10. DataSequence (ordered action list)

An ordered list of steps triggered by a watchpoint event. Each step
pairs a DataAction with a delay (cycles for RTS, milliseconds for ATS).

```cpp
enum class SequenceType : std::uint8_t { RTS, ATS };
enum class SequenceStatus : std::uint8_t { IDLE, WAITING, EXECUTING, COMPLETE };

struct SequenceStep {
  DataAction action;
  std::uint32_t delayCycles;  // Delay before this step
};

struct DataSequence {
  std::array<SequenceStep, SEQUENCE_MAX_STEPS> steps;
  std::uint8_t stepCount;
  std::uint8_t currentStep;
  SequenceType type;
  SequenceStatus status;
  std::uint16_t eventId;         // Event that triggers this sequence
  std::uint32_t delayRemaining;
  bool armed;
  std::uint32_t runCount;
};
```

Connection to watchpoints:

- A watchpoint or group fires an `eventId`
- Sequences bound to that `eventId` start executing from step 0
- Re-triggering while running restarts from step 0 (preemptive)

Steps can contain DATA_WRITE or COMMAND actions, enabling sequences
that both modify data and send cross-bus commands.

## Filesystem Integration

The ApexFileSystem allocates directories for runtime sequences:

| Directory | Purpose                 | Content                                    |
| --------- | ----------------------- | ------------------------------------------ |
| `rts/`    | Real-Time Sequences     | Binary sequence files loaded at runtime    |
| `ats/`    | Absolute Time Sequences | Time-tagged command sequences from storage |

Sequence files are arrays of `SequenceStep` packed structs, loaded by the
DataProxyManager during init or via C2 command.

## Usage Examples

### Compile-Time Proxy (what data IS)

```cpp
// User defines their structs
struct SensorInputs {
  double temperature;
  double pressure;
  std::uint32_t flags;
};

struct ControlOutputs {
  double throttle;
  double pitch;
  std::uint8_t mode;
};

// Model uses typed wrappers
class ThermalModel : public SimModelBase {
public:
  std::uint8_t init() noexcept override {
    registerData(DataCategory::INPUT, "sensors",
                 &inputs_.get(), sizeof(SensorInputs));
    registerData(DataCategory::OUTPUT, "control",
                 &outputs_.get(), sizeof(ControlOutputs));
    return 0;
  }

  std::uint8_t step() noexcept {
    double temp = inputs_.get().temperature;
    outputs_.get().throttle = computeThrottle(temp);
    return 0;
  }

private:
  Input<SensorInputs> inputs_;
  Output<ControlOutputs> outputs_;
};
```

### Runtime Operations (what is DONE TO data)

```cpp
// Zero the thrust.z field of HilDriver #0 OUTPUT at cycle 500
DataAction action{};
DataTarget target{0x007A00, DataCategory::OUTPUT, 8, 4};
initZeroAction(action, target, ActionTrigger::AT_CYCLE, 500, 100);

// Watch altitude exceed 150m on HilPlantModel OUTPUT
DataWatchpoint watch{};
watch.target = {0x007800, DataCategory::OUTPUT, 36, 4};
watch.predicate = WatchPredicate::GT;
watch.dataType = WatchDataType::FLOAT32;
watch.eventId = 1;
float threshold = 150.0F;
std::memcpy(watch.threshold.data(), &threshold, 4);
watch.armed = true;

// Custom delegate: fire when temperature rate exceeds 5 deg/sec
struct RateCtx { float lastTemp; float rateLimit; };
RateCtx rateCtx{0.0F, 5.0F};
auto rateFn = [](void* c, const uint8_t* data, size_t) noexcept -> bool {
  auto* ctx = static_cast<RateCtx*>(c);
  float temp{};
  std::memcpy(&temp, data, sizeof(float));
  float rate = temp - ctx->lastTemp;
  ctx->lastTemp = temp;
  return rate > ctx->rateLimit;
};
DataWatchpoint rateWatch{};
rateWatch.predicate = WatchPredicate::CUSTOM;
rateWatch.customAssess = {rateFn, &rateCtx};
rateWatch.armed = true;

// Group: altitude > 150 AND velocity < 10
WatchpointGroup group{};
group.indices = {0, 1};
group.count = 2;
group.logic = GroupLogic::AND;
group.eventId = 5;
group.armed = true;

// Send RESET command to HilDriver when group fires
DataAction cmd{};
initCommandAction(cmd, 0x007A00, ActionTrigger::ON_EVENT, 5, 0x01);

// 2-step sequence: zero thrust, wait 50 cycles, restore
DataSequence seq{};
seq.type = SequenceType::RTS;
seq.eventId = 5;
seq.stepCount = 2;
initZeroAction(seq.steps[0].action, target, ActionTrigger::IMMEDIATE, 0, 50);
seq.steps[0].delayCycles = 0;
float nominal = 100.0F;
initSetAction(seq.steps[1].action, target, ActionTrigger::IMMEDIATE, 0,
              &nominal, 4, 0);
seq.steps[1].delayCycles = 50;
seq.armed = true;
```

## Implementation Order

Build bottom-up with unit tests at each layer:

1. **DataCategory.hpp** - Simple enum + toString (done)
2. **FaultInjectionProxy** - Static-sized mask queue (done)
3. **EndiannessProxy** - Byte-swap with raw pointers (done)
4. **MasterDataProxy** - Compose proxies (done)
5. **ModelData** - Category-aware wrapper (done)
6. **DataTarget.hpp** - Runtime data addressing (done)
7. **DataAction.hpp** - Action descriptors with timing + commands (done)
8. **DataWatchpoint.hpp** - Conditional triggers + custom delegates + groups (done)
9. **DataSequence.hpp** - Ordered action lists with RTS/ATS modes (done)
10. **DataProxyManager** - SUPPORT component orchestrating 6-9 (planned)
11. **TOML schema** - Configuration for watchpoints, groups, sequences (planned)
12. **Command registry** - Component command registration + validation (planned)

## Static Sizing Constraints

| Constant                      | Value | Rationale                                 |
| ----------------------------- | ----- | ----------------------------------------- |
| `FAULT_MAX_MASKS`             | 4     | Typical fault scenarios need 1-2 masks    |
| `FAULT_MAX_MASK_LEN`          | 32    | Covers most field sizes (up to 256-bit)   |
| `ACTION_QUEUE_SIZE`           | 16    | Max concurrent pending actions            |
| `WATCHPOINT_TABLE_SIZE`       | 8     | Max concurrent watchpoints                |
| `WATCHPOINT_GROUP_MAX_REFS`   | 4     | Max watchpoints per group                 |
| `WATCHPOINT_GROUP_TABLE_SIZE` | 4     | Max concurrent groups                     |
| `SEQUENCE_MAX_STEPS`          | 8     | Max steps per sequence                    |
| `SEQUENCE_TABLE_SIZE`         | 4     | Max concurrent sequences                  |
| `COMMAND_PAYLOAD_MAX`         | 16    | Max command payload bytes                 |
| `WATCH_VALUE_SIZE`            | 8     | Max threshold/value bytes (covers double) |

These can be template parameters if needed, but fixed defaults keep API simple.

## Future Extensions

### Proxy Chain (Layer 4 slots)

- **Encryption proxy** - Add to proxy chain (slot 3)
- **Checksum proxy** - CRC on data blocks (slot 4)
- **Timestamping proxy** - Attach time metadata to I/O (slot 5)

### Runtime Operations (Layer 5-7)

- **C2 command interface** - External action/watch management
- **Sequence state machine** - Step dependencies, conditional branches
- **Double buffering** - For async producer/consumer
- **Action logging** - Record all applied actions to tlm/ for post-analysis
