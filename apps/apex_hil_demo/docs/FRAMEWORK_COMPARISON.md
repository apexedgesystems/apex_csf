# Flight Software Framework Comparison

Side-by-side comparison of Apex, NASA cFS, and NASA F Prime measured on
identical hardware under controlled conditions. All frameworks running
concurrently on the same Raspberry Pi 4.

## Test Environment

| Parameter          | Value                                                 |
| ------------------ | ----------------------------------------------------- |
| Hardware           | Raspberry Pi 4 Model B, 4x Cortex-A72 @ 1.5 GHz       |
| Memory             | 4 GB LPDDR4                                           |
| OS                 | Debian 13 (Trixie), aarch64                           |
| Kernel             | 6.8.0, PREEMPT                                        |
| Compiler (Apex)    | GCC 14, -O3 -DNDEBUG, aarch64-linux-gnu cross-compile |
| Compiler (cFS)     | GCC 14, native build on Pi                            |
| Compiler (F Prime) | GCC 14, native build on Pi                            |
| Measurement        | /proc/[pid]/stat sampling, 10-second window           |
| Date               | 2026-03-22                                            |

## Configurations Under Test

Five processes running simultaneously on the Pi during measurement:

| Label           | Framework    | Version | Rate    | Description                                                                                                                                                                                                                        |
| --------------- | ------------ | ------- | ------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Apex Base**   | Apex CSF     | HEAD    | 100 Hz  | Bare framework: scheduler, filesystem, registry, interface, action engine. Zero application components, zero scheduled tasks. Pure framework overhead baseline.                                                                    |
| **Apex Full**   | Apex CSF     | HEAD    | 100 Hz  | Emulated HIL path: HilPlantModel (3DOF physics), VirtualFlightCtrl (SLIP over PTY), HilDriver. 9 components, 8 scheduled tasks.                                                                                                    |
| **Apex HIL**    | Apex CSF     | HEAD    | 1000 Hz | Full HIL demo: plant model, virtual controller, real STM32 driver (UART), emulated driver, comparator, action engine, system monitor, test plugin. 13 components (7 app + 6 built-in), 14 scheduled tasks, real UART I/O to STM32. |
| **cFS**         | NASA cFS     | v7.0+   | 100 Hz  | Core Flight Executive with 12 apps: CI_LAB, TO_LAB, SCH_LAB, LC, SC, CF, HS, DS, FM, SAMPLE_APP. sch_lab TickRate set to 100.                                                                                                      |
| **F Prime Ref** | NASA F Prime | 3.5+    | 100 Hz  | Reference deployment with ~30 components, 3 rate groups (divisors 1/2/4 off 100 Hz base clock). Modified from stock 1 Hz to 100 Hz for direct comparison.                                                                          |

## Runtime Performance

All measured simultaneously, 10-second /proc/stat sample:

| Framework   | Rate    | Components | Tasks         | CPU%   | RSS     | Threads |
| ----------- | ------- | ---------- | ------------- | ------ | ------- | ------- |
| Apex Base   | 100 Hz  | 6          | 0             | 0.60%  | 21.4 MB | 16      |
| cFS         | 100 Hz  | 12 apps    | msg bus       | 1.10%  | 5.9 MB  | 23      |
| Apex Full   | 100 Hz  | 9          | 8             | 1.70%  | 28.4 MB | 19      |
| Apex HIL    | 1000 Hz | 13         | 14            | 5.30%  | 39.1 MB | 23      |
| F Prime Ref | 100 Hz  | ~30        | 3 rate groups | 15.25% | 5.7 MB  | 22      |

All four 100 Hz builds are directly comparable. F Prime's high CPU is
kernel-dominant (131 sys ticks vs 22 user in a 10-second window), indicating
overhead in sleep/wake cycles (nanosleep, futex) rather than user-space
computation. Each of F Prime's ~30 active components runs its own thread,
producing significant context-switch overhead at 100 Hz.

**F Prime at stock 1 Hz (for reference):**

| Framework   | Rate | CPU%  | RSS    | Threads |
| ----------- | ---- | ----- | ------ | ------- |
| F Prime Ref | 1 Hz | 0.40% | 5.9 MB | 22      |

The 38x CPU increase (0.40% to 15.25%) from 1 Hz to 100 Hz confirms that
F Prime's overhead scales super-linearly with rate. A 100x rate increase
produced a 38x CPU increase, with the gap attributable to kernel scheduling
overhead dominating at higher frequencies.

### Per-Tick CPU Cost (normalized to 100 Hz)

| Framework            | Rate    | CPU%   | CPU / 100 Hz |
| -------------------- | ------- | ------ | ------------ |
| Apex HIL             | 1000 Hz | 5.30%  | 0.53%        |
| Apex Base            | 100 Hz  | 0.60%  | 0.60%        |
| cFS                  | 100 Hz  | 1.10%  | 1.10%        |
| Apex Full            | 100 Hz  | 1.70%  | 1.70%        |
| F Prime Ref (1 Hz)   | 1 Hz    | 0.40%  | 40.00%       |
| F Prime Ref (100 Hz) | 100 Hz  | 15.25% | 15.25%       |

Apex HIL at 1000 Hz normalizes to 0.53% per 100 Hz equivalent, the lowest
per-tick cost. The higher absolute CPU at 100 Hz for Apex Full (1.70%)
compared to Apex Base (0.60%) is the cost of real work: 3DOF physics
integration, SLIP framing, PTY read/write, and CRC validation every cycle.

