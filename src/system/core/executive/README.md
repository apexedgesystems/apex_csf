# Executive Module

The executive is the central coordinator for Apex systems. It owns the scheduler, filesystem, and component registry.

## Overview

| Class           | Purpose                                          |
| --------------- | ------------------------------------------------ |
| `ApexExecutive` | Abstract base class defining executive interface |
| `ApexExecutive` | Default implementation with full RT scheduling   |

## Component Identity System

All components registered with the executive implement the identity interface:

```cpp
class SystemComponentBase {
public:
  // Required overrides (pure virtual)
  [[nodiscard]] virtual std::uint16_t componentId() const noexcept = 0;
  [[nodiscard]] virtual const char* componentName() const noexcept = 0;

  // Auto-assigned during registration
  [[nodiscard]] std::uint8_t instanceIndex() const noexcept;
  [[nodiscard]] std::uint32_t fullUid() const noexcept;  // (componentId << 8) | instanceIndex
};
```

### fullUid Composition

```
fullUid = (componentId << 8) | instanceIndex

Example:
  componentId = 102, instanceIndex = 1
  fullUid = (102 << 8) | 1 = 0x6601
```

### Multi-Instance Support

Components with the same `componentId` AND `componentName` can have multiple instances:

```cpp
PolynomialModel poly1, poly2;
exec.registerComponent(&poly1);  // instanceIndex=0, fullUid=0x6600
exec.registerComponent(&poly2);  // instanceIndex=1, fullUid=0x6601
```

### Collision Detection

| Same componentId | Same componentName | Result                    |
| ---------------- | ------------------ | ------------------------- |
| Yes              | Yes                | OK (multi-instance)       |
| Yes              | No                 | ERROR_COMPONENT_COLLISION |

## Component ID Registry

| Range | Purpose                                       |
| ----- | --------------------------------------------- |
| 0     | Executive (reserved)                          |
| 1-100 | System components (Scheduler=1, FileSystem=2) |
| 101+  | Simulation models                             |

### Assigned IDs

| ID  | Component          |
| --- | ------------------ |
| 0   | Executive          |
| 1   | Scheduler          |
| 2   | FileSystem         |
| 101 | SequencedDemoModel |
| 102 | PolynomialModel    |
| 103 | GravityDemoModel   |

## TPRM Loading

The executive loads a packed tprm file containing configuration for all components:

```
master.tprm
    |
    +-> UID 0: Executive tunables (48 bytes)
    +-> UID 1: Scheduler task config (variable)
    \-> UID 101+: Model tunables (variable)
```

Individual tprms are extracted to `.apex_fs/tprm/` during init.

## Usage

```bash
./ApexDemo \
  --config apps/apex_demo/tprm/master.tprm \
  --archive-path /path/to/output \
  --shutdown-after 5
```

## RT Safety

| Function              | RT-Safe | Notes                       |
| --------------------- | ------- | --------------------------- |
| `init()`              | No      | Allocates, performs I/O     |
| `run()`               | Yes     | Main loop is RT-safe        |
| `shutdown()`          | No      | Joins threads, flushes logs |
| `registerComponent()` | No      | Modifies registry           |

## See Also

- `CLAUDE.md` - Component identity interface details
- `apps/apex_demo/README.md` - Demo application and phases
- `apps/apex_demo/RESUME.md` - Development resume document
