# Phase 5 LPD8806 first-transfer attempt result

## Decision

The 2026-08-02 first-transfer attempt stopped safely before sending a payload.
Linux `spi-gpio` rejected the userspace `SPI_NO_CS` mode flag during spidev
configuration. The strip remained dark, the runtime binding was removed, the
MCP3002 remained on `mcp320x`, the service remained inactive, and firmware
reported no throttling.

This is a configuration failure, not an LPD8806 timing result. It authorizes a
retry after removing `SPI_NO_CS`; it does not count as a successful transfer.

## Evidence

| Item | Value |
| --- | --- |
| Source commit | `c21999d0f203198ce8ca3d01dbb4378caeb2289f` |
| Capture | `oclock-phase5-lpd-transfer-20260802T162012Z-cugE7QsY.tar.gz` |
| Capture SHA-256 | `0221f1415c9be7f39000205d18385c239feb32af02eb70508733b570357259c9` |
| Transfer-tool SHA-256 | `19b38d0f69a3fe365e5983be8645eecb1362e69895c83d6a2893f801d13702aa` |
| Tool result | status 1; no frame metadata emitted |
| Verifier result | 7 expected transfer-dependent failures; rollback checks passed |

The adjacent archive checksum matched before extraction and every member used
a relative, non-traversing path. Raw evidence remains outside Git.

## Root cause

The tool opened the dynamically discovered `/dev/spidev4.0`, then attempted
to configure mode `SPI_MODE_0 | SPI_NO_CS`. The kernel recorded:

```text
spidev spi4.0: setup: unsupported mode bits 40
```

The tool reported `EINVAL` from mode configuration. Because initialization
failed there, `SPI_IOC_MESSAGE(1)` was never called and no frame was sent.

The overlay already declares the strip's dedicated `spi-gpio` controller with
`num-chipselects = <0>`. There is no kernel chip-select signal to suppress, and
this controller does not advertise the optional `SPI_NO_CS` userspace mode
bit. The correction is to request plain SPI mode 0 and retain the overlay's
zero-chip-select topology.

## Safety and rollback observations

- The target, overlay, ARM binary, and dynamic discovery checks passed.
- The operator observed no flash or instability.
- Normal unbind removed the spidev character device and cleared the override.
- MCP3002 remained bound to `mcp320x`.
- `oclock.service` remained inactive.
- `throttled=0x0` was captured before and after.

The corrected retry must use the same one-frame, timeout, immediate-unbind,
and visual-observation gate.
