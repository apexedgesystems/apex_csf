# FreeRTOS Integration Notes

## Motivation

The bare-metal `stm32_encryptor_demo` uses McuExecutive with Stm32SysTickSource
for cooperative scheduling. This works well for simple workloads but does not
exercise FreeRTOS, which is needed for:

- Complex workloads requiring preemption between independent subsystems
- Third-party middleware (lwIP, USB stacks) that expects an RTOS
- Multi-task architectures where the executive is one of several FreeRTOS tasks

FreeRTOS support is integrated into `stm32_encryptor_demo` via the `APEX_USE_FREERTOS`
compile flag (CMake option). When enabled, the same application code runs the
executive inside a FreeRTOS task using FreeRtosTickSource. The command protocol
and serial_checkout.py are identical in both modes.

## Architecture: Option 3 (McuExecutive + FreeRtosTickSource)

### What changes

| Layer             | Bare-metal                         | FreeRTOS                                   |
| ----------------- | ---------------------------------- | ------------------------------------------ |
| Tick source       | Stm32SysTickSource (SysTick + WFI) | FreeRtosTickSource (vTaskDelayUntil)       |
| Main loop         | McuExecutive::run() in main()     | McuExecutive::run() in FreeRTOS task      |
| SysTick ownership | Shared: HAL + tick source          | FreeRTOS kernel                            |
| HAL timebase      | SysTick (shared)                   | SysTick (via FreeRTOS xTaskGetTickCount)   |
| Context switch    | None (single-threaded)             | FreeRTOS PendSV (preemptive between tasks) |
| Idle behavior     | WFI in tick source                 | FreeRTOS idle task (configUSE_IDLE_HOOK)   |

### What stays the same

- SchedulerLite priorities (127, 0, -128) still control execution order within a tick
- All application code (EncryptorEngine, CommandDeck, KeyStore, OverheadTracker)
- Command protocol (same opcodes, same serial_checkout.py)
- SLIP + CRC framing on both channels
- AES-256-GCM encryption pipeline

### What you do NOT get (yet)

- FreeRTOS preemption between SchedulerLite tasks (they run cooperatively within
  a single FreeRTOS task)
- FreeRTOS priorities for individual scheduler tasks (the executive task has one
  FreeRTOS priority; SchedulerLite sorts by its own priority field)
- Time-slicing within the executive tick
- Stack isolation between scheduler tasks (they share the executive task stack)

To get real FreeRTOS preemption between tasks, the scheduler would need a new
variant (SchedulerFreeRtos) that maps each task to a FreeRTOS task with its own
stack and priority. That is a larger effort and not needed for the encryptor.

## FreeRTOS Source

FreeRTOS V10.2.1 is bundled with STM32CubeL4 at:

```
/opt/STM32CubeL4/Middlewares/Third_Party/FreeRTOS/Source/
```

No Docker container changes needed. When `APEX_USE_FREERTOS=ON`, the CMakeLists.txt
conditionally adds these sources, same pattern as the HAL sources.

### Kernel sources used

| File                         | Purpose                                        |
| ---------------------------- | ---------------------------------------------- |
| tasks.c                      | Task creation, scheduling, delay functions     |
| list.c                       | Linked list implementation (internal)          |
| queue.c                      | Queue and semaphore primitives                 |
| timers.c                     | Software timer service (disabled but required) |
| portable/GCC/ARM_CM4F/port.c | Cortex-M4F context switching                   |
| portable/MemMang/heap_4.c    | Dynamic memory with coalescence                |

### Memory budget

| Allocation                            | Size   | Source              |
| ------------------------------------- | ------ | ------------------- |
| FreeRTOS heap (configTOTAL_HEAP_SIZE) | 8 KB   | heap_4.c (.bss)     |
| Executive task stack                  | 2 KB   | From FreeRTOS heap  |
| Idle task stack                       | 512 B  | From FreeRTOS heap  |
| FreeRTOS kernel data                  | ~1 KB  | .bss                |
| Total FreeRTOS overhead               | ~12 KB | Fits in 96 KB SRAM1 |

## SysTick Ownership

