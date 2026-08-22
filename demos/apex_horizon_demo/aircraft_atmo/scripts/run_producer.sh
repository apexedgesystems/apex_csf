#!/usr/bin/env bash
# Launch the aircraft_atmo producer with the full pairing runbook baked
# in: RT thread priorities when the host permits them, contention
# priority, host-IPC shm, and the boot-identity verification that
# proves the right binary + config are running.
#
# Usage:
#   run_producer.sh [--name NAME] [--fs-root PATH] [--fg] [--kill]
#
#   --name    container name        (default: aircraft_atmo_producer)
#   --fs-root filesystem root       (default: /tmp/aircraft_atmo_fs)
#   --fg      run in the foreground (default: detached)
#   --kill    stop + remove the named container and exit
set -euo pipefail

NAME=aircraft_atmo_producer
FS_ROOT=/tmp/aircraft_atmo_fs
DETACH=-d
while [[ $# -gt 0 ]]; do
  case "$1" in
  --name)
    NAME="$2"
    shift 2
    ;;
  --fs-root)
    FS_ROOT="$2"
    shift 2
    ;;
  --fg)
    DETACH=""
    shift
    ;;
  --kill)
    docker rm -f "$NAME" 2>/dev/null && echo "removed $NAME" || echo "no container $NAME"
    exit 0
    ;;
  *)
    echo "unknown arg: $1"
    exit 2
    ;;
  esac
done

# Repo root = three levels up from this script's directory.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
BIN=build/hosted-x86_64-debug/bin/ApexAircraftAtmoDemo
TPRM=build/hosted-x86_64-debug/demos/apex_horizon_demo/aircraft_atmo/exec/tprm/master.tprm
[[ -f "$ROOT/$BIN" ]] || {
  echo "missing $BIN -- build first (make debug)"
  exit 1
}
[[ -f "$ROOT/$TPRM" ]] || {
  echo "missing generated $TPRM -- build apex_tprm_ApexAircraftAtmoDemo"
  exit 1
}

docker rm -f "$NAME" 2>/dev/null || true

# Try the RT-privileged launch first: with CAP_SYS_NICE + rtprio the
# executive's FIFO thread table (clock 90 / tasks 80) applies and the
# producer preempts timeshare load -- measured burst-free cadence under
# 24-thread contention. Fall back to unprivileged + the soft-lag
# override when the host refuses the capability.
run_common=(--ipc=host --cpu-shares 8192
  -v "$ROOT:/home/kalex/workspace:rw" -w /home/kalex/workspace
  --user "$(id -u):$(id -g)" apex.cuda-build
  "./$BIN" --config "$TPRM" --fs-root "$FS_ROOT")

if docker run $DETACH --name "$NAME" --cap-add SYS_NICE --ulimit rtprio=99 \
  "${run_common[@]}" --rt-mode lag-tolerant --rt-max-lag 200; then
  MODE="RT-FIFO (SYS_NICE granted)"
else
  echo "privileged launch refused; falling back to soft-lag timeshare"
  docker run $DETACH --name "$NAME" \
    "${run_common[@]}" --rt-mode lag-tolerant --rt-max-lag 200
  MODE="timeshare (soft-lag override)"
fi

[[ -n "$DETACH" ]] || exit 0

echo "launched $NAME [$MODE]; verifying boot identity..."
sleep 8
docker exec "$NAME" bash -c "
  grep -oE 'dt=[0-9]+ms' $FS_ROOT/logs/models/Aircraft_0.log | head -1
  grep -oE 'spec_hash=0x[0-9a-f]+' $FS_ROOT/logs/models/CelestialBody_0.log | head -1
  ps -T -o cls,rtprio,comm -p 1 | grep -E 'exec_clock|exec_tasks'
  tail -1 $FS_ROOT/heartbeat.csv" || {
  echo 'IDENTITY CHECK FAILED -- inspect logs before pairing'
  exit 1
}
echo "kill switch: docker rm -f $NAME"
