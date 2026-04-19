# ApexActionDemo Deploy Procedure

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
make release APP=ApexActionDemo

# 4. Clean target on Pi
ssh kalex@192.168.1.119 'sudo rm -rf ~/apex_action && mkdir ~/apex_action'

# 5. Deploy RPi package
rsync -a build/release/ApexActionDemo/rpi/ kalex@192.168.1.119:~/apex_action/

# 6. Deploy boot-time RTS sequences
ssh kalex@192.168.1.119 'mkdir -p ~/apex_action/bank_a/rts'
scp apps/apex_action_demo/tprm/rts/*.rts kalex@192.168.1.119:~/apex_action/bank_a/rts/

# 7. Start on Pi
ssh kalex@192.168.1.119 'cd ~/apex_action && \
  sudo ./run.sh ApexActionDemo </dev/null &>/tmp/apex_action.log &'

# 8. Verify
ssh kalex@192.168.1.119 'pgrep ApexActionDemo && sudo ss -tlnp | grep 9000'

# 9. Run checkout
python3 apps/apex_action_demo/scripts/checkout.py --host 192.168.1.119
```

## Zenith Target Generation

After deploying, generate and validate Zenith target configs:

```bash
# Generate struct dicts + target configs
make apex-data-db
make zenith-target APP=ApexActionDemo

# Validate against live target
make zenith-validate APP=ApexActionDemo HOST=192.168.1.119

# Copy to zenith
cp -r build/native-linux-debug/zenith_targets/ApexActionDemo/* \
  /path/to/zenith/targets/pi-action-demo/
```
