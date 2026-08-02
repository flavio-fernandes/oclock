# Phase 5 kernel-SPI discovery handoff

## Purpose

The first step after the failed per-edge GPIO trials is a read-only inspection
of the selected Raspberry Pi Zero W/Trixie system. The result will establish
whether its exact downstream kernel and boot environment can support two
independent `spi-gpio` controllers exposed to the application through
`spidev`.

This is discovery, not installation. No Device Tree overlay is included or
enabled yet. The current GPIO drivers and a future SPI controller cannot own
the same lines simultaneously, so pin ownership, driver binding, permissions,
startup behavior, and rollback must be reviewed before an overlay is applied.

## Fixed hardware profile

The proposed kernel-SPI profile preserves the existing wires:

| Device | Transport | BCM GPIOs |
| --- | --- | --- |
| LPD8806 | independent `spi-gpio`/`spidev` bus | clock 20, data 21, no CS |
| MCP3002 | independent `spi-gpio`/`spidev` bus | clock 17, MISO 27, MOSI 22, CS 4 |
| HT1632 | later narrow bulk mmap transport | CS 6, WR 13, data 19, select clock 26 |
| Motion | libgpiod v2 input | 10 |

The two software SPI controllers are intentional. The devices do not share a
clock/data wiring pair, and forcing them onto one bus would require rewiring.

## What the collector records

[`collectPhase5SpiTarget.sh`](../misc/collectPhase5SpiTarget.sh) captures:

- the exact Zero W model, revision, Trixie release, ARMv6 runtime, armhf
  package architecture, running kernel, and installed kernel packages;
- `CONFIG_SPI`, `CONFIG_SPI_GPIO`, and `CONFIG_SPI_SPIDEV` from the running
  kernel configuration;
- loaded modules, module availability/dependencies, and `spi-gpio`/`spidev`
  module metadata;
- existing SPI controllers, SPI devices, registered drivers, and
  `/dev/spidev*` nodes without opening any node;
- `spidev` aliases, module checksum, sysfs binding controls, and any installed
  matching downstream kernel source;
- current GPIO consumer metadata for every office-clock BCM offset;
- Raspberry Pi boot-configuration locations and only their Device Tree/SPI/GPIO
  directives;
- installed overlay files/documentation plus `dtoverlay`, `dtc`, and
  `fdtoverlay` availability;
- active Device Tree SPI nodes and a read-only snapshot of `oclock.service`.

The archive deliberately omits serial numbers, MAC addresses, and network
configuration. The script does not load modules, apply or compile overlays,
bind drivers, open SPI devices, request or read GPIO lines, stop services,
install packages, or modify boot files.

## Operator procedure

Do not switch the target worktree or replace its running executable. Extract
the collector from the PR branch into `/tmp` and run it independently:

```sh
cd ~/oclock-phase3
git fetch origin agent/plan-wiringpi-migration

spi_discovery_commit=$(git rev-parse FETCH_HEAD)
collector=/tmp/collectPhase5SpiTarget.sh
git show FETCH_HEAD:misc/collectPhase5SpiTarget.sh >"${collector}"
chmod 0755 "${collector}"

sudo "${collector}"
printf 'spi_discovery_commit=%s\n' "${spi_discovery_commit}"
```

Share both generated files:

- `/tmp/oclock-phase5-spi-<timestamp>-<suffix>.tar.gz`
- the adjacent `.tar.gz.sha256`

There is no observation window and no operator prompt. The clock service is
left exactly as found.

## How the result will be used

The archive is a gate for the next commit, not proof that the transport meets
timing requirements. Review it to decide:

1. whether both `spi-gpio` and `spidev` are present for the running kernel;
2. whether the downstream kernel accepts a direct Device Tree `spidev`
   compatible or requires a real device identifier or explicit
   `driver_override` binding;
3. which overlay directory and boot `config.txt` are authoritative;
4. whether any existing controller, overlay, or process already owns the six
   LPD8806/MCP3002 pins;
5. how `/dev/spidev*` permissions should be granted without broadening the
   migration into a service-user or privilege redesign.

Only after those answers are documented should the repository add a disabled
by-default overlay with exact install, enable, disable, and uninstall steps.
The first application conversion will be the LPD8806 only. The MCP3002 and its
light-threshold calibration remain unchanged until strip timing passes.

## Binding caution

Upstream Linux no longer supports describing a Device Tree peripheral with a
generic `compatible = "spidev"`; it expects a supported real device identifier
or a deliberate sysfs `driver_override`/bind operation. Raspberry Pi kernels
have carried downstream behavior in this area, so the exact installed kernel
must be correlated with its source rather than assuming upstream or historical
Pi behavior. The collector gathers that correlation evidence without testing a
binding or changing live kernel state.

## References

- [Linux SPI userspace API](https://www.kernel.org/doc/html/latest/spi/spidev.html)
- [Raspberry Pi Device Tree and overlay documentation](https://www.raspberrypi.com/documentation/configuration/device-tree)
- [Raspberry Pi `config.txt` documentation](https://www.raspberrypi.com/documentation/computers/config_txt.html)
