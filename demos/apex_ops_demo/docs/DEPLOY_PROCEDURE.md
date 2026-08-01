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
  sudo ./run.sh </dev/null &>/tmp/opsdemo.log &'

# 6. Verify
ssh kalex@192.168.1.119 'pgrep ApexOpsDemo && sudo ss -tlnp | grep 9000'

# 7. Run checkout
python3 demos/apex_ops_demo/scripts/checkout.py --host 192.168.1.119 \
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
cp -r build/hosted-x86_64-debug/zenith_targets/ApexOpsDemo/* \
  /path/to/zenith/targets/pi-ops-demo/
```

## Multi-Instance (Thor)

For running two instances on the same host (different ports):

```bash
# Instance A: port 9000 (default TPRM)
# Instance B: port 9001 (requires modified interface TPRM)

# The build packs the default master from tprm/tprm.manifest. Instance B
# repacks the build's generated payloads with a port-9001 interface,
# mirroring the manifest's entry set.
TOOLS=build/hosted-x86_64-debug/bin/tools/rust
TPRM=build/hosted-x86_64-debug/demos/apex_ops_demo/exec/tprm
cp demos/apex_ops_demo/tprm/toml/interface.toml /tmp/interface_b.toml
sed -i 's/value = 9000/value = 9001/' /tmp/interface_b.toml
$TOOLS/cfg2bin --config /tmp/interface_b.toml --output /tmp/interface_b.tprm
$TOOLS/tprm_pack pack \
  -e "0x000000:$TPRM/payloads/toml_executive_toml.tprm" \
  -e "0x000100:$TPRM/payloads/toml_scheduler_toml.tprm" \
  -e "0x000400:/tmp/interface_b.tprm" \
  -e "0x000500:$TPRM/payloads/toml_action_toml.tprm" \
  -e "0x00C800:$TPRM/payloads/toml_system_monitor_toml.tprm" \
  -e "0x00D000:$TPRM/payloads/toml_wave_gen_0_toml.tprm" \
  -e "0x00D001:$TPRM/payloads/toml_wave_gen_1_toml.tprm" \
  -o "/tmp/master_b.tprm"

# Deploy to instance B
scp /tmp/master_b.tprm kalex@192.168.1.40:~/c2_demo_b/bank_a/tprm/master.tprm
```
