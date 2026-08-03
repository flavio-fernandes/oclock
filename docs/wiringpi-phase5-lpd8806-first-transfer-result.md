# Phase 5 LPD8806 first-transfer results

## Accepted retry

The corrected 2026-08-02 retry passed all 17 verifier checks with zero
failures and zero warnings. It sent exactly one 728-byte all-off frame at
1 MHz, the operator observed no flash or instability, and the verifier
returned the strip to its unbound state before asking for that observation.
The MCP3002 remained bound to `mcp320x`, `oclock.service` remained inactive,
and firmware reported no throttling.

This accepts the Linux SPI transport's first live payload and authorizes the
separate MCP3002/IIO application conversion. It is not final strip performance
acceptance: the measured `LPD8806::show()` call took 20,956 microseconds,
which is longer than the application's existing 12 ms tick.

| Item | Value |
| --- | --- |
| Source commit | `be9513b7269b9c63ebb119c78ffd188a7d6cfbb8` |
| Capture | `oclock-phase5-lpd-transfer-20260802T165727Z-8OeB0Ln6.tar.gz` |
| Capture SHA-256 | `519419fdbca05da5a5e9135babc635b188ba5d70c5f439878333dc8cd35d0e2d` |
| Transfer-tool SHA-256 | `47997ffd306956098a3a23be6a240e7f78373fe57054c9d7ff0c180fbe14c06f` |
| Frame | 720 data bytes plus 8 latch bytes |
| SPI configuration | mode 0, MSB first, 8 bits, 1 MHz |
| Measured `show()` time | 20,956 microseconds |
| Verifier result | 17 checks passed; 0 failures; 0 warnings |

The adjacent archive checksum matched before extraction, every archive member
used a relative non-traversing path, and a scan found no SSH or private-tailnet
topology markers. Raw evidence remains outside Git.

## First attempt

The 2026-08-02 first-transfer attempt stopped safely before sending a payload.
Linux `spi-gpio` rejected the userspace `SPI_NO_CS` mode flag during spidev
configuration. The strip remained dark, the runtime binding was removed, the
MCP3002 remained on `mcp320x`, the service remained inactive, and firmware
reported no throttling.

This is a configuration failure, not an LPD8806 timing result. It authorizes a
retry after removing `SPI_NO_CS`; it does not count as a successful transfer.

### Evidence

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

### Root cause

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

### Safety and rollback observations

- The target, overlay, ARM binary, and dynamic discovery checks passed.
- The operator observed no flash or instability.
- Normal unbind removed the spidev character device and cleared the override.
- MCP3002 remained bound to `mcp320x`.
- `oclock.service` remained inactive.
- `throttled=0x0` was captured before and after.

The accepted retry used the same one-frame, timeout, immediate-unbind, and
visual-observation gate.