FreeRTOS owns SysTick for its kernel tick. The SysTick_Handler calls both
FreeRTOS and HAL:

```cpp
extern "C" void SysTick_Handler() {
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}
```

FreeRTOS runs SysTick at 1 kHz (configTICK_RATE_HZ = 1000). The
FreeRtosTickSource uses `vTaskDelayUntil()` to prescale down to the executive
frequency (100 Hz = delay of 10 ms = 10 FreeRTOS ticks).

## Interrupt Priority Configuration

FreeRTOS on Cortex-M4 requires specific NVIC priority configuration:

| Priority | Assignment                                                |
| -------- | --------------------------------------------------------- |
| 0-4      | Reserved for high-priority ISRs (above FreeRTOS)          |
| 5        | configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY              |
| 5-14     | ISRs that may call FreeRTOS API (UART, etc.)              |
| 15       | configLIBRARY_LOWEST_INTERRUPT_PRIORITY (PendSV, SysTick) |

UART interrupts (USART1, USART2) must have priority >= 5 so they can safely
call FreeRTOS-aware functions if needed. With the bare-metal version they use
the default priority (0), which needs adjustment.

## FreeRtosTickSource

Implements ITickSource using FreeRTOS `vTaskDelayUntil()`:

```
waitForNextTick()  ->  vTaskDelayUntil(&lastWakeTime_, periodTicks_)
currentTick()      ->  xTaskGetTickCount()
tickFrequency()    ->  configured frequency (e.g., 100 Hz)
start()            ->  record initial xTaskGetTickCount()
stop()             ->  set running flag false
```

This is ~30 lines of code. The executive sees no difference between
Stm32SysTickSource and FreeRtosTickSource -- both satisfy ITickSource.

## Size Comparison: Bare-Metal vs FreeRTOS (Option 3)

Both modes were built with `-Os` (size-optimized) for NUCLEO-L476RG (1 MB FLASH,
96 KB SRAM1 + 32 KB SRAM2). Identical application code, same 36/36 serial checkout
checks passing. Build with `APEX_USE_FREERTOS=OFF` (default) for bare-metal,
`APEX_USE_FREERTOS=ON` for FreeRTOS.

### Measured firmware sizes

| Metric                  | Bare-metal       | FreeRTOS (Option 3) | Delta             |
| ----------------------- | ---------------- | ------------------- | ----------------- |
| FLASH (.text + .rodata) | 22,780 B (2.17%) | 26,140 B (2.49%)    | +3,360 B (+14.7%) |
| RAM (.data + .bss)      | 7,456 B (7.58%)  | 15,960 B (16.24%)   | +8,504 B (+114%)  |

### Where the FLASH delta goes (+3,360 B)

| Component                                               | Approximate size |
| ------------------------------------------------------- | ---------------- |
| FreeRTOS kernel (tasks.c, list.c, queue.c)              | ~2,400 B         |
| FreeRTOS port (ARM_CM4F context switch)                 | ~400 B           |
| FreeRTOS timers.c (linked but timer task disabled)      | ~300 B           |
| FreeRtosTickSource (vTaskDelayUntil wrapper)            | ~100 B           |
| SysTick_Handler (dual-dispatch + scheduler state check) | ~60 B            |
| FreeRTOS hooks (stack overflow, malloc failed)          | ~100 B           |

### Where the RAM delta goes (+8,504 B)

| Component                                               | Size     | Notes                                      |
| ------------------------------------------------------- | -------- | ------------------------------------------ |
| FreeRTOS heap (configTOTAL_HEAP_SIZE)                   | 8,192 B  | .bss, heap_4 arena                         |
| FreeRTOS kernel state (ready/delayed lists, tick count) | ~300 B   | .bss                                       |
| Idle task TCB + stack (128 words from heap)             | ~570 B   | Allocated from heap at vTaskStartScheduler |
| Executive task TCB + stack (512 words from heap)        | ~2,100 B | Allocated from heap at xTaskCreate         |

