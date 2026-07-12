# Ops Demo Application Design

## Purpose

ApexOpsDemo is the primary test vehicle for the Zenith operations interface. It
provides a known-good, self-contained application that exercises every APROTO
capability with rich, plot-friendly telemetry. No hardware dependencies -- runs
as a pure SIL process on any POSIX host or Raspberry Pi.

## Architecture

```
+------------------------------------------------------------------+
|                       ApexOpsDemo (POSIX)                         |
|                                                                  |
|  +--------------------+   +--------------------+                 |
|  |  WaveGenerator     |   |  WaveGenerator     |                 |
|  |  (instance 0)      |   |  (instance 1)      |                 |
|  |  100 Hz step       |   |  100 Hz step       |                 |
|  |  1 Hz telemetry    |   |  1 Hz telemetry    |                 |
|  +--------------------+   +--------------------+                 |
|                                                                  |
|  +--------------------+   +--------------------+                 |
|  |  SystemMonitor     |   |  TestPlugin        |                 |
|  |  (0xC800)          |   |  (0xFA00)          |                 |
|  +--------------------+   +--------------------+                 |
|                                                                  |
|  +--------------------+                                          |
|  |  ApexInterface     |                                          |
|  |  TCP:9000           |                                          |
|  +--------------------+                                          |
+------------------------------------------------------------------+
         ^                                    |
         |          TCP + SLIP + APROTO       |
         +------------------------------------+
         |
   Zenith / checkout.py / AprotoClient
```

## Component Design

### WaveGenerator (componentId=208, SW_MODEL)

Two instances generate independent waveforms. Each has:

- **waveStep task (100 Hz):** Advances phase accumulator, computes waveform
  output based on type selection (sine/square/triangle/sawtooth/composite),
  applies optional noise injection via LCG, and updates peak/RMS statistics.
  RT-safe: bounded float math, no allocation.

- **telemetry task (1 Hz):** Logs current waveform type, frequency, output,
  peaks, and RMS to component log. NOT RT-safe (fmt::format).

- **Three registered data blocks:**

  - TUNABLE_PARAM (32B): frequency, amplitude, waveType, dcOffset, etc.
  - STATE (48B): phase, output, peaks, RMS accumulator, sample count
  - OUTPUT (8B): current output + normalized phase

- **handleCommand (0x0100):** Returns WaveGenHealthTlm (32B) with config
  snapshot and computed statistics.

- **TPRM:** Loaded from `{fullUid:06x}.tprm`. All parameters tunable at
  runtime via RELOAD_TPRM.

### TestPlugin (componentId=250, SW_MODEL)

Generic hot-swap test target. Has a single tick counter task at 10 Hz.
Two .so versions built (v1 and v2) to verify the RELOAD_LIBRARY pipeline.

### SystemMonitor (componentId=200, SUPPORT)

Standard framework component monitoring CPU load, temperature, RAM, and
file descriptors. Configured for RPi4 with 4 Cortex-A72 cores.

## APROTO Capability Coverage

| Capability           | How Exercised                                   |
| -------------------- | ----------------------------------------------- |
| SYS_NOOP             | Connectivity to all 11 components               |
| SYS_PING             | Echo with variable payload                      |
| GET_HEALTH           | Executive, Scheduler, Interface, Action, SysMon |
| INSPECT              | WaveGen STATE, OUTPUT, TUNABLE_PARAM            |
| GET_REGISTRY         | Runtime component self-description              |
| GET_DATA_CATALOG     | Runtime data block enumeration                  |
| CMD_PAUSE/RESUME     | Execution control                               |
| CMD_SLEEP/WAKE       | Clock pause/resume                              |
| CMD_LOCK/UNLOCK      | Scheduler skip during lock                      |
| FILE_BEGIN/CHUNK/END | Chunked file upload                             |
| FILE_GET/READ_CHUNK  | Chunked file download                           |
| FILE_ABORT           | Mid-stream abort + recovery (both directions)   |
| RELOAD_TPRM          | Hot-reload wave parameters                      |
| RELOAD_LIBRARY       | Hot-swap TestPlugin .so                         |
| RELOAD_EXECUTIVE     | Process restart via execve (deferred ACK)       |
| SET_VERBOSITY        | Log level control                               |
| LOAD_RTS/START/STOP  | RTS sequence execution (NOOP sweep, wave ctrl)  |

## Scheduler Configuration

| Task      | Component     | Rate   | Priority |
| --------- | ------------- | ------ | -------- |
| waveStep  | WaveGen #0    | 100 Hz | 127      |
| waveStep  | WaveGen #1    | 100 Hz | 126      |
| telemetry | WaveGen #0    | 1 Hz   | -128     |
| telemetry | WaveGen #1    | 1 Hz   | -128     |
| telemetry | SystemMonitor | 1 Hz   | -128     |
| tick      | TestPlugin    | 10 Hz  | -64      |

## Zenith Target Generation

Target configs for Zenith are auto-generated from the build:

```bash
make zenith-target APP=ApexOpsDemo
```

This produces `app_manifest.json`, `commands.json`, `telemetry.json`, and
`structs/*.json` from the `app_data.toml` manifest and struct dictionaries.
See `HOW_TO_RUN.md` for the full integration workflow.