F Prime's 15.25% at 100 Hz is 25x the Apex Base overhead and 14x cFS. This
is a consequence of F Prime's one-thread-per-active-component model: ~22
threads each waking via nanosleep every 10 ms produces heavy context-switch
and futex overhead. The framework was designed for 1 Hz spacecraft operations,
not high-rate embedded control loops.

### Memory Analysis

| Framework   | RSS     | Notes                                                |
| ----------- | ------- | ---------------------------------------------------- |
| F Prime Ref | 5.7 MB  | Static linking, minimal heap                         |
| cFS         | 5.9 MB  | Static allocation, shared memory regions             |
| Apex Base   | 21.4 MB | Dynamic shared libs, async log queues, thread stacks |
| Apex Full   | 28.4 MB | + component data, PTY buffers, SLIP codec state      |
| Apex HIL    | 39.1 MB | + UART buffers, comparator history, system monitor   |

RSS difference between Apex and cFS/F Prime is primarily:

- Shared library overhead (15-19 .so files mapped into process space)
- Async log backend (lock-free queues, dedicated flush thread)
- Thread pool stacks (2 worker threads + framework threads)
- Registry and TPRM data structures (heap-allocated)

## Binary and Deployment Size

| Framework   | Binary | Deploy Size | Shared Libs | Linking Strategy         |
| ----------- | ------ | ----------- | ----------- | ------------------------ |
| Apex Base   | 80 KB  | 2.0 MB      | 15          | Dynamic (.so)            |
| Apex Full   | 156 KB | 2.1 MB      | 16          | Dynamic (.so)            |
| Apex HIL    | 159 KB | 3.2 MB      | 19          | Dynamic (.so)            |
| cFS         | 1.5 MB | 3.4 MB      | 0           | Static + dlopen for apps |
| F Prime Ref | 2.6 MB | 14 MB       | 0           | Fully static             |

Apex executables are small (80-159 KB) because all framework code lives in
shared libraries. This enables library hot-swap at runtime without restarting
the executive. cFS uses dlopen for app loading but statically links the core.
F Prime statically links everything into a single monolithic binary.

## Architectural Comparison

### Scheduling

| Feature           | Apex                                                             | cFS                                        | F Prime                                        |
| ----------------- | ---------------------------------------------------------------- | ------------------------------------------ | ---------------------------------------------- |
| Model             | Priority-based preemptive, multi-thread pool                     | Cooperative, table-driven message dispatch | Rate group divisors, active component threads  |
| Configuration     | Binary TPRM (runtime changeable)                                 | Compile-time schedule tables (sch_lab)     | FPP topology + compile-time divisors           |
| Rate control      | Per-task frequency (N/D ratio), phase offset, priority           | Fixed table slots, one tick rate           | Integer divisors off base clock                |
| Multi-rate        | Native: any task can run at any rate independently               | Single base rate, tasks assigned to slots  | Fixed set of rate groups (typically 3)         |
| Thread model      | Dedicated clock thread + worker pool + I/O thread                | Single-process, multi-thread (one per app) | One thread per active component                |
| RT support        | SCHED_FIFO/RR, per-thread core affinity, hard deadline detection | SCHED_FIFO optional, OSAL abstraction      | Soft real-time (wall timers), no hard deadline |
| Deadline handling | Frame overrun detection, configurable lag tolerance              | No built-in deadline detection             | No built-in deadline detection                 |
| Sequencing        | Sequence groups with phase ordering within a tick                | Implicit via table slot ordering           | Implicit via port connection order             |

### Inter-Component Communication

| Feature            | Apex                                                                        | cFS                                       | F Prime                                     |
| ------------------ | --------------------------------------------------------------------------- | ----------------------------------------- | ------------------------------------------- |
| Primary mechanism  | Internal message bus (lock-free queues, pre-allocated buffers)              | Software Bus (SB) publish/subscribe       | Typed port connections (compile-time wired) |
| Wire protocol      | APROTO (external boundary only, not internal)                               | CCSDS packets (end-to-end, including SB)  | FPP-generated serialization                 |
| Internal latency   | ~0.14 us per command post (lock-free queue push)                            | Microseconds (SB routing + copy)          | Sub-microsecond (direct call via port)      |
| External transport | TCP/UDP/Unix/serial (APROTO + SLIP/COBS framing)                            | UDP (CI_LAB/TO_LAB)                       | TCP (to GDS)                                |
| Buffering          | BufferPool (pre-allocated) + MPMC cmd / SPSC tlm queues                     | SB pipe depth (configurable per app)      | Active component message queues             |
| Multicast          | Reference-counted MessageBuffer (single buffer, N readers, atomic refcount) | SB natively multicasts to all subscribers | Port fan-out in topology                    |

### Configuration and Tuning

| Feature               | Apex                                                | cFS                                   | F Prime                                |
| --------------------- | --------------------------------------------------- | ------------------------------------- | -------------------------------------- |
| Runtime config format | Binary TPRM (packed structs, per-component)         | cFE Table Services (binary tables)    | Parameter database (PrmDb)             |
| Config source         | TOML -> cfg2bin -> binary TPRM -> tprm_pack archive | Header files -> table images          | FPP constants + parameter definitions  |
| Runtime reload        | RELOAD_TPRM command (live, no restart)              | Table load/activate (live, per table) | Parameter update (live, per component) |
| Per-instance config   | fullUid = (componentId << 8) \| instanceIndex       | AppId-based table ownership           | Component instance base ID             |
| Config tools          | tprm_template, cfg2bin, tprm_pack (Rust CLI)        | Table tools (cFS-provided)            | FPP compiler + fprime-util             |

### Fault Management

