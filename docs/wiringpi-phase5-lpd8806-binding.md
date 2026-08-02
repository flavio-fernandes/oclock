# Phase 5 guarded LPD8806 spidev binding

## Result

The exact-board gate passed all eight checks and explicit rollback restored
the original unbound state. See the accepted
[binding result](wiringpi-phase5-lpd8806-binding-result.md). Implementation of
the hardware-free SPI transport and frame tests is now authorized; the first
hardware transfer remains a separate gate.

## Purpose

This gate proves that only the live LPD8806 SPI child can be explicitly bound
to Linux `spidev` and that its character device appears. It does not open that
device or transfer a byte. The native MCP3002/`mcp320x` binding must remain
unchanged throughout.

The helper discovers both children by their resolved Device Tree paths. Never
copy a runtime bus number such as `spi4.0` or `/dev/spidev4.0` into application
configuration; those numbers may change after a boot or kernel update.

## Preconditions

- Use the selected Zero W Rev 1.1/Trixie/ARMv6 target.
- Leave the checksum-pinned Office Clock overlay active.
- Keep `oclock.service` inactive.
- Do not run any earlier WiringPi, libgpiod, or mmap candidate while the
  overlay owns the SPI GPIOs.
- Keep the legacy Zero/Jessie unit available as physical rollback.

## Binding-only check

Fetch the PR branch, verify the expected commit supplied with the run, and
extract it without switching the target checkout:

```sh
cd ~/oclock-phase3
git fetch origin agent/plan-wiringpi-migration

binding_commit=$(git rev-parse FETCH_HEAD)
binding_dir=$(mktemp -d /tmp/oclock-lpd-binding-XXXXXXXX)
git archive FETCH_HEAD | tar -x -C "${binding_dir}"
cd "${binding_dir}"

sudo misc/managePhase5Lpd8806Binding.sh status
sudo misc/managePhase5Lpd8806Binding.sh bind --confirm BIND
sudo misc/collectPhase5Lpd8806Binding.sh

printf 'binding_commit=%s\nbinding_dir=%s\n' \
  "${binding_commit}" "${binding_dir}"
```

Share the generated archive and adjacent checksum. The manager refuses an
unexpected board, revision, OS, overlay hash, boot directive, active service,
ambiguous Device Tree match, changed MCP3002 binding, existing strip driver,
or pre-existing override. It loads `spidev`, writes only the discovered strip
child's `driver_override`, binds that child, and waits for udev. If final
validation fails, it attempts to undo the binding and clear the override.

The read-only collector verifies:

- one LPD8806 child found by Device Tree path;
- an explicit `spidev` override and driver binding;
- a character device for that discovered child;
- the MCP3002 still bound to `mcp320x`;
- kernel ownership of strip clock and data;
- an inactive `oclock.service`.

Neither helper opens `/dev/spidev*`; no SPI ioctl or transfer occurs.

## Runtime rollback

After collecting evidence, leave the binding in place only if the next
instructions explicitly require it. Otherwise undo it with:

```sh
sudo misc/managePhase5Lpd8806Binding.sh unbind --confirm UNBIND
sudo misc/managePhase5Lpd8806Binding.sh status
```

The unbind action removes the strip binding and clears `driver_override`; it
deliberately leaves the harmless `spidev` module loaded. A reboot also clears
this runtime-only override and binding. It does not remove or disable the boot
overlay.

To roll back the overlay itself, follow the disable or offline recovery steps
in [`wiringpi-phase5-spi-live-boot.md`](wiringpi-phase5-spi-live-boot.md).

## Next gate

The repository now contains the hardware-free
[SPI transport and frame tests](wiringpi-phase5-lpd8806-transport.md). Its
native ARM build must pass next. The first hardware transfer remains a
separate guarded trial that preserves the 720 GRB data bytes and eight zero
latch bytes and measures full-frame time.
