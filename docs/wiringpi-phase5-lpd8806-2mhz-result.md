# Phase 5 LPD8806 2 MHz result

## Result

The 2026-08-02 guarded 2 MHz experiment **passed both gates with zero failures
and zero warnings**. Requesting 2 MHz from the same application transport moved
the kernel `spi-gpio` controller onto its undelayed path and brought
`LPD8806::show()` inside the 12 ms application tick with better than 2x margin.

This reverses the transport-timing rejection recorded for the 1 MHz profile. It
does not authorize deployment, and it does not change the production speed.

| Item | Value |
| --- | --- |
| Experiment commit | `b639cc5430f0fe0469321cb81f99867449b9dea4` |
| Helper binary SHA-256 | `7abc8c06975d1c39bf5b0683426a65572756e5301739e30c00a709125e8536eb` |
| Clean helper build | 63 seconds wall clock |
| One-frame gate | 17 of 17 checks OK |
| One-frame `show()` | 3,193 microseconds |
| One-frame archive | `oclock-phase5-lpd-transfer-20260802T191335Z-63NNirUx.tar.gz` |
| One-frame archive SHA-256 | `c4761ae38e14ad3c75438bf0c3f4937918ce387e1c6e986cae12f90ebaf9e94e` |
| Cadence gate | 7 of 7 checks OK |
| Cadence archive | `oclock-phase5-lpd-cadence-20260802T191545Z-WckvlllG.tar.gz` |
| Cadence archive SHA-256 | `8423d2543b9c53a3d3e300f359e41528ef745131b68969484b1f763ad4668cec` |
| Visual result | Entire strip remained off and stable in both gates |

## Cadence measurements

All 25 frames returned valid application-path metadata and all 25 met the
budget.

| Statistic | 1 MHz (rejected) | 2 MHz (this run) |
| --- | --- | --- |
| Valid frames | 25 of 25 | 25 of 25 |
| Frames within 12,000 us | 0 of 25 | 25 of 25 |
| Minimum | 20,293 us | 2,997 us |
| Median | 20,473 us | 3,001 us |
| 95th percentile | 24,722 us | 3,400 us |
| Maximum | 44,533 us | 5,026 us |
| Mean | 21,685.3 us | 3,150.8 us |

The median improved by a factor of approximately 6.8. Excluding the first
frame, every sample fell in a narrow 2,997 to 3,400 microsecond band. The
5,026-microsecond maximum was frame 1 and is consistent with first-touch page
faults and cold caches in a freshly executed process; it still met the budget.

A 728-byte frame at a nominal 2 MHz bit clock has a theoretical floor near
2,912 microseconds. The observed 2,997-microsecond minimum sits just above
that floor, which indicates the controller genuinely free-ran at close to the
requested rate rather than inserting per-edge delays.

## Why the speed increase helped

This is the outcome predicted by the exact running `rpi-6.18.y` source. The
`spi-gpio` delay helper calls `ndelay()` only when the requested half-cycle is
at least 500 ns, which is 1 MHz or slower, and `ndelay()` is often a rounded-up
`udelay()`. The mode-0 bit loop invokes that delay on both sides of every clock
edge. Requesting 2 MHz makes the calculated half-cycle less than 500 ns and
skips the delay entirely.

The measured 6.8x improvement across a sharp speed threshold is strong
supporting evidence for that reading, but it remains an inference from timing
and source. No waveform was captured and the controller did not report an
effective clock rate.

## State after the experiment

- The strip's temporary `spidev` binding and character device were removed.
- `spi4.0` reports `Driver: none` and an empty `driver_override`.
- The MCP3002 remained bound to its native `mcp320x` driver throughout.
- `oclock.service` remained inactive.
- Firmware reported `throttled=0x0` before and after both gates.
- The installed overlay checksum matched the accepted artifact.
- The helper linked no WiringPi and the full application was never started.

## Limits of this evidence

Record these honestly before treating 2 MHz as settled:

- Every frame in *this* gate was **all-off** (720 bytes of `0x80`). That
  limitation has since been resolved: the
  [colored sequence gate](wiringpi-phase5-lpd8806-colors-result.md) passed on
  2026-08-02, latching uniform red, green, and blue across all 240 pixels with
  correct GRB byte order and no signal-integrity failure. Its slowest frame was
  4,424 microseconds, so treat roughly 4.4 ms rather than the 3.0 ms all-off
  median as the observed worst case for a single frame.
- The measurement covers `show()` only. It excludes the HT1632 matrix work, ADC
  reads, and the rest of the application tick, so it is a necessary but not
  sufficient condition for meeting the cadence in production.
- The production factory remained at 1 MHz at the moment this gate ran;
  `OCLOCK_STRIP_SPEED_HZ` was overridden only for the separately named helper.
  **On this evidence it was then promoted to 2 MHz**, which is the current
  production value in `src/spi/StripSpeed.h`.
- Frames were issued as sequential process invocations, not from the
  application's timer thread under real scheduling pressure.

## Next work

1. Promote 2 MHz to the production strip speed as a separately reviewed change
   to [`StripSpeed.h`](../src/spi/StripSpeed.h), keeping the helper override
   mechanism intact.
2. Add a guarded **colored** frame gate before any application run, to prove
   correct pixel data at 2 MHz rather than only correct silence. This is the
   first gate that can expose a signal-integrity problem.
3. Only then consider a guarded full-application run, which also re-opens the
   light-threshold question deferred by the
   [MCP3002 calibration result](wiringpi-phase5-mcp3002-calibration-result.md).
4. Revisit the HT1632 transport after the two standard SPI devices are settled.

The already-documented hardware-SPI rewiring option is no longer the expected
path for the strip. Do not pursue rewiring on timing grounds unless the colored
frame or full-application gate reintroduces a failure.