| Feature             | Apex                                                            | cFS                                  | F Prime                           |
| ------------------- | --------------------------------------------------------------- | ------------------------------------ | --------------------------------- |
| Watchdog            | Built-in executive watchdog (configurable interval)             | Health Service (HS) app              | Health component (ping-based)     |
| Process supervision | ApexWatchdog (external, auto-restart on crash/hang)             | cFE Executive Services (app restart) | None built-in                     |
| Action engine       | DataWatchpoint + DataSequence + EventNotification (TPRM-driven) | Limit Checker (LC) app               | None built-in (custom components) |
| Graceful restart    | RELOAD_EXECUTIVE (execve, preserves PID)                        | cFE Processor Reset                  | Manual restart                    |
| Error isolation     | Per-component error status, lock/unlock individual components   | Per-app error counters, app restart  | Per-component error events        |

### Hot Swap and Runtime Updates

| Feature          | Apex                                              | cFS                             | F Prime                       |
| ---------------- | ------------------------------------------------- | ------------------------------- | ----------------------------- |
| Library hot-swap | RELOAD_LIBRARY (dlopen/dlclose, no restart)       | App stop/start (dlopen)         | Not supported (static binary) |
| Config hot-swap  | RELOAD_TPRM (re-read binary config, live)         | Table load/activate             | Parameter update              |
| Binary update    | Bank A/B swap + RELOAD_EXECUTIVE                  | cFE file transfer + app restart | Full redeploy                 |
| Schema evolution | PackedTprm versioned archives, forward-compatible | Table versioning via header     | FPP schema versioning         |

### Filesystem and Data Recording

| Feature             | Apex                                                      | cFS                                      | F Prime                        |
| ------------------- | --------------------------------------------------------- | ---------------------------------------- | ------------------------------ |
| Filesystem          | ApexFileSystem (structured: logs/, tlm/, rts/, ats/, db/) | cFE File Services (flat, /cf/ and /ram/) | OS filesystem (no abstraction) |
| Telemetry recording | Per-component binary log files (async, RT-safe)           | Data Storage (DS) app                    | PrmDb + Svc::ComLogger         |
| Data export         | RDAT/SDAT binary files + CLI analysis tools               | CCSDS packet capture                     | FPP-generated serialization    |
| File transfer       | Chunked C2 transfer with CRC-32C and progress             | CF (CFDP) app                            | Svc::FileUplink / FileDownlink |
| Log archival        | Automatic tar on shutdown (configurable)                  | Manual file retrieval                    | Manual file retrieval          |

### Command and Telemetry

