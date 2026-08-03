# Phase 5 kernel-SPI discovery result

## Decision

The 2026-08-02 read-only capture clears the kernel-SPI discovery gate on the
selected Raspberry Pi Zero W/Trixie image. The reported `dtoverlay` failure was
a collector bug, not missing target software: this `dtoverlay` prints valid
help and returns status 1 for a bare `-h`, while both `dtoverlay -l` and
`dtoverlay -a` completed successfully.

No overlay is deployment-approved yet. The next step is an offline merge of
the disabled-by-default project overlay against a copy of the target's active
Device Tree. This must pass before any file is installed under `/boot`.

## Retained evidence

| Item | Value |
| --- | --- |
| Capture | `oclock-phase5-spi-20260802T135213Z-RCuWzN1Y.tar.gz` |
| Capture SHA-256 | `a650a19ba134e59d81ac78b56263f07c60c08641df71c6cb4e887c0d4e420eb0` |
| Model | Raspberry Pi Zero W Rev 1.1, revision `9000c1` |
| OS | Raspbian GNU/Linux 13 (Trixie), ARMv6/armhf |
| Kernel | `6.18.39+rpt-rpi-v6`, package `1:6.18.39-1+rpt1` |
| Overlay tools | `raspi-utils-dt 20260626-1`, `dtoverlay`, `dtc 1.7.2`, `fdtoverlay` |
| Authoritative boot configuration | `/boot/firmware/config.txt` |

The adjacent checksum verified before extraction, and the archive contained
no absolute or parent-traversal paths. The raw archive remains outside Git.

## Target findings

- `CONFIG_SPI=y`, `CONFIG_SPI_GPIO=m`, and `CONFIG_SPI_SPIDEV=m` are present.
- The exact `spi-gpio.ko.xz` and `spidev.ko.xz` modules are installed.
- No SPI controller, SPI child, or `/dev/spidev*` node existed at capture time.
- No office-clock GPIO line had a kernel consumer. The retained directions are
  residue from the stopped candidate and are not ownership claims.
- Every existing BCM offset—4, 6, 10, 13, 17, 19, 20, 21, 22, 26, and 27—was
  visible through the selected Broadcom GPIO chip.
- `oclock.service` was inactive/dead and disabled. It must stay inactive while
  an overlay claims GPIO 4, 17, 20, 21, 22, and 27 **and the application still
  requests those lines directly**. That is what later changed: the application
  moved to the overlay's native IIO device for the ADC and to `spidev` for the
  strip, so it no longer competes for those GPIOs. The service is now enabled
  and running with the overlay active.
- `/boot/firmware/config.txt` contains no Office Clock or general SPI enable;
  `/boot/config.txt` is a separate regular file and is not the selected edit
  target.
- The installed overlay collection has no generic arbitrary-pin `spi-gpio`
  overlay, so this repository must own the exact overlay.

The capture itself changed no module, overlay, binding, SPI device, GPIO line,
service, package, or boot file.

## Exact kernel-source correlation

The installed package identifies Raspberry Pi's `linux` source version
`1:6.18.39-1+rpt1`. Its signed source manifest records these inputs:

| Source input | Verified SHA-256 |
| --- | --- |
| `linux_6.18.39.orig.tar.xz` | `a7a7e3d2ae9d95e74197223a8d4eb5f6be7aac21b6e6de27e9685d001c1f8cb0` |
| `linux_6.18.39-1+rpt1.debian.tar.xz` | `a1bd56df5157ab9ca1cfb5655ec4cdb136f5911ac284d45b6c6cd7adfe62cabb` |

Both downloaded inputs matched the manifest. The exact armhf Raspberry Pi
configuration sets `CONFIG_MCP320X=m`. The driver supports
`compatible = "microchip,mcp3002"`, exposes both single-ended channels through
IIO, and performs the MCP3002 command/response as a kernel SPI transaction.

The exact `spidev.c` rejects a Device Tree node described directly as
`compatible = "spidev"`. Its installed compressed module SHA-256 is
`635863f9af5f50d6412791c3bb5195c9dab465bcf821e2d51252aee74f4c927e`.
The LPD8806 therefore uses an honest project identifier,
`flaviof,oclock-lpd8806`, and a guarded trial will explicitly set
`driver_override` to `spidev` before binding it. The overlay does not borrow a
different product's compatible string.

This changes the earlier all-`spidev` sketch in one useful way:

| Device | Revised kernel interface |
| --- | --- |
| LPD8806 | transmit-only `spi-gpio`; explicit userspace `spidev` override/bind |
| MCP3002 | full-duplex `spi-gpio`; native `mcp320x` driver and IIO raw channels |

The split keeps the existing wires while avoiding custom ADC protocol code in
the modern profile. The legacy WiringPi implementation and default build stay
unchanged.

## Collector correction

[`collectPhase5SpiTarget.sh`](../misc/junk/wiringpi-migration/collectPhase5SpiTarget.sh) now:

- treats successful `dtoverlay -l` and `dtoverlay -a` as the tooling gate;
- captures `CONFIG_IIO`, `CONFIG_MCP320X`, `mcp320x` module metadata, and
  current IIO sysfs state;
- recognizes the exact reviewed kernel and installed `spidev` module hash.

The corrected collector remains read-only. A rerun is useful for provenance
but is not required to reinterpret the already captured `dtoverlay` output.

## Next gate

Build [`oclock-spi-overlay.dts`](../hardware/oclock-spi-overlay.dts) with
`make spi-overlay`, then run
[`verifyPhase5SpiOverlayDryRun.sh`](../misc/junk/wiringpi-migration/verifyPhase5SpiOverlayDryRun.sh) on
the Zero W. The verifier compiles the overlay and merges it into a file copy of
the active Device Tree. It does not apply the overlay or touch `/boot`.

Only after that archive passes should a separate, explicitly reversible step
install the overlay, add one line to `/boot/firmware/config.txt`, reboot, and
inspect the resulting SPI/IIO ownership while `oclock.service` remains stopped.

## Primary references

- [Exact Raspberry Pi kernel source manifest](https://archive.raspberrypi.com/debian/pool/main/l/linux/linux_6.18.39-1+rpt1.dsc)
- [Linux spidev userspace API](https://www.kernel.org/doc/html/latest/spi/spidev.html)
- [Raspberry Pi Device Tree and overlay documentation](https://www.raspberrypi.com/documentation/computers/configuration.html#device-trees-overlays-and-parameters)
