# Phase 5 LPD8806 1 MHz cadence result

## Result

The 2026-08-02 repeated all-off benchmark rejected the 1 MHz `spi-gpio`
profile on performance. All 25 frames returned valid application-path
metadata, the operator confirmed that the strip remained dark and stable, and
all rollback and isolation checks passed. However, zero of the 25
`LPD8806::show()` calls met the existing 12 ms application-tick budget.

This is a transport-timing failure, not a functional or safety failure. Do not
run the full application with the 1 MHz strip profile.

| Item | Value |
| --- | --- |
| Transfer source commit | `be9513b7269b9c63ebb119c78ffd188a7d6cfbb8` |
| Cadence verifier commit | `21a544d238a04eddca82364d7591a835349e01bf` |
| Capture | `oclock-phase5-lpd-cadence-20260802T183445Z-cDuPJUGN.tar.gz` |
| Capture SHA-256 | `a09dbfeba1acf38fa353bb77f3c46ea198300c0bd9aa161c8b64a277f95692c4` |
| Frames | 25 valid of 25 requested |
| Frames within 12 ms | 0 of 25 |
| Minimum | 20,293 microseconds |
| Median | 20,473 microseconds |
| 95th percentile | 24,722 microseconds |
| Maximum | 44,533 microseconds |
| Mean | 21,685.3 microseconds |
| Visual result | Entire strip remained off and stable |

The adjacent archive checksum matched before extraction, every archive member
used a relative non-traversing path, and a scan found no SSH or private-tailnet
topology markers. Raw evidence remains outside Git.

## State after the benchmark

- The strip's temporary `spidev` binding and character device were removed.
- The MCP3002 remained bound to its native `mcp320x` driver.
- `oclock.service` remained inactive.
- Firmware reported `throttled=0x0`.
- The benchmark ran standalone all-off frames; it did not run the application
  or read the ADC.

## Kernel explanation and next experiment

The exact Raspberry Pi downstream `rpi-6.18.y` `spi-gpio` source explains a
speed discontinuity at 1 MHz. Its delay helper calls `ndelay()` only when the
requested half-cycle is at least 500 ns, corresponding to 1 MHz or slower. The
source explicitly notes that `ndelay()` is often a rounded-up `udelay()` and
that the undelayed GPIO bit-banger may free-run between roughly 1 and 10 Mbps.
The mode-0 bit loop invokes that delay on both sides of every clock edge, while
the controller performs GPIO operations through descriptor calls that may
sleep.

Sources:

- [Raspberry Pi `spi-gpio.c`, `rpi-6.18.y`](https://github.com/raspberrypi/linux/blob/rpi-6.18.y/drivers/spi/spi-gpio.c#L54-L102)
- [Raspberry Pi `spi-bitbang-txrx.h`, `rpi-6.18.y`](https://github.com/raspberrypi/linux/blob/rpi-6.18.y/drivers/spi/spi-bitbang-txrx.h#L45-L73)
- [Raspberry Pi SPI core speed validation, `rpi-6.18.y`](https://github.com/raspberrypi/linux/blob/rpi-6.18.y/drivers/spi/spi.c#L3826-L3872)

The stable approximately 20.4 ms cluster is therefore consistent with delay
overhead at exactly 1 MHz. That is an inference from the measured result and
the exact running-kernel branch, not a measured waveform or reported effective
clock rate.

The next gate should request 2 MHz from the same application transport. This
makes the calculated half-cycle less than 500 ns and exercises `spi-gpio`'s
undelayed path without rewiring. Keep the production factory at 1 MHz until the
standalone experiment passes. Reuse the same all-off frame, bind-once cadence
guard, visual confirmation, timeout, and immediate unbind. First prove one
safe frame; then repeat 25 frames against the 12 ms budget. Do not change the
live overlay or run the whole application for this experiment: the current
spidev control path can configure and submit a per-transfer speed while the
SPI core limits only against a controller maximum when one exists.

If 2 MHz does not provide adequate and stable margin, reject the arbitrary-pin
`spi-gpio` strip path and evaluate the already-documented hardware-SPI rewiring
option. Do not reduce application refresh behavior to conceal the miss.