| Feature              | Apex                                               | cFS                                | F Prime                         |
| -------------------- | -------------------------------------------------- | ---------------------------------- | ------------------------------- |
| C2 protocol          | APROTO (compact binary, 14-byte header)            | CCSDS Command/Telemetry packets    | FPP-generated command/telemetry |
| Transport            | TCP (primary), UDP, Unix, serial                   | UDP (CI_LAB/TO_LAB), SB internally | TCP (to GDS)                    |
| Health telemetry     | 48-byte ExecutiveHealthPacket (GET_HEALTH)         | Housekeeping (HK) app              | Health component telemetry      |
| Component inspection | INSPECT command (read any component's data by UID) | Table dump, HK request             | Parameter get, channel read     |
| Ground system        | Python APROTO client (lightweight, scriptable)     | COSMOS, ITOS, or custom            | F Prime GDS (web-based, Python) |
| Latency (LAN)        | < 5 ms round-trip (TCP, measured)                  | ~ 10-50 ms (UDP, app-dependent)    | ~ 10-50 ms (TCP to GDS)         |

### Cross-Platform and Embedded Support

| Feature              | Apex                                                     | cFS                             | F Prime                        |
| -------------------- | -------------------------------------------------------- | ------------------------------- | ------------------------------ |
| POSIX (Linux/macOS)  | Full support (ApexExecutive)                             | Full support (OSAL)             | Full support                   |
| Bare-metal MCU       | McuExecutive (STM32, AVR, Pico, ESP32, C2000, PIC32)    | VxWorks, RTEMS (not bare-metal) | Baremetal scheduler (limited)  |
| Same-source reuse    | Same source compiles for POSIX (shared) and MCU (static) | Separate BSP per platform       | Separate topology per platform |
| GPU (CUDA)           | Native support (schedulable GPU tasks)                   | Not supported                   | Not supported                  |
| Hardware abstraction | HAL layer per platform (UART, SPI, I2C, CAN, Flash)      | OSAL + PSP per platform         | Os abstraction layer           |

### Build System

| Feature           | Apex                                            | cFS                         | F Prime                                 |
| ----------------- | ----------------------------------------------- | --------------------------- | --------------------------------------- |
| Build tool        | CMake + Ninja                                   | CMake + Make                | CMake + Ninja + FPP + Python            |
| Code generation   | apex_data_gen (struct dictionaries, optional)   | None (manual CCSDS headers) | FPP compiler (mandatory, generates C++) |
| Cross-compile     | Docker-based toolchains (one command)           | Manual toolchain setup      | fprime-util + ARM toolchains            |
| Package/deploy    | pkg_resolve.sh (auto ELF dependency resolution) | Manual file staging         | fprime-util install                     |
| Language standard | C++23                                           | C99                         | C++11 (generated code)                  |
| Test framework    | Google Test (integrated, parallel + serial)     | UT Assert (cFS-provided)    | Google Test (via fprime-util)           |

## Deep Dive: Internal Message Bus

Each framework uses a fundamentally different approach to inter-component
communication. This section traces a message through each framework's
internal bus to illustrate the architectural differences.

### Apex: Lock-Free Queue Bus

Apex uses a pre-allocated buffer pool with lock-free queues. Components
communicate through `IInternalBus`, an abstraction implemented by
`ApexInterface`. APROTO wire encoding exists only at the external TCP
boundary; internal messages carry metadata in struct fields, not serialized
headers.

**Architecture:**

```
BufferPool (128 pre-allocated MessageBuffers, 4096 bytes each)
    |
    | acquire() [lock-free pop from free list]
    v
MessageBuffer {
  data[4096]            // payload bytes
  fullUid               // component address
  opcode                // operation code
  sequence              // for ACK correlation
  internalOrigin        // true=internal, false=from TCP
  refCount (atomic)     // for zero-copy multicast
}
    |
    | pointer pushed to destination queue (8 bytes, not the buffer)
    v
Per-Component Queues:
  cmdInbox  [MPMC lock-free, 32 entries]  <-- commands from bus/external
  tlmOutbox [SPSC lock-free, 64 entries]  --> telemetry to external
```

**Point-to-point command flow:**

```
Sender component:
  bus->postInternalCommand(srcUid, dstUid, opcode, payload)
    |
    |-- queueMgr_.get(dstUid)           O(1) hash lookup
    |-- bufferPool_.acquire(len)         lock-free pop
    |-- memcpy(buf->data, payload)       one copy
    |-- buf->opcode = opcode             metadata in struct fields
    |-- cmdInbox.tryPush(buf)            8-byte pointer, lock-free
    |
    v [next scheduler tick]
  drainCommandsToComponents()
    |-- cmdInbox.tryPop(buf)             lock-free pop
    |-- comp->handleCommand(opcode, span{buf->data, buf->length})
    |-- bufferPool_.release(buf)         lock-free push back to free list
```

**Zero-copy multicast flow:**

```
bus->postMulticastCommand(srcUid, {dst1, dst2, dst3, dst4}, opcode, payload)
    |
    |-- acquire ONE buffer
    |-- copy payload ONCE
    |-- buf->setRefCount(4)              atomic store
    |-- dst1.cmdInbox.tryPush(buf)       same pointer
    |-- dst2.cmdInbox.tryPush(buf)       same pointer
    |-- dst3.cmdInbox.tryPush(buf)       same pointer
    |-- dst4.cmdInbox.tryPush(buf)       same pointer
    |
    v [each recipient processes independently]
    dst1: handleCommand(), release(buf)  refCount 4->3
    dst2: handleCommand(), release(buf)  refCount 3->2
    dst3: handleCommand(), release(buf)  refCount 2->1
    dst4: handleCommand(), release(buf)  refCount 1->0, returned to pool
```

**External RX path (TCP -> component):**

```
TCP bytes -> SLIP/COBS decode -> APROTO parse -> routeToComponent()
    |
    |-- acquire buffer from pool
    |-- copy payload (strip APROTO header)
    |-- set metadata from APROTO header (fullUid, opcode, sequence)
    |-- buf->internalOrigin = false
    |-- cmdInbox.tryPush(buf)
    |-- immediate ACK queued (command accepted, not yet processed)
    |
    v [fallback if queue full]
    |-- comp->handleCommand() synchronously
    |-- response sent inline
```

**Measured latency (x86_64, clang-21, -O2):**

| Operation              | Latency  | Throughput  | Jitter  |
| ---------------------- | -------- | ----------- | ------- |
| Buffer acquire/release | 0.077 us | 13.1M ops/s | 4.2% CV |
| SPSC push/pop          | 0.090 us | 11.1M ops/s | 2.3% CV |
| MPMC push/pop          | 0.111 us | 9.0M ops/s  | 4.9% CV |
| Post internal command  | 0.127 us | 7.2M ops/s  | 1.6% CV |
| Multicast (4 targets)  | 0.497 us | 2.0M ops/s  | 1.0% CV |
| QueueManager lookup    | 0.057 us | 17.6M ops/s | 1.1% CV |

**Key design properties:**

- All hot-path operations are RT-safe (no malloc, no mutex, no syscall)
- Pre-allocated pool eliminates allocation jitter
- Protocol-agnostic internally; swap wire protocol without touching bus
- Statistics counters track overflow, routing failures, throughput

### cFS: Software Bus (Publish-Subscribe)

cFS uses a publish-subscribe model where messages are routed by Message ID
through a centralized Software Bus. Every message is a CCSDS packet, even
internal ones.

**Architecture:**

```
Software Bus (SB)
    |
    |-- Routing Table: MsgId -> [Pipe0, Pipe1, ..., PipeN]
    |-- Each app owns one or more Pipes (FIFO queues)
    |-- Messages are CCSDS packets with 8-byte primary header
    |
    v
Per-App Pipe (fixed-depth FIFO, e.g., 64 messages)
```

**Publish flow:**

```
Publisher app:
  CFE_SB_TransmitMsg(msgPtr, incrementSequence=true)
    |
    |-- Extract MsgId from CCSDS primary header
    |-- Routing table lookup: MsgId -> subscriber list
    |-- For each subscriber pipe:
    |     if (copy mode) memcpy into pipe buffer
    |     else push pointer
    |-- Return (asynchronous delivery)
    |
    v [each subscriber independently]
  Subscriber app:
    CFE_SB_ReceiveBuffer(&bufPtr, pipeId, CFE_SB_PEND_FOREVER)
    |-- Blocks on pipe semaphore until message arrives
    |-- Returns pointer to message buffer
    |-- App dispatches based on MsgId
```

**Key characteristics:**

- True M:N pub-sub: multiple publishers, multiple subscribers per MsgId
- Adding a subscriber requires no changes to the publisher
- CCSDS headers present on every message (even internal), adding 8+ bytes overhead
- Pipe overflow drops messages (configurable: drop newest or oldest)
- Route configuration is static after startup (subscribe during app init)
- Copy semantics by default; zero-copy optional but less common

**Differences from Apex:**

- cFS routes by **message type** (MsgId); Apex routes by **destination** (fullUid)
- cFS uses semaphore-based blocking; Apex uses lock-free queues
- cFS serializes CCSDS headers on every internal message; Apex uses struct fields
- cFS subscriber model is implicit (subscribe and forget); Apex is explicit
  (caller specifies destination)

### F Prime: Typed Port Invocation

F Prime uses compile-time wired port connections. Communication is a direct
function call for passive components, or a queued message for active
components.

**Architecture:**

```
FPP Topology Definition (compile-time wiring):
  componentA.dataOut -> componentB.dataIn
  componentA.dataOut -> componentC.dataIn   (fan-out)

Code generation produces:
  componentA::dataOut_out(port, data)
    -> componentB::dataIn_handler(data)     (direct call if passive)
    -> componentC::dataIn_handler(data)     (queued if active)
```

**Passive component (synchronous, zero overhead):**

```
ComponentA calls output port:
  this->dataOut_out(0, sensorData)
    |
    |-- Generated code: direct function call to connected handler
    |-- ComponentB::dataIn_handler(sensorData)
    |-- Executes in caller's thread context
    |-- Returns to caller
    |
    v (zero queuing, zero serialization, sub-microsecond)
```

**Active component (asynchronous, queued):**

```
ComponentA calls output port:
  this->dataOut_out(0, sensorData)
    |
    |-- Generated code serializes sensorData into message buffer
    |-- Message pushed to ComponentB's internal queue
    |-- ComponentB's thread dequeues and dispatches
    |-- ComponentB::dataIn_handler(sensorData)  [deserialized]
    |
    v (serialization + queue + deserialization overhead)
```

**Key characteristics:**

- Port connections verified at compile time (type mismatch = build error)
- Passive components have zero IPC overhead (direct call)
- Active components pay serialization cost per message
- One OS thread per active component (expensive at high rates)
- No runtime routing changes possible (topology is compiled in)
- FPP generates all dispatch/serialization code automatically

**Why F Prime struggles at 100 Hz:**
Each active component runs its own thread with a blocking message loop.
At 100 Hz, ~22 threads each call nanosleep(10ms), wake, check queue,
process, sleep. The kernel overhead of 22 wake/sleep cycles every 10 ms
dominates. This model works well at 1 Hz (one wake/sleep per second per
thread) but scales poorly with rate.

### Message Bus Summary

| Dimension          | Apex                           | cFS                      | F Prime                         |
| ------------------ | ------------------------------ | ------------------------ | ------------------------------- |
| Routing model      | Explicit destination (fullUid) | Implicit pub-sub (MsgId) | Compile-time wiring (ports)     |
| Internal encoding  | None (struct fields)           | CCSDS headers (8+ bytes) | FPP serialization               |
| Queue type         | Lock-free (MPMC/SPSC)          | Semaphore-based FIFO     | Per-component OS queue          |
| Memory model       | Pre-allocated pool, refcounted | Copy or reference        | Heap-allocated messages         |
| Multicast          | Zero-copy refcounted buffer    | Native pub-sub fanout    | Port fan-out (N copies)         |
| RT-safety          | All ops lock-free              | Platform-dependent       | Depends on component type       |
| Dynamic routing    | Yes (register at runtime)      | Subscribe at init only   | No (compile-time only)          |
| Thread model       | Shared worker pool             | One thread per app       | One thread per active component |
| Overhead at 100 Hz | 0.60% (base)                   | 1.10%                    | 15.25%                          |

## Deep Dive: Runtime Update Mechanisms

The ability to update software on a running system without full redeployment
varies dramatically across frameworks. This section compares each framework's
approach in detail.

### Apex: Three-Tier Update Architecture

Apex provides three levels of runtime update, from least to most disruptive:

**Tier 1: RELOAD_TPRM (parameter hot-reload, zero downtime)**

Updates component configuration without stopping execution. Uses A/B bank
file swap for atomicity.

```
Operator:
  1. Transfer new .tprm file to inactive bank via C2 FILE_BEGIN/CHUNK/END
  2. Send RELOAD_TPRM command (targetUid, filename)

Executive:
  1. Optionally lock component (prevent task execution during reload)
  2. comp->loadTprm(inactiveTprmDir)  -- component reads and validates
  3. fileSystem_.swapBankFile(TPRM_DIR, filename)  -- active<->inactive
  4. Unlock component
  5. ACK to operator

Rollback:
  Send RELOAD_TPRM again -- previous version is now in inactive bank
```

Use cases: Change scheduler rates, update control gains, modify action
engine watchpoint thresholds, adjust telemetry intervals. All without
restart.

**Tier 2: RELOAD_LIBRARY (component hot-swap, brief task gap)**

Replaces a single component's shared library (.so) at runtime. The
component is stopped, swapped, and restarted while the rest of the
system continues running.

```
Operator:
  1. Transfer new .so to inactive bank via C2
  2. Send LOCK_COMPONENT (targetUid)  -- scheduler skips tasks
  3. Send RELOAD_LIBRARY (targetUid)

Executive (10-step sequence):
  1. Verify component exists and is locked
  2. Resolve .so path in inactive bank
  3. dlopen(newSo, RTLD_NOW) + dlsym("apex_create_component")
  4. Validate: new componentId must match old componentId
  5. Transfer instanceIndex from old to new component
  6. new->loadTprm() + new->init()
  7. Archive old component's log file (FIFO, max 5 per component)
  8. scheduler_.replaceComponentTasks(targetUid, *newComp)
     -- rewire function pointers to new .so symbols
     -- verify all expected tasks found in new component
  9. registry_.updateComponent(targetUid, newComp)
  10. fileSystem_.swapBankFile(LIB_DIR, soName)
  11. Auto-unlock on success

Failure at any step:
  Old component remains locked and running
  Operator can retry or unlock to restore service

Critical detail:
  Step 8 (task re-wiring) prevents use-after-free. The scheduler stores
  direct function pointers to task methods. When dlclose unloads the old
  .so, those pointers become dangling. replaceComponentTasks() atomically
  updates all task entries before the old library is unloaded.
```

**Tier 3: RELOAD_EXECUTIVE (full binary swap, process restart)**

Replaces the entire executive binary via execve(). PID is preserved.
Filesystem persists across restart.

```
Operator:
  1. Transfer new binary to inactive bank via C2
  2. Send RELOAD_EXECUTIVE

Executive:
  1. Locate new binary in inactive bank
  2. Swap bank files (active <-> inactive)
  3. Flush all logs
  4. Reconstruct original argv
  5. execve(newBinary, argv)  -- replaces process image in-place
     |
     v [new process starts]
     Reads active_bank marker
     Loads configs from active bank
     Full init sequence (same PID, fresh state)

Failure handling:
  If execve() returns (error), swap banks back to restore known-good
  binary. Log error and NAK to operator.
```

**External watchdog (ApexWatchdog):**

A separate supervisor process that forks the executive and monitors a
heartbeat pipe. Handles crashes and hangs that the executive cannot
self-recover from.

```
ApexWatchdog (parent process)
    |
    |-- fork() + exec(ApexHilDemo, ...)
    |-- Pass write-end of pipe via APEX_WATCHDOG_FD env var
    |-- Poll read-end with timeout (default 10 sec)
    |
    v [executive writes 1 byte per watchdog interval]
    |
    |-- Timeout? kill child, log crash, restart
    |-- Crash (SIGCHLD)? log crash, restart
    |
    |-- After 2 consecutive crashes: restart with safe.tprm
    |-- After 5 consecutive crashes: halt (operator intervention)
    |
    Crash state persisted to watchdog.state file:
      consecutiveCrashes, totalCrashes, totalRestarts, lastCrashEpoch
```

**Swap traceability:**

All runtime updates logged to `core/swap.log` for post-incident audit:

```
LOCK: uid=0x007800 name=HilPlantModel
RELOAD_LIBRARY_BEGIN: uid=0x007800 so=HilPlantModel_0.so
RELOAD_LIBRARY_LOAD_OK: uid=0x007800 componentId=120
RELOAD_LIBRARY_INIT_OK: uid=0x007800
RELOAD_LIBRARY_LOG_ARCHIVED: uid=0x007800 dest=20260315-150100/
RELOAD_LIBRARY_OK: uid=0x007800 tasks=3
```

### cFS: App-Level Restart

cFS supports runtime updates at the application level. The core executive
(cFE) cannot be updated without a full restart.

```
Approach:
  1. Upload new app .so via CFDP (CF app) or CI_LAB
  2. cFE Executive Services: CFE_ES_StopApp("MY_APP")
     -- App's main task exits
     -- SB subscriptions removed
     -- dlclose(app.so)
  3. CFE_ES_StartApp("MY_APP", "/cf/my_app.so", ...)
     -- dlopen(new .so)
     -- App re-initializes (creates pipes, subscribes to SB)
     -- App begins processing

Characteristics:
  - Full app lifecycle restart (init, subscribe, run)
  - SB routing rebuilt on subscription (not preserved across restart)
  - Other apps continue running during the swap
  - No task-level granularity (entire app restarts)
  - Table load/activate for parameter changes (similar to RELOAD_TPRM)
  - Processor Reset for full system restart (similar to RELOAD_EXECUTIVE)
```

### F Prime: Full Redeploy

F Prime's monolithic static binary cannot be partially updated at runtime.

```
Update procedure:
  1. Modify source code
  2. Rebuild (fprime-util build)
  3. Stop running instance (kill or SIGTERM)
  4. Transfer new binary (scp, Svc::FileUplink)
  5. Start new instance

Characteristics:
  - No hot-swap capability
  - Parameters (PrmDb) can be updated at runtime
  - Topology changes require rebuild
  - Component changes require rebuild
  - Simple and predictable (no dlopen complexity)
```

### Runtime Update Summary

| Capability                    | Apex                               | cFS                     | F Prime          |
| ----------------------------- | ---------------------------------- | ----------------------- | ---------------- |
| Parameter update (no restart) | RELOAD_TPRM                        | Table load/activate     | PrmDb update     |
| Single component swap         | RELOAD_LIBRARY (dlopen)            | App stop/start (dlopen) | Not supported    |
| Full binary swap              | RELOAD_EXECUTIVE (execve)          | Processor Reset         | Manual redeploy  |
| External supervisor           | ApexWatchdog (auto-restart)        | cFE ES (app restart)    | None built-in    |
| Rollback mechanism            | A/B bank (previous always on disk) | Manual re-upload        | Manual re-upload |
| Update granularity            | Per-component task-level           | Per-app                 | Whole system     |
| Audit trail                   | swap.log (structured)              | Event messages          | None built-in    |
| Degraded-mode fallback        | Safe TPRM after N crashes          | Limited                 | None             |

### Assessment

**RELOAD_TPRM** is the strongest differentiator. cFS has table load/activate
which is conceptually similar, but Apex's A/B bank swap means the previous
version is always on disk for instant rollback -- no special rollback command,
no operator procedure, just send RELOAD_TPRM again. cFS requires the operator
to re-upload the old table. F Prime's PrmDb only handles simple scalar
parameters, not scheduler rates or action engine rules.

**RELOAD_LIBRARY** is where Apex is genuinely ahead of both. The task pointer
re-wiring (step 8) preserves the component's identity (fullUid, queue pair,
scheduler slots) across the swap. From the rest of the system's perspective,
nothing changed -- same UID, same queue pair, same scheduler slots. In cFS,
the app fully restarts: new pipe IDs, new SB subscriptions, new task. Any
state accumulated by the old app is lost unless manually persisted to a table
or file. F Prime cannot do component-level updates at all.

