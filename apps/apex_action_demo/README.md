# Action Demo

Demonstrates the action engine's observe-and-react pipeline alongside the
DataTransform support component for runtime data mutation.

## Components

| Component     | Type     | Purpose                                    |
| ------------- | -------- | ------------------------------------------ |
| SensorModel   | SW_MODEL | Temperature ramp with overtemp detection   |
| DataTransform | SUPPORT  | Byte-level data mutation via ByteMaskProxy |
| SystemMonitor | SUPPORT  | CPU/memory/FD health monitoring            |

## Scenarios

1. **Watchpoint threshold detection** -- action engine monitors SensorModel
   temperature output and fires events when thresholds are crossed.

2. **Event-driven sequencing** -- event notifications trigger RTS/ATS
   command sequences that send COMMANDs to registered components.

3. **Fault injection via DataTransform** -- ground commands arm transform
   entries targeting sensor output bytes. The apply() task corrupts data
   each cycle, and the action engine detects the corruption via watchpoints.

4. **ARM_CONTROL dynamic reconfiguration** -- actions dynamically arm and
   disarm watchpoints and sequences at runtime.

## Building

```bash
make compose-debug
```

## Running

```bash
./build/native-linux-debug/bin/ApexActionDemo [--fs-root .apex_fs]
```
