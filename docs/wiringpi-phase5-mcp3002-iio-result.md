# Phase 5 MCP3002 native-IIO first-read result

## Decision

The exact Raspberry Pi Zero W passed the guarded native-IIO first-read gate on
2026-08-02. All 13 checks passed with zero failures and zero warnings. The
standalone reader accessed each single-ended channel once as the invoking
`pi` user, and the ADC, strip, and service states were unchanged afterward.

This accepts the application's MCP3002 transport and authorizes a separate,
operator-assisted dark/bright capture. It does not approve the existing light
thresholds or the whole application.

## Evidence

| Item | Value |
| --- | --- |
| Source commit | `d47629b0670d0357f7e18842e22e6e3cc6c7ab07` |
| Capture | `oclock-phase5-mcp3002-read-20260802T172253Z-5uZghnGK.tar.gz` |
| Capture SHA-256 | `4cdf247058546e412c32c17ed3f605b3a18a5b9b9cbdac8e197b0d564de9d4ab` |
| Reader SHA-256 | `6c155a61155e9e7c0b94cc8b010d879438bbbf7075d787e15baf242bbe2db260` |
| Channel 0 | 1013 raw |
| Channel 1 | 1016 raw |
| Two-channel wall time | 6,656 microseconds |
| Verifier result | 13 checks passed; 0 failures; 0 warnings |

The adjacent archive checksum matched before extraction, all archive members
used relative non-traversing paths, and a scan found no SSH or private-tailnet
topology markers. Raw evidence remains outside Git.

The full ARM application build at the preceding `673d5a7` implementation
checkpoint succeeded and produced an ARM EABI5 binary linking libgpiod and
libatomic without WiringPi or the legacy ADC source. That build was not wrapped
with a timer, so no duration is claimed. Future Pi builds must record
`/usr/bin/time -p`, the exact target, and whether the build was clean or
incremental.

## Interpretation

The two raw values prove that both IIO attributes are readable independently
and remain in the MCP3002's 10-bit range. Their average is 1014 using the
application's integer averaging rule. This single ambient sample is far above
the existing 360/500 dimming hysteresis thresholds and is consistent with the
earlier observation that automatic dimming never engaged.

It is not enough to distinguish room conditions, sensor orientation, channel
behavior, or a calibration issue. Do not change thresholds from this sample.
The next capture must keep channels separate and record stable uncovered and
covered windows before any threshold decision.

## Safety observations

- The accepted overlay checksum and exact Zero W/Trixie target matched.
- MCP3002 was discovered by Device Tree identity, not an IIO or SPI number.
- Both raw attributes were readable without running the application as root.
- MCP3002 remained bound to `mcp320x`.
- The strip remained unbound.
- `oclock.service` remained inactive.
- Firmware reported no current or historical throttling.
- No GPIO line, SPI binding, boot file, or service state changed.
