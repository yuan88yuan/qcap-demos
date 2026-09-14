# Dante AVH Fresh-Boot Matrix (2026-09-11)

Every sample rebooted both TX and RX, remounted NFS, verified DVR
`Bad file descriptor=0`, then used the same RX ALLEGRO2 decoder and HDMI DRM
output. TX ran for 40 seconds after RX startup. Raw TX, RX, and qdisc logs are
stored beside `results.csv`.

| Topology | Samples | TX EAGAIN | RX loss rate | Mean decoded frames |
| --- | ---: | ---: | ---: | ---: |
| AVH main only (`16490`) | 6 | 42,524 total; 17.3% of send attempts | 18.2% | 30 |
| AVH main + RTP probe (`16490` + `16500`) | 6 | 0 on both branches | main 1.0%; probe 1.0% | 1,695 |
| TX probe, RX main only (`16490` + discarded `16500`) | 3 | 0 on both branches | main 1.7% | 1,529 |
| Pure direct (`16491`) | 6 | 0 | 0.8% | 1,919 |

The AVH main-only and main+probe runs both had the same DVR-installed HTB
classifier: destination `192.168.190.101`, UDP port `16490`, class `1:20`,
rate `200Mbit`. In every captured snapshot the class had zero qdisc drops.
The probe topology additionally sent a same-RTP-buffer branch to `16500`,
which bypassed that port-specific classifier.

The evidence excludes offered video rate, AVH audio traffic, and the presence
of the `16490` HTB classifier as sufficient causes of EAGAIN: the probe
topology sent an additional full RTP branch while retaining AVH audio and
`16490` shaping, yet all six runs had zero EAGAIN. It does not identify the
precise mechanism. The leading testable hypothesis is that the post-payloader
tee changes zstreamer scheduling or upstream backpressure in a way that avoids
the main-only UDP send burst.

## Temporary workaround

Run the TX process with `ZST_DANTE_PORT_PROBE_OFFSET=10`, but do not set that
variable for the RX process. TX therefore sends the post-payloader RTP tee to
main `16490` and probe `16500`; RX binds only main `16490`, and the probe
datagrams are discarded because no receiver listens on `16500`.

Three fresh-boot runs validated this topology. Both TX branches had zero
EAGAIN in every run, RX recorded zero probe packets, and the main decoder had
zero decode errors. This is the temporary baseline for investigating the
separate `allegro video decoder reset unexpected` problem. The workaround
doubles RTP egress, so it must be used only where the available network
bandwidth accommodates the additional stream.