**RELOAD_EXECUTIVE** with execve is incremental over cFS's Processor Reset.
Both restart the process. Apex's A/B banking makes it safer (known-good binary
always on disk), but the operation itself is similar.

**The watchdog with degraded-mode fallback** (safe.tprm after N crashes) is a
capability neither cFS nor F Prime has out of the box. cFS has Health Service
but it restarts individual apps, not the whole executive with alternate config.

**Where cFS has an edge:** cFS's multi-process app model provides memory
isolation for free. A buggy cFS app can crash without taking down the
executive. In Apex's single-process architecture, a bad component can corrupt
shared memory or crash the whole process. LOCK_COMPONENT mitigates this
operationally but not at the memory-protection level.

### Future Improvements: Best of All Worlds

Several enhancements could combine the strengths of all three frameworks while
keeping the existing Apex update mechanisms intact:

**1. Component sandboxing via separate address spaces**

Run high-risk or third-party components in child processes with IPC over
shared memory or Unix domain sockets. The executive forks a sandbox process,
the component runs in isolation, and the internal bus bridges the process
boundary transparently. A crash in the sandbox kills only that child; the
executive detects it via SIGCHLD and can auto-restart or replace the component.

This brings cFS-style fault isolation to Apex without abandoning the
single-process model for trusted components. Trusted components continue
running in-process with zero IPC overhead; untrusted components pay the
IPC cost but gain crash isolation.

