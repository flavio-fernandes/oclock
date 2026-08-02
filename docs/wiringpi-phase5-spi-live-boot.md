# Phase 5 guarded live SPI-overlay boot

## Purpose

This gate tests only Linux discovery and ownership after a reboot with the
accepted Office Clock Device Tree overlay. It does not run an application
candidate or perform an SPI/IIO transfer.

The live change is intentionally split into three operator-visible actions:

1. install a checksum-pinned overlay and append one managed boot block;
2. reboot manually;
3. run a read-only collector while `oclock.service` remains inactive.

## Preconditions

- Use the selected Zero W Rev 1.1/Trixie/ARMv6 unit.
- Keep the original Zero/Jessie clock and SD card untouched as rollback.
- Confirm `oclock.service` is inactive. The existing GPIO backend must not
  compete with the new kernel controllers.
- Build the overlay from the fetched PR commit with `make spi-overlay`.
- Its SHA-256 must be
  `53b593f4c30a78c8beb446cf506816af29a817d0daf93bb899a9054c7aa61959`.

## Enable for the next boot

From the temporary checkout containing the fetched PR commit:

```sh
make spi-overlay
sha256sum build/oclock-spi-overlay.dtbo

sudo misc/managePhase5SpiOverlay.sh status
sudo misc/managePhase5SpiOverlay.sh enable \
  --overlay build/oclock-spi-overlay.dtbo \
  --confirm ENABLE
sudo misc/managePhase5SpiOverlay.sh status
```

The helper refuses the wrong model, revision, architecture, OS, active service,
active overlay, duplicate managed block, or wrong compiled checksum. On
success it:

- creates a timestamped, byte-identical
  `/boot/firmware/config.txt.oclock-phase5-*.bak`;
- writes an adjacent SHA-256 file for that backup;
- installs a byte-identical `/boot/firmware/overlays/oclock-spi.dtbo` without
  overwriting an existing file;
- appends this block to `/boot/firmware/config.txt`:

```text
# BEGIN Office Clock Phase 5 SPI overlay
[all]
dtoverlay=oclock-spi
# END Office Clock Phase 5 SPI overlay
```

It syncs those files but does not reboot. Record the printed backup path.

## Reboot and inspect

After reviewing the second `status` output:

```sh
sudo reboot
```

Reconnect, do not start `oclock.service`, and run:

```sh
cd ~/oclock-phase3
git fetch origin agent/plan-wiringpi-migration

spi_boot_commit=$(git rev-parse FETCH_HEAD)
spi_boot_dir=$(mktemp -d /tmp/oclock-spi-boot-build-XXXXXXXX)
git archive FETCH_HEAD | tar -x -C "${spi_boot_dir}"
cd "${spi_boot_dir}"

sudo misc/managePhase5SpiOverlay.sh status
sudo misc/collectPhase5SpiOverlayBoot.sh

printf 'spi_boot_commit=%s\nspi_boot_dir=%s\n' \
  "${spi_boot_commit}" "${spi_boot_dir}"
```

Share the generated archive and checksum. The collector verifies:

- the installed artifact and one active boot directive;
- both live `spi-gpio` controller nodes;
- an unbound LPD8806 SPI child;
- the MCP3002 child bound to `mcp320x`;
- both IIO raw-channel attributes without reading them;
- all six kernel-owned GPIO offsets;
- an inactive `oclock.service`.

No `driver_override`, spidev bind, device open, ADC read, or transfer occurs in
this gate.

## Rollback

Before reboot, or after reconnecting normally, disable the managed line with:

```sh
sudo misc/managePhase5SpiOverlay.sh disable --confirm DISABLE
sudo misc/managePhase5SpiOverlay.sh status
sudo reboot
```

`disable` takes another timestamped boot-config backup, comments the exact
managed directive, and deliberately leaves the `.dtbo` installed. An installed
but unreferenced overlay file is inert and remains available for inspection.

If the system cannot boot or cannot be reached, power it off, mount the boot
partition on another machine, and comment or remove only this exact line from
`config.txt`:

```text
dtoverlay=oclock-spi
```

The timestamped `config.txt.oclock-phase5-*.bak` and checksum are on the same
boot filesystem. Restore the newest known-good backup if a narrow edit is not
possible. The legacy Zero/Jessie unit remains the independent physical
rollback.

Do not delete the overlay artifact or backups until a later gate has exercised
and documented both normal disablement and recovery.
