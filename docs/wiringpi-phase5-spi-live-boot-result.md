# Phase 5 live SPI-overlay boot result

## Decision

The accepted Office Clock overlay passed its live-boot gate on the Raspberry
Pi Zero W/Trixie target on 2026-08-02. Both kernel software-SPI controllers
appeared with the preserved GPIO wiring, the MCP3002 bound to its native IIO
driver, and the LPD8806 child remained deliberately unbound. The onboard Wi-Fi
returned after reboot and the service remained inactive.

This result authorizes a separately guarded, runtime-only LPD8806 `spidev`
binding check. It does not authorize opening the strip device, transferring a
frame, reading the ADC, starting `oclock`, or deploying a modern candidate.

## Evidence

| Item | Value |
| --- | --- |
| Source commit | `ee74276bfb3cea666ecb0b48e6be6c3f037e1acf` |
| Capture | `oclock-phase5-spi-boot-20260802T151201Z-YEXB983Q.tar.gz` |
| Capture SHA-256 | `c6919e9f4f2b824ed5a066467582a6da9de1825c1258278943272601a247ad4c` |
| Installed overlay SHA-256 | `53b593f4c30a78c8beb446cf506816af29a817d0daf93bb899a9054c7aa61959` |
| Target | Raspberry Pi Zero W Rev 1.1, revision `9000c1`, ARMv6/armhf Trixie |
| Kernel | `6.18.39+rpt-rpi-v6` |
| Collector result | 10 OK, 0 failures, 0 warnings |

The adjacent checksum verified before extraction, and the archive contained
no absolute or parent-traversal paths. Raw evidence remains outside Git.

## Accepted live state

The managed boot block and installed artifact each appeared exactly once.
The runtime bus numbers are discoveries from this boot, not stable interface
names:

| Device Tree child | This boot | Driver/interface | Preserved GPIOs |
| --- | --- | --- | --- |
| `/oclock-strip-spi/lpd8806@0` | `spi4.0` | deliberately unbound | SCK 20, MOSI 21 |
| `/oclock-adc-spi/mcp3002@0` | `spi3.0` | `mcp320x`, `iio:device0` | SCK 17, MISO 27, MOSI 22, active-low CS 4 |

Both MCP3002 raw-channel attributes existed, but the collector did not read
them. GPIO consumer metadata attributed offsets 4, 17, 20, 21, 22, and 27 to
the new kernel controllers. The loaded modules included `spi_gpio`,
`spi_bitbang`, `mcp320x`, and `industrialio`; `spidev` was not yet loaded.

The first SSH attempt immediately after reboot reported no route to the host.
A later attempt connected normally over the Zero W's onboard Wi-Fi, and the
target reported eight minutes of uptime. This is successful recovery, not yet
evidence for a bounded cold-boot reconnect time or long-term Wi-Fi stability.

## Safety and rollback state

The collector did not bind `spidev`, open a device, transfer data, read IIO,
request GPIO, change a package, or start the service. The enable operation
retained this boot-configuration backup:

```text
/boot/firmware/config.txt.oclock-phase5-20260802T150221Z.bak
```

The normal disable and offline SD-card recovery procedures remain in
[`wiringpi-phase5-spi-live-boot.md`](wiringpi-phase5-spi-live-boot.md). Keep the
overlay active and `oclock.service` inactive for the next binding-only gate.
