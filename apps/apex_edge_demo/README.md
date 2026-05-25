# Edge Demo

Edge compute GPU demonstration. Runs heavy CUDA workloads under
ApexExecutive to prove RT scheduling stability under GPU saturation.
Configuration is fully data-driven via TPRM files, with deploy and
release pipelines for cross-compiling to NVIDIA Jetson AGX Thor.

## Building

```bash
make compose-debug                       # native with CUDA
cmake --preset cross-jetson-release      # cross-compile for Thor
```

## Running

```bash
./build/hosted-x86_64-debug/bin/ApexEdgeDemo \
  --config apps/apex_edge_demo/tprm/master.tprm --shutdown-after 10
```

## See Also

- [docs/EDGE_DESIGN.md](docs/EDGE_DESIGN.md) -- internals: GPU workload
  composition and RT scheduling proof.
- [docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md) -- step-by-step run guide.
- [docs/RESULTS.md](docs/RESULTS.md) -- expected scheduling latency and
  GPU utilization figures.
- [docs/DEPLOY_PROCEDURE.md](docs/DEPLOY_PROCEDURE.md) -- Jetson Thor
  packaging and on-target deployment notes.
- [tprm/](tprm/) -- TPRM configuration files for each scenario.
