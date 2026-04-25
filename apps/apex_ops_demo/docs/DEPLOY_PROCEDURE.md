# ApexOpsDemo Deploy Procedure

End-to-end build, test, release, and deploy for Raspberry Pi.

## Prerequisites

- Pi: `kalex@192.168.1.119` (Pi 4, PREEMPT kernel)
- Docker Compose environment configured

## Procedure

```bash
# 1. Build native debug (from distclean if needed)
make compose-debug

# 2. Run all tests
make compose-testp

# 3. Build release (rpi, package, tarball)
make release APP=ApexOpsDemo

# 4. Deploy to Pi (clean install from tarball)
scp build/release/ApexOpsDemo.tar.gz kalex@192.168.1.119:~/ApexOpsDemo.tar.gz
ssh kalex@192.168.1.119 'sudo rm -rf ~/apex_c2_demo && mkdir ~/apex_c2_demo && \
  tar xzf ~/ApexOpsDemo.tar.gz -C ~/apex_c2_demo --strip-components=1 && \
  rm ~/ApexOpsDemo.tar.gz'

# 5. Start on Pi
ssh kalex@192.168.1.119 'cd ~/apex_c2_demo/rpi && \
  sudo ./run.sh ApexOpsDemo </dev/null &>/tmp/opsdemo.log &'

# 6. Verify
ssh kalex@192.168.1.119 'pgrep ApexOpsDemo && sudo ss -tlnp | grep 9000'

# 7. Run checkout
python3 apps/apex_ops_demo/scripts/checkout.py --host 192.168.1.119 \
  --skip-restart --skip-reload-lib
```

## Zenith Target Generation

After deploying, generate and validate Zenith target configs:

```bash
# Generate struct dicts + target configs
make apex-data-db
make zenith-target APP=ApexOpsDemo

# Validate against live target
make zenith-validate APP=ApexOpsDemo HOST=192.168.1.119

# Copy to zenith
cp -r build/native-linux-debug/zenith_targets/ApexOpsDemo/* \
  /path/to/zenith/targets/pi-ops-demo/
```

## Multi-Instance (Thor)

For running two instances on the same host (different ports):

```bash
# Instance A: port 9000 (default TPRM)
# Instance B: port 9001 (requires modified interface TPRM)

# Build port-9001 TPRM for instance B
TOOLS=build/native-linux-debug/bin/tools/rust
cp apps/apex_ops_demo/tprm/toml/interface.toml /tmp/interface_b.toml
sed -i 's/value = 9000/value = 9001/' /tmp/interface_b.toml
$TOOLS/cfg2bin --config /tmp/interface_b.toml --output /tmp/interface_b.tprm
$TOOLS/tprm_pack pack \
  -e "0x000000:apps/apex_ops_demo/tprm/executive.tprm" \
  -e "0x000100:apps/apex_ops_demo/tprm/scheduler.tprm" \
  -e "0x000400:/tmp/interface_b.tprm" \
  -e "0x00D000:apps/apex_ops_demo/tprm/wave_gen_0.tprm" \
  -e "0x00D001:apps/apex_ops_demo/tprm/wave_gen_1.tprm" \
  -e "0x00C800:apps/apex_ops_demo/tprm/system_monitor.tprm" \
  -o "/tmp/master_b.tprm"

# Deploy to instance B
scp /tmp/master_b.tprm kalex@192.168.1.40:~/c2_demo_b/bank_a/tprm/master.tprm
```