```
Executive (main process)
    |
    |-- Trusted components: in-process (current model, zero-copy)
    |
    |-- Untrusted components: forked sandbox processes
    |     |-- IPC via Unix domain socket or shared memory
    |     |-- Crash detected via SIGCHLD
    |     |-- Auto-restart or RELOAD_LIBRARY into sandbox
    |
    |-- Component interface unchanged (IInternalBus abstraction)
```

**2. Pre-validated library staging with checksum verification**

Before RELOAD_LIBRARY, validate the new .so in a staging area: verify
ELF architecture, check symbol table for required factory symbols, compare
ABI version tags, and compute CRC-32C. This catches bad binaries before
they enter the hot-swap path, reducing the window where a component is
locked but not yet replaced.

```
Staging pipeline:
  1. FILE_TRANSFER -> stage/pending/MyComponent_0.so
  2. VALIDATE_LIBRARY command:
     -- ELF header check (aarch64, shared object)
     -- dlopen in dry-run mode (RTLD_LAZY | RTLD_NOLOAD)
     -- Symbol check: apex_create_component, apex_destroy_component
     -- ABI version tag match
     -- CRC-32C stored in manifest
  3. ACK with validation report
  4. Operator sends RELOAD_LIBRARY (confident it will succeed)
```

**3. State transfer across component swaps**

