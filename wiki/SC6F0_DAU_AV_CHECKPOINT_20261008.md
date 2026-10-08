# SC6F0 DAU diagnostics / SDI2 Ultra AV checkpoint

2026-10-08: user accepts correct SDI2 Ultra picture as an intermediate milestone
and requests incremental commits rather than waiting for all future work.
Firmware source checkpoint: sc6f0-dante fix/sdi2-ultra-format `2f8c476d0716`.

This App change adds DAU method/format/video-publication diagnostics only. It
does not change AudioChange publication policy, format fields, source start/stop,
HDMI/SDI mode selection, S16 audio or original AES3 fanout. No late-client replay,
GetCableStatus-triggered audio notification or extra CLI option is included.
The experimental replay workaround and its dedicated files were removed and
were never deployed. Diagnostic logs can add runtime load; this is not a proof
of timing equivalence for every HDMI/SDI input format.

Installed measured App SHA-256:
`d5d885099589c74e10fe347a7a6b4c017d6f7998ce65c9b2067736fb8bb8afd6`.
Successful normal SDI2 coldboots have user-confirmed correct picture/audio and
zero App service restarts. Boot 63f7863c-94a0-461f-90cd-9eb74d6a78d1 logs three
natural GetAudioInfo requests returning stereo/S16/48kHz and Ultra ASRC forwarding.
Existing binaries predate this commit; no clean rebuild from this commit is
claimed. Firmware notes preserve detailed source/artifact provenance.

Known late-Ultra silence and intermittent HDMI RX black output are separately
user-deferred, non-blocking issues, not repaired by these diagnostics. Metadata
presence/channel/rate parity, full image and lifecycle/format/CDC release work
continue as later milestones. This commit is not production release signoff.