The heap (8,192 B) dominates the RAM cost. After allocating the executive task
(~2,100 B) and idle task (~570 B), approximately 5,500 B of heap remains unused.
Reducing `configTOTAL_HEAP_SIZE` to 4096 would recover 4 KB at the cost of less
headroom for additional FreeRTOS tasks.

### Efficiency verdict

Bare-metal is clearly more efficient for this workload. The FreeRTOS kernel adds
3.4 KB of code and 8.5 KB of RAM for identical functionality. The RAM doubling is
the main concern on small MCUs -- on the L476RG (96 KB SRAM1) it is acceptable,
but on parts with 20-32 KB RAM it would be significant.

The FreeRTOS variant becomes worthwhile when:

- Multiple independent subsystems need preemption (networking + control + UI)
- Third-party middleware requires an RTOS (lwIP, USB device stacks)
- Sleep modes need FreeRTOS tickless idle (configUSE_TICKLESS_IDLE)
- The application outgrows cooperative scheduling

## Pure FreeRTOS Approach (Option 4)

The current FreeRTOS variant (Option 3) wraps the entire McuExecutive in a single
FreeRTOS task. A "pure FreeRTOS" approach would eliminate McuExecutive entirely
and map each logical task to its own FreeRTOS task with independent stack, priority,
and timing.

### Task mapping

| Current (Option 3)                     | Pure FreeRTOS (Option 4)                                |
| -------------------------------------- | ------------------------------------------------------- |
| 1 FreeRTOS task runs McuExecutive     | 4 FreeRTOS tasks (one per function)                     |
| SchedulerLite controls execution order | FreeRTOS priority preemption controls order             |
| Shared stack (2 KB for all tasks)      | Per-task stacks (512 B - 1 KB each)                     |
| Rate dividers (freqN/freqD) set rates  | vTaskDelayUntil per task sets rates                     |
| DWT bracketing via profiler tasks      | FreeRTOS run-time stats (configGENERATE_RUN_TIME_STATS) |

The four tasks would be:

| Task            | Rate   | FreeRTOS Priority | Stack | Notes                        |
| --------------- | ------ | ----------------- | ----- | ---------------------------- |
| dataChannelTask | 100 Hz | 3 (highest app)   | 1 KB  | Encryption + SLIP framing    |
| commandTask     | 20 Hz  | 2                 | 512 B | Command parsing, flash ops   |
| ledBlinkTask    | 2 Hz   | 1 (lowest app)    | 256 B | Single GPIO toggle           |
| statsTask       | 1 Hz   | 1                 | 512 B | Replaces profiler bracketing |

### RAM comparison

| Allocation                    | Option 3 (current)    | Option 4 (pure)        |
| ----------------------------- | --------------------- | ---------------------- |
| FreeRTOS heap                 | 8,192 B               | 10,240 B               |
| Task stacks                   | 2,048 B (1 executive) | 2,304 B (4 tasks)      |
| Task control blocks           | ~140 B (1 TCB + idle) | ~350 B (4 TCBs + idle) |
| Idle task                     | ~570 B                | ~570 B                 |
| McuExecutive + SchedulerLite | ~200 B                | 0 B                    |
| **Total RTOS overhead**       | **~11 KB**            | **~13.5 KB**           |

Pure FreeRTOS uses ~2.5 KB more RAM. Each FreeRTOS task needs its own stack
(minimum usable ~256 B, practical minimum ~512 B) plus a TCB (~70 B). With 4
application tasks, the per-task stack cost exceeds the shared-stack savings from
removing McuExecutive.

### FLASH comparison

| Component                       | Option 3 | Option 4 | Delta       |
| ------------------------------- | -------- | -------- | ----------- |
| FreeRTOS kernel                 | ~3,100 B | ~3,100 B | 0           |
| McuExecutive + SchedulerLite   | ~1,200 B | 0 B      | -1,200 B    |
| vTaskDelayUntil calls (4 tasks) | 0 B      | ~200 B   | +200 B      |
| FreeRTOS run-time stats         | 0 B      | ~300 B   | +300 B      |
| **Net FLASH delta**             | -        | -        | **~-700 B** |

Pure FreeRTOS saves a small amount of FLASH by dropping executive_mcu. The
savings are modest because SchedulerLite is already minimal (~1.2 KB).

