# Phase 5 SPI overlay offline gate

## Result

The exact Zero W run passed with zero failures at commit `c79c53c`. The
checksum-valid archive, accepted merged properties, and safety outcome are in
the [offline result](wiringpi-phase5-spi-overlay-result.md). The next gate is
the separately guarded [live overlay boot](wiringpi-phase5-spi-live-boot.md).

## Scope

[`oclock-spi-overlay.dts`](../hardware/oclock-spi-overlay.dts) is the first
project-owned Device Tree overlay for the modern Zero W profile. It preserves
the installed BCM wiring:

| Device | Controller properties | Child binding |
| --- | --- | --- |
| LPD8806 | clock 20, MOSI 21, no CS | `flaviof,oclock-lpd8806`; later explicit `spidev` override |
| MCP3002 | clock 17, MISO 27, MOSI 22, active-low CS 4 | `microchip,mcp3002`; native `mcp320x`/IIO |

It is intentionally not installed by the Makefile and is not enabled by any
repository default. `make spi-overlay` only compiles
`build/oclock-spi-overlay.dtbo` in the checkout.

The LPD8806 node does not claim to be another product merely to match the
kernel's `spidev` allow-list. A later guarded binding step must write `spidev`
to that exact SPI child's `driver_override` and bind it. The resulting dynamic
bus number must be discovered from the child node; application code must not
assume `/dev/spidev0.0`.

## First target check: offline only

Keep `oclock.service` stopped. Fetch the PR branch without switching the
target checkout, extract it to a temporary directory, build the overlay, and
run the verifier:

```sh
cd ~/oclock-phase3
git fetch origin agent/plan-wiringpi-migration

spi_overlay_commit=$(git rev-parse FETCH_HEAD)
spi_overlay_dir=$(mktemp -d /tmp/oclock-spi-overlay-build-XXXXXXXX)
git archive FETCH_HEAD | tar -x -C "${spi_overlay_dir}"
cd "${spi_overlay_dir}"

make spi-overlay
sudo misc/verifyPhase5SpiOverlayDryRun.sh \
  --overlay hardware/oclock-spi-overlay.dts

printf 'spi_overlay_commit=%s\nspi_overlay_dir=%s\n' \
  "${spi_overlay_commit}" "${spi_overlay_dir}"
```

Share the generated archive and adjacent checksum. The verifier:

- confirms the exact Zero W/Trixie target and inactive service;
- confirms `spi-gpio`, `spidev`, and `mcp320x` are available;
- compiles the overlay with the target `dtc`;
- copies `/proc/device-tree` to a temporary blob;
- merges the overlay into that copy with `fdtoverlay`;
- verifies both child identifiers and every preserved GPIO offset in the
  merged file.

The verifier retains only the four Office Clock nodes' properties. It removes
the temporary full-tree blobs before creating the shareable archive because a
complete live Device Tree can contain unrelated board identifiers.

It does not load a module, apply an overlay, bind a driver, read a device,
request a GPIO line, or modify `/boot`. All generated files remain under its
new result directory in `/tmp`.

## Live procedure status

The offline gate itself did not copy the overlay or edit boot configuration.
The subsequent repository checkpoint provides a separate reversible
[live-boot procedure](wiringpi-phase5-spi-live-boot.md) with these safeguards:

1. reconfirm that `oclock.service` is inactive;
2. retain a byte-for-byte boot-configuration backup;
3. install a checksum-pinned `.dtbo` under a project-specific name;
4. add one project-owned `dtoverlay=` line and reboot;
5. verify exact SPI child paths, IIO channels, and GPIO consumers before any
   userspace binding or transfer;
6. provide normal disablement and offline SD-card rescue before the application
   is allowed to drive either device.

The LPD8806 binding and all transfers remain a later gate.

The legacy Zero/Jessie clock remains the production rollback throughout this
work.