Add an optional `exportState()` / `importState()` interface to
SystemComponentBase. During RELOAD_LIBRARY, the executive calls
`oldComp->exportState()` to serialize component state into a buffer,
then `newComp->importState(buffer)` after init. Components opt in by
overriding these methods. This preserves accumulated state (counters,
filter history, calibration data) across swaps.

```cpp
// Optional override on SystemComponentBase
virtual bool exportState(std::vector<uint8_t>& out) noexcept { return false; }
virtual bool importState(rospan<uint8_t> in) noexcept { return false; }
```

cFS loses state on app restart because the app fully reinitializes. This
would give Apex stateful hot-swap -- something neither cFS nor F Prime
offers.

**4. Differential TPRM updates**

Instead of transferring and loading entire TPRM files, support patching
individual fields within a TPRM. The C2 command specifies byte offset,
length, and new value. The executive patches the active TPRM in-place
(with A/B atomicity). Useful for tuning a single gain or threshold
without transferring the entire component configuration over a slow link.

**5. Message-pump scheduling (cFS-style, alongside direct dispatch)**

Add a ScheduledMessagePump component that walks a TPRM-driven table of
(targetUid, opcode, rateDivisor) entries on each tick and posts wakeup
messages to components via the internal bus. Receiving components handle
the message in their existing handleCommand() -- they don't need to know
they're being "scheduled" this way.

This enables two scheduling paradigms coexisting in the same executive:
hard real-time tasks (plant model, flight controller, driver) stay on the
direct-dispatch scheduler with SCHED_FIFO, core affinity, and deadline
detection. Housekeeping, telemetry aggregation, and monitoring components
get woken via bus messages from the pump -- loosely coupled, no function
pointer binding, and adding a new consumer is a TPRM change (no recompile).

