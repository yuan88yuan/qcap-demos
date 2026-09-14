# Dante AVH Three-Path Port Probe (2026-09-11)

## Question

Determine whether the DVR HTB classifier for AVH RTP port 16490 is itself the
cause of TX `sendto(EAGAIN)`.

## Implementation

Set `ZST_DANTE_PORT_PROBE_OFFSET=10` on both AVH TX and AVH RX applications.
When a DVR `createFlow` supplies port `P`, the coordinator creates:

```text
TX
encoded H.264 -> one RTP payloader -> tee
                                      -> AVH UDP sink: P
                                      -> probe UDP sink: P + 10

RX
AVH UDP source: P      -> reorder -> normal decode/output path
probe UDP source: P+10 -> reorder -> unlinked drop/statistics-only path
```

The tee sends the same RTP buffer to both sinks. It does not clone encoded video
or independently packetize the probe path. The existing direct mirror remains
a third path on port 16491.

## Live Flow Evidence

The TX DVR supplied this real AVH flow:

```text
TX video flow created: idx=0 ch=0 port=16490 to=192.168.190.101
```

Live `tc filter show dev eth1` showed that only port 16490 is classified to
HTB class `1:20`:

```text
match 0000406a/0000ffff at 20   # UDP destination port 16490
match 00110000/00ff0000 at 8    # UDP
match c0a8be65/ffffffff at 16   # destination 192.168.190.101
flowid 1:20
```

The `P+10` probe uses port 16500 and the direct mirror uses port 16491; both
therefore use the HTB default class rather than `1:20`.

## Results

Final TX sample:

| Path | Port | Packets sent | EAGAIN | Pending queue activity |
| --- | ---: | ---: | ---: | --- |
| AVH | 16490 | 30,389 | 0 | queued/retried/expired/full = 0/0/0/0 |
| AVH probe | 16500 | 30,388 | 0 | queued/retried/expired/full = 0/0/0/0 |
| Direct mirror | 16491 | 35,010 | 0 | not applicable |

The AVH HTB class was active and carried real traffic:

```text
class htb 1:20 rate 200Mbit ceil 200Mbit
Sent 33,710,900 bytes 33,630 pkt (dropped 0, overlimits 11,244)
backlog 0b 0p
```

RX AVH/probe final sample:

| Path | Reorder input | Reorder loss |
| --- | ---: | ---: |
| AVH 16490 | 33,630 | 404 |
| Probe 16500 | 33,600 | 434 |

The RX direct application reported 36,994 reorder input and 4,751 loss. Its
application clock/start time differs from the AVH application, so it is not an
exact endpoint-pair comparison in this run; its TX mirror still had 0 EAGAIN.

## Conclusion

This clean-flow run disproves the narrow hypothesis that the 16490 HTB filter
or class `1:20` inherently causes TX EAGAIN:

- AVH traffic was definitely classified into `1:20`.
- The class had no qdisc drops or backlog.
- AVH 16490, same-RTP probe 16500, and direct 16491 all had 0 TX EAGAIN.
- AVH and probe RX reorder loss were nearly equal.

The earlier EAGAIN-heavy runs therefore depend on another uncontrolled state
or condition. Candidates include a poisoned/stale DVR lifecycle state or a
different flow/configuration state, but this run does not identify which one.

## Remaining Failure

After controlled active-flow SIGTERM shutdown, TX DVR entered the known
`Bad file descriptor` loop (count 17); RX was still 0 when sampled. This keeps
the active-flow shutdown race unresolved.
