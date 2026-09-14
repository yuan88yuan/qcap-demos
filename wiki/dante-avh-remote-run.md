# Dante AVH Remote Runbook

Use this runbook for TX/RX RTP tests on SC6F0 boards.

## Hosts

```text
TX: petalinux@sc6f0-060f00112334.local  (192.168.190.99)
RX: petalinux@sc6f0-060f00112336.local  (192.168.190.101)
Netns: dante_avh
```

## Required Preparation

Run after every board reboot:

```bash
ssh petalinux@sc6f0-060f00112334.local 'sudo -n /opt/qcap/bin/mount-dev.sh; mkdir -p /tmp/soak'
ssh petalinux@sc6f0-060f00112336.local 'sudo -n /opt/qcap/bin/mount-dev.sh; mkdir -p /tmp/soak'
```

The NFS paths used by the applications are:

```text
/mnt/dev/zzlee/docker/qcap-demos/_objs/xlnk2_arm64/bin/
/mnt/dev/zzlee/docker/zstreamer/build-xlnk2_arm64/src/
/mnt/dev/zzlee/docker/qcap-dev/qcap/build-qcap/_objs/xlnk2_arm64/lib/
```

Check that both DVRs are clean before a flow run:

```bash
ssh petalinux@sc6f0-060f00112334.local 'grep -c "Bad file descriptor" /var/log/dante_dvrserver.log'
ssh petalinux@sc6f0-060f00112336.local 'grep -c "Bad file descriptor" /var/log/dante_dvrserver.log'
```

Both counts must be `0` after a reboot.

## Working TX/RX Command

Run this **as one local shell command** from the repository root. It keeps both
SSH sessions open while their children run, then sends SIGTERM and waits for
normal process exit. This is the only launch method validated to keep both
applications alive in this environment.

```bash
ssh -o ConnectTimeout=6 petalinux@sc6f0-060f00112336.local '
  rm -f /tmp/soak/rx-avh.log /tmp/soak/rx-direct.log
  avh=; direct=
  cleanup() {
    [ -n "$avh" ] && kill -TERM "$avh" 2>/dev/null
    [ -n "$direct" ] && kill -TERM "$direct" 2>/dev/null
    wait "$avh" 2>/dev/null
    wait "$direct" 2>/dev/null
  }
  trap cleanup EXIT HUP INT TERM
  sudo -n ip netns exec dante_avh env \
    LD_LIBRARY_PATH=/mnt/dev/zzlee/docker/zstreamer/build-xlnk2_arm64/src/:/mnt/dev/zzlee/docker/qcap-dev/qcap/build-qcap/_objs/xlnk2_arm64/lib/ \
    /mnt/dev/zzlee/docker/qcap-demos/_objs/xlnk2_arm64/bin/sc6f0-dante-rx \
    --rtp-stats-only --interval 5 > /tmp/soak/rx-avh.log 2>&1 &
  avh=$!
  sudo -n ip netns exec dante_avh env \
    LD_LIBRARY_PATH=/mnt/dev/zzlee/docker/zstreamer/build-xlnk2_arm64/src/:/mnt/dev/zzlee/docker/qcap-dev/qcap/build-qcap/_objs/xlnk2_arm64/lib/ \
    /mnt/dev/zzlee/docker/qcap-demos/_objs/xlnk2_arm64/bin/sc6f0-dante-rx \
    --rtp-stats-only --direct 192.168.190.99 --local-ip 192.168.190.101 \
    --port 16491 --interval 5 > /tmp/soak/rx-direct.log 2>&1 &
  direct=$!
  sleep 70
' > /tmp/opencode/rx-control.log 2>&1 &
rx_ssh=$!
sleep 5
ssh -o ConnectTimeout=6 petalinux@sc6f0-060f00112334.local '
  rm -f /tmp/soak/tx.log
  tx=
  cleanup() {
    [ -n "$tx" ] && kill -TERM "$tx" 2>/dev/null
    wait "$tx" 2>/dev/null
  }
  trap cleanup EXIT HUP INT TERM
  sudo -n ip netns exec dante_avh env \
    LD_LIBRARY_PATH=/mnt/dev/zzlee/docker/zstreamer/build-xlnk2_arm64/src/:/mnt/dev/zzlee/docker/qcap-dev/qcap/build-qcap/_objs/xlnk2_arm64/lib/ \
    /mnt/dev/zzlee/docker/qcap-demos/_objs/xlnk2_arm64/bin/sc6f0-dante-tx \
    --direct-mirror 192.168.190.101 --mirror-port 16491 > /tmp/soak/tx.log 2>&1 &
  tx=$!
  sleep 60
' > /tmp/opencode/tx-control.log 2>&1 &
tx_ssh=$!
wait "$tx_ssh"
wait "$rx_ssh"
```

The logs are written to:

```text
TX: /tmp/soak/tx.log
RX AVH: /tmp/soak/rx-avh.log
RX direct: /tmp/soak/rx-direct.log
Local SSH control: /tmp/opencode/{tx,rx}-control.log
```

## Do Not Use

Do not launch `sc6f0-dante-tx` or `sc6f0-dante-rx` through detached `setsid`,
`screen`, `nohup`, or a short-lived SSH command in this environment. Those
methods caused the child process to exit when its SSH launcher ended; if it had
connected to DVR, the abrupt socket close poisoned DVR with repeated
`Bad file descriptor` errors.

## Current Shutdown Limitation

The foreground control command sends normal SIGTERM and waits, but the 2026-09-11
soak still wedged both DVRs after an active-flow shutdown. Treat every completed
active-flow run as requiring a DVR-health check, and reboot a board if its
bad-fd count increases. Do not assume the current grace-delay patch prevents the
DVR stale-fd race.