```cpp
// ~50 lines on top of existing infrastructure
class ScheduledMessagePump : public CoreComponentBase {
  // TPRM table: [{ targetUid, opcode, rateDivisor }]
  //
  // tick(cycle):
  //   for each entry:
  //     if (cycle % entry.rateDivisor == 0)
  //       bus->postInternalCommand(fullUid(), entry.targetUid, entry.opcode, {})
};
```

This borrows cFS's best idea (schedule/execution decoupling via message
publishing) without giving up deterministic direct dispatch for the tasks
that need it. The pump's table is TPRM-driven, so adding or removing
message-scheduled components is a runtime RELOAD_TPRM operation.

**6. Component health scoring for autonomous decisions**

Extend the watchdog model to track per-component health scores based on
error rates, overrun frequency, and watchpoint fire counts. When a
component's score degrades below a threshold, the executive can
autonomously lock it, fall back to a safe TPRM, or trigger a
RELOAD_LIBRARY from a known-good bank -- without operator intervention.
This brings cFS Health Service concepts into Apex's more granular
component model.

## Unique Capabilities

### Apex Only

- **TPRM hot-reload**: Change scheduler rates, component parameters, and action
  engine rules at runtime without restart. Binary config files transferred over
  C2, loaded atomically.
- **Library hot-swap**: Replace a shared library (.so) at runtime via
  RELOAD_LIBRARY command. The executive unloads the old library, loads the new
  one, and re-registers the component. No process restart.
- **Bank A/B binary swap**: Two complete binary sets on disk. RELOAD_EXECUTIVE
  switches banks and restarts via execve (same PID, clean re-init).
- **GPU task scheduling**: CUDA kernels registered as schedulable tasks,
  dispatched by the same scheduler as CPU tasks.
- **Bare-metal + POSIX from same source**: FlightController compiles and runs
  identically on STM32 (McuExecutive, no heap, no OS) and Linux
  (ApexExecutive, full framework). Shared codec and protocol libraries.
- **Action engine**: Declarative watchpoints, sequences, and notifications
  configured via TPRM. No callback code required on the executive.
- **MasterDataProxy**: Data transformation layer for component output buffers.
  Handles endianness conversion and fault injection transparently.
- **Executive watchdog supervisor**: External process monitors the executive,
  auto-restarts on crash or hang (configurable timeout).

### cFS Only

- **CCSDS native**: First-class CCSDS Space Packet Protocol support throughout
  the Software Bus. Every message is a CCSDS packet with APID routing.
- **Software Bus multicast**: Any app can subscribe to any message ID. Adding
  consumers requires no code changes to the publisher.
- **cFE Executive Services**: OS-level app management (start, stop, restart,
  reload) without executive restart. Apps are dynamically loaded shared objects.
- **Flight heritage**: Decades of mission heritage (ISS, Lunar Gateway, CubeSats).
  Extensively tested in space environments.
- **CFDP file transfer**: CCSDS File Delivery Protocol (CF app) for reliable
  file transfer over unreliable links.
- **VxWorks/RTEMS support**: OSAL provides certified RTOS support for
  flight-qualified operating systems.

### F Prime Only

- **FPP modeling language**: Domain-specific language for defining components,
  ports, commands, telemetry, events, and parameters. Generates C++ code,
  documentation, and ground system dictionaries from a single source.
- **Topology-driven architecture**: System wiring defined declaratively in FPP
  topology files. Port connections are compile-time verified.
- **Built-in GDS**: Web-based ground data system included. Real-time telemetry
  display, command dispatch, event log, and file management out of the box.
- **Component autocoding**: FPP generates base classes with all port handlers,
  command dispatchers, telemetry channels, and parameter accessors. Developers
  only write the implementation logic.
- **Data products framework**: Built-in support for science data collection,
  prioritized downlink, and catalog management.

## Soak Test Validation

ApexHilDemo ran continuously during all measurements:

| Metric                      | Value                                             |
| --------------------------- | ------------------------------------------------- |
| Duration                    | 287 minutes (4.8 hours)                           |
| Clock cycles completed      | 17,253,287                                        |
| Clock frequency             | 1000 Hz                                           |
| Frame overruns              | 2,640 (0.015%)                                    |
| STM32 frames exchanged      | 862,665 TX / 887,462 RX                           |
| CRC errors                  | 0                                                 |
| Communication loss events   | 0                                                 |
| Watchdog warnings           | 0                                                 |
| Real vs emulated divergence | 0.000000                                          |
| Concurrent processes        | 4 (cFS + F Prime + ApexBenchFull + ApexBenchBase) |

The system maintained zero communication loss and zero CRC errors over nearly
5 hours of continuous operation while four additional framework instances ran
concurrently on the same hardware.

## Methodology Notes

- All five processes ran simultaneously on the same Pi 4. CPU measurements
  reflect real contention for shared resources (cache, memory bus, kernel
  scheduling).
- CPU% measured via /proc/[pid]/stat fields 14+15 (utime + stime) sampled
  over a 10-second window. This captures current CPU usage, not lifetime
  average.
- RSS from /proc/[pid]/status VmRSS field. Reflects actual physical memory
  pages in use.
- Apex builds are release (-O3 -DNDEBUG), cross-compiled with
  aarch64-linux-gnu-g++. cFS and F Prime are native release builds on the Pi.
- cFS sch_lab was configured with TickRate=100. F Prime Ref was modified
  from stock 1 Hz to 100 Hz (TimeInterval changed from 1,0 to 0,10000 in
  Main.cpp) for direct comparison.
- No synthetic load was applied. Each framework runs its stock or near-stock
  workload to represent realistic framework overhead.
