# ApexSpecDemo Deploy Procedure

End-to-end build, test, release, and deploy for Raspberry Pi.

## Prerequisites

- Pi: `kalex@raspberrypi.local` (Pi 4; the DHCP address moves -- resolve via mDNS)
- Docker Compose environment configured

## Procedure

```bash
# 1. Build native debug (from distclean if needed)
make compose-debug

# 2. Run all tests
make compose-testp

# 3. Build release (rpi, package, tarball)
make release APP=ApexSpecDemo

# 4. Deploy to Pi (clean install from tarball)
scp build/release/ApexSpecDemo.tar.gz kalex@raspberrypi.local:~/ApexSpecDemo.tar.gz
ssh kalex@raspberrypi.local 'sudo rm -rf ~/apex_spec_demo && mkdir ~/apex_spec_demo && \
  tar xzf ~/ApexSpecDemo.tar.gz -C ~/apex_spec_demo --strip-components=1 && \
  rm ~/ApexSpecDemo.tar.gz'

# 5. Start on Pi
ssh kalex@raspberrypi.local 'cd ~/apex_spec_demo/rpi && \
  sudo ./run.sh </dev/null &>/tmp/specdemo.log &'

# 6. Verify
ssh kalex@raspberrypi.local 'pgrep ApexSpecDemo && sudo ss -tlnp | grep 9000'

# 7. Run checkout
python3 demos/apex_spec_demo/scripts/checkout.py --host raspberrypi.local
```

## Zenith Target Generation

After deploying, generate and validate Zenith target configs:

```bash
# Generate struct dicts + target configs
make apex-data-db
make zenith-target APP=ApexSpecDemo

# Validate against live target
make zenith-validate APP=ApexSpecDemo HOST=raspberrypi.local

# Copy to zenith
cp -r build/hosted-x86_64-debug/zenith_targets/ApexSpecDemo/* \
  /path/to/zenith/targets/pi-spec-demo/
```