### What you gain

1. **True preemption**: dataChannelTask (priority 3) preempts commandTask
   (priority 2) mid-execution. Currently, if commandTask takes a long time
   (e.g., flash erase), it blocks the data channel until the tick completes.

2. **Stack isolation**: A stack overflow in commandTask does not corrupt
   dataChannelTask. FreeRTOS stack overflow detection (configCHECK_FOR_STACK_OVERFLOW)
   catches it per-task.

3. **Independent timing**: Each task runs at its own rate via vTaskDelayUntil.
   No rate divider arithmetic, no shared tick boundary. A slow task delays only
   itself, not the entire tick.

4. **FreeRTOS ecosystem**: Tasks can use queues, semaphores, event groups for
   inter-task communication. Stream buffers could replace raw UART polling.

### What you lose

1. **Deterministic execution order**: SchedulerLite guarantees tasks run in
   priority order within each tick. Pure FreeRTOS runs highest-ready-priority
   first, but equal-priority tasks time-slice (round-robin). The strict
   "profiler start -> work -> profiler end" bracket no longer works.

2. **DWT cycle bracketing**: With cooperative scheduling, the profiler start/end
   tasks measure the exact busy cycles of a tick. With preemptive tasks, there
   is no single "tick" to bracket. FreeRTOS run-time stats (using DWT as the
   timer source) measure per-task CPU time but not per-tick overhead.

3. **RAM efficiency**: 4 independent stacks cost more than 1 shared stack,
   especially when tasks are simple (ledBlinkTask needs only a few bytes of
   stack but must allocate at least configMINIMAL_STACK_SIZE = 128 words).

4. **Portability**: The executive model (ITickSource + SchedulerLite) works on
   any platform. Pure FreeRTOS tasks are FreeRTOS-specific. Moving to a
   different RTOS (Zephyr, ThreadX) requires rewriting the task structure.

5. **Rate accuracy**: SchedulerLite rate dividers are exact integer ratios of
   the fundamental frequency. vTaskDelayUntil has 1-tick (1 ms) granularity.
   100 Hz = 10 ms periods are exact, but odd rates (e.g., 33 Hz) introduce
   jitter.

### When pure FreeRTOS makes sense

The pure approach is justified when the application has tasks with significantly
different execution times or latency requirements that cannot tolerate cooperative
blocking. Examples:

- A control loop at 1 kHz that must not be blocked by a 50 ms flash erase
- A networking stack (lwIP) that needs its own task with large stack
- Tasks that block on I/O (semaphores, queues) rather than polling

For the encryptor, all tasks are fast (single-digit microsecond per tick at idle,
sub-millisecond under load) and the cooperative model works well. The FreeRTOS
variant (Option 3) proves the integration path without the added complexity.

### Potential Phase H: Pure FreeRTOS variant

If pursued, this could be a new compile flag (e.g., `APEX_FREERTOS_PURE`) or a
separate app. Implementation steps:

1. Replace McuExecutive with 4 xTaskCreate calls, each with vTaskDelayUntil
2. Replace DWT bracketing with configGENERATE_RUN_TIME_STATS (DWT as timer)
3. Add OVERHEAD command handler that reads FreeRTOS run-time stats
4. Adjust FreeRTOS heap to accommodate 4 task stacks
5. Validate with serial_checkout.py (same command protocol)

## Build Commands

```bash
# Bare-metal (default)
docker compose run --rm -T dev-stm32 make stm32

# FreeRTOS
docker compose run --rm -T dev-stm32 bash -c \
  'cmake --preset stm32-baremetal -DAPEX_USE_FREERTOS=ON && cmake --build --preset stm32-baremetal -j$(nproc)'
```

## Verification

The same `serial_checkout.py` validates both firmware modes:

```bash
# Same script regardless of build mode -- flash the appropriate .elf first
python3 apps/stm32_encryptor_demo/scripts/serial_checkout.py --verbose
```

All 36 checks should pass in both modes. Overhead numbers will differ
slightly due to FreeRTOS context switch overhead.
