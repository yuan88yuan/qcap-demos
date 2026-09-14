# Dante AVH Pending Queue Soak (2026-09-11)

> Status: removed after the three-path port-probe run showed zero EAGAIN and
> zero queue activity on all paths. The queue only reacts after EAGAIN, so it
> cannot explain that clean-flow result and is no longer in `danteudpsink`.

## Purpose

Validate a bounded retry queue for nonblocking RTP `sendto()` EAGAIN and compare
AVH RTP with the direct mirror path.

## Change Under Test

`zstreamer` `danteudpsink` now retains an RTP packet buffer reference when
`sendto()` returns EAGAIN/EWOULDBLOCK or EINTR.

- FIFO capacity: 128 RTP packets.
- Maximum queue age: 10 ms.
- Queue is flushed before sending the next input RTP packet.
- Full or expired entries are discarded, so the retry mechanism cannot create
  unbounded realtime latency.
- The queue references RTP buffers; it does not clone an encoded video buffer.

The existing TX binary did not print the new `pending-*` sink properties, so
this run cannot quantify successful retries or queue-expiry drops directly.

## Test Setup

- TX: `192.168.190.99`; RX: `192.168.190.101`; Dante netns: `dante_avh`.
- Both boards rebooted, mounted `/mnt/dev` using `mount-dev.sh`, and started
  with `dvr_server` bad-fd count 0 and default `mq/pfifo_fast` qdiscs.
- zstreamer was built with the established command:
  `./scripts/build.sh xlnk2_arm64`.
- TX/RX binaries passed `LD_BIND_NOW=1 --help` before the run.
- RX ran AVH RTP stats and direct RTP stats. TX ran AVH plus a direct mirror to
  UDP port 16491. Both were kept alive by foreground SSH control sessions and
  sent SIGTERM at the end.

## Successful Observations

- TX created a real AVH flow:
  `TX video flow created: idx=0 ch=0 port=16490 to=192.168.190.101`.
- TX reported `flows=1` throughout the data portion of the run.
- Last TX sample:

  | Route | Packets sent | Send errors | EAGAIN | SO_SNDBUF |
  | --- | ---: | ---: | ---: | ---: |
  | AVH 16490 | 62,431 | 15,688 | 15,688 | 212,992 |
  | Direct mirror 16491 | 71,930 | 0 | 0 | 212,992 |

- RX final samples:

  | Route | RTP UDP in | RTP loss |
  | --- | ---: | ---: |
  | AVH | 62,789 | 9,104 |
  | Direct | 71,012 | 2,941 |

The TX/RX statistics are sampled on different clocks and start AVH after flow
creation, so these totals must not be subtracted as exact endpoint pairs.
They do confirm the prior pattern: AVH gets TX EAGAIN while the equal-source
direct mirror gets none, and AVH has substantially higher RTP loss.

## Failures And Limits

- The retry queue's explicit counters (`pending-queued`, `pending-retried`,
  `pending-dropped-expired`, and `pending-dropped-full`) were not emitted by
  the deployed TX test binary. This run does not yet prove how many EAGAIN
  packets were recovered versus expired.
- `send-errors-eagain` counts failed send attempts, not terminal packet loss;
  retrying one queued packet can increment it multiple times.
- The graceful stop remains unsafe with an active flow. After the controlled
  SIGTERM shutdown, both DVR logs entered repeated `Bad file descriptor`
  loops: TX count grew to 55 and RX reached 47. The current 2-second
  `dante_stop()` grace delay did not prevent the DVR stale-fd race.
- No valid direct-path traffic-shaping A/B result exists yet. A stale DVR HTB
  tree previously showed an AVH class ceiling of 200Mbit, but that class had
  sent 0 bytes. It is not evidence that HTB caused the AVH EAGAIN.

## Required Follow-Up

1. Add the `pending-*` properties to `sc6f0-dante-tx` periodic route logging,
   rebuild the existing qcap-demos target, and repeat this controlled soak.
2. Treat a run as clean only if both DVR bad-fd counts remain unchanged after
   TX and RX shutdown.
3. Capture `tc -s class show dev eth1` during a real live AVH flow before
   drawing a conclusion about DVR traffic shaping.

## Pending Queue Counter Run

After adding `pending-*` properties to the TX periodic route log and rebuilding
only `sc6f0-dante-tx`, a fresh-board short soak produced this final AVH TX
sample:

```text
eagain=7246 pending=0 queued=6586 retried=3803 expired=2783 full=660
```

Interpretation:

- The bounded queue **does not reduce EAGAIN occurrence**. `eagain` counts
  failed nonblocking `sendto()` calls; every initial backpressure event still
  produces one, and a failed retry can add another.
- It did recover `3,803 / 6,586 = 57.7%` of packets admitted to the pending
  queue. Those packets would have been dropped immediately by the old sink.
- `2,783` queued packets exceeded the 10 ms latency budget and `660` later
  packets found the 128-packet queue full. These are explicit bounded-queue
  drops, not unbounded latency.
- At the corresponding RX samples, AVH reported loss 4,038 and direct reported
  loss 1,051. Clocks and route start times differ, so these are not an exact
  endpoint-loss calculation.

The TX DVR again entered the bad-fd loop after active-flow shutdown (count 14);
the RX count was still 0 at collection time. The queue retry result is valid,
but shutdown remains unsafe.
