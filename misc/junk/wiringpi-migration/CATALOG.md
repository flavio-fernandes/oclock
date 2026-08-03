# Retired WiringPi migration tooling

These 21 files drove the hardware gates that moved the Office Clock from
WiringPi on a Pi Zero/Jessie to libgpiod plus kernel SPI on a Pi Zero W/Trixie.
Every one of them has served its purpose. **Nothing here is used by the
application, the build, the test suite, or the running clock.**

They are kept because the migration's conclusions rest on them: each result
document says "this measurement came from this script," and being able to read
the script is what makes the number checkable rather than merely asserted.

## Deleting this directory

It is severable on purpose. `tests/compatibility.sh` deliberately asserts
nothing about these files, and no Makefile target builds them, so removal is:

```sh
git rm -r misc/junk/wiringpi-migration
```

The one cost is that documents under `docs/` link here. Those links break on
deletion; the prose around them still stands on its own, and the files remain
readable in the pull request that introduced them.

## What each file proved

### Evidence collectors — read-only, changed nothing

| File | What it collected | Result recorded in |
| --- | --- | --- |
| `collectHardwareBaseline.sh` | The healthy original Zero/Jessie production baseline, including the 3.55% CPU figure everything later is compared against | [`wiringpi-phase0-baseline.md`](../../../docs/wiringpi-phase0-baseline.md) |
| `collectGpioTarget.sh` | Whether the target image could support a libgpiod backend at all | [`wiringpi-phase3-target-baseline.md`](../../../docs/wiringpi-phase3-target-baseline.md) |
| `collectPhase5FastGpioTarget.sh` | Whether the Zero W exposed the restricted `/dev/gpiomem` needed for the bounded fast-value experiment | [`wiringpi-phase5-fast-backend.md`](../../../docs/wiringpi-phase5-fast-backend.md) |
| `collectPhase5SpiTarget.sh` | That the target kernel had the SPI core, `spi-gpio`, `spidev`, and a `mcp320x` module — the discovery that redirected the whole architecture | [`wiringpi-phase5-kernel-spi-result.md`](../../../docs/wiringpi-phase5-kernel-spi-result.md) |
| `collectPhase5SpiOverlayBoot.sh` | The first boot with the overlay active: two controllers claiming only the six reviewed GPIOs | [`wiringpi-phase5-spi-live-boot-result.md`](../../../docs/wiringpi-phase5-spi-live-boot-result.md) |
| `collectPhase5Lpd8806Binding.sh` | The runtime `spidev` binding's state, without opening the device | [`wiringpi-phase5-lpd8806-binding-result.md`](../../../docs/wiringpi-phase5-lpd8806-binding-result.md) |
| `collectPhase5Lpd8806Build.sh` | A native ARM build's linkage and identity, without executing it | [`wiringpi-phase5-lpd8806-build-result.md`](../../../docs/wiringpi-phase5-lpd8806-build-result.md) |
| `collectPhase5Mcp3002Calibration.sh` | Three controlled light windows: uncovered, covered, uncovered | [`wiringpi-phase5-mcp3002-calibration-result.md`](../../../docs/wiringpi-phase5-mcp3002-calibration-result.md) |

### Guarded gates — each required an operator to watch and confirm

| File | What it gated | Result recorded in |
| --- | --- | --- |
| `verifyPhase1Hardware.sh` | The WiringPi-backed candidate after the GPIO interface seam was introduced | [`wiringpi-phase1-interface.md`](../../../docs/wiringpi-phase1-interface.md) |
| `verifyPhase5SpiOverlayDryRun.sh` | Offline merge of the overlay into a copy of the live Device Tree | [`wiringpi-phase5-spi-overlay-result.md`](../../../docs/wiringpi-phase5-spi-overlay-result.md) |
| `verifyPhase5Lpd8806FirstTransfer.sh` | Exactly one all-off frame — the first data the modern stack ever sent the strip | [`wiringpi-phase5-lpd8806-first-transfer-result.md`](../../../docs/wiringpi-phase5-lpd8806-first-transfer-result.md) |
| `verifyPhase5Lpd8806Cadence.sh` | 25 timed frames. **This is the gate that rejected 1 MHz**, 0 of 25 in budget at a 20.473 ms median | [`wiringpi-phase5-lpd8806-cadence-result.md`](../../../docs/wiringpi-phase5-lpd8806-cadence-result.md) |
| `verifyPhase5Lpd8806Colors.sh` | The first non-zero pixel data, proving GRB order and signal integrity survived at 2 MHz | [`wiringpi-phase5-lpd8806-colors-result.md`](../../../docs/wiringpi-phase5-lpd8806-colors-result.md) |
| `verifyPhase5Mcp3002FirstRead.sh` | Both ADC channels through the exact application input class | [`wiringpi-phase5-mcp3002-iio-result.md`](../../../docs/wiringpi-phase5-mcp3002-iio-result.md) |
| `verifyPhase5Ht1632Render.sh` | Worst-case matrix renders. **Rejected the per-edge path at 19.2 ms**, then accepted the burst path at 4.094 ms | [`wiringpi-phase5-ht1632-burst-result.md`](../../../docs/wiringpi-phase5-ht1632-burst-result.md) |
| `verifyPhase5GpiodHardware.sh` | The 13-check whole-application trial. The most reusable file here if the full checklist is ever re-run | [`wiringpi-phase5-application-trial-result.md`](../../../docs/wiringpi-phase5-application-trial-result.md) |

`verifyPhase5GpiodHardware.sh` is also where a **false pass** was found and
fixed: it counted the application's `light_sensor: 0` startup sentinel as a
genuine dark reading, so it reported dimming success on a run where the sensor
never went below the threshold. The operator disagreed with the harness and the
operator was right.

### Superseded helper

| File | Superseded by |
| --- | --- |
| `managePhase5Lpd8806Binding.sh` | [`misc/bindOclockStripSpi.sh`](../../bindOclockStripSpi.sh) |

The gate helper demanded a typed confirmation word, asserted the board model
and overlay checksum, and refused to run while `oclock.service` was active. All
three are correct for proving a trial ran against a reviewed overlay, and all
three are wrong for a boot path. The production binder keeps the guard that
carries real meaning — the live Device Tree child — and drops the ceremony. It
covers `status`, `bind`, and `unbind`, so nothing is lost by retiring this.

### Standalone measurement programs

These were built by `make phase5-*` targets, now removed from the Makefile.

| File | What it measured |
| --- | --- |
| `phase5Lpd8806AllOff.cpp` | One 728-byte all-off frame. Built at both 1 MHz and 2 MHz so the rejected profile stayed reproducible |
| `phase5Lpd8806Colors.cpp` | Red, green, blue at half brightness, always ending with an all-off frame because LPD8806 pixels latch and retain |
| `phase5Ht1632Render.cpp` | 20 forced full-panel rewrites, calling `clear()` so the benchmark could not benefit from dirty-chunk shortcuts |
| `phase5Mcp3002Read.cpp` | Both MCP3002 channels through the real `AnalogInput` implementation |

To rebuild one, compile it against the same sources the removed Makefile rules
used; see this file's history for those rules.

## The two findings most worth keeping

**1 MHz was slower than 2 MHz on identical wires.** The kernel's `spi-gpio`
driver inserts `ndelay()` on both sides of every clock edge when the requested
half-cycle is 500 ns or longer. Asking for 1 MHz crosses that threshold; asking
for 2 MHz takes the undelayed free-running path. `verifyPhase5Lpd8806Cadence.sh`
measured both.

**The HT1632's 50 ns data-setup requirement was being met by accident.** The
old path satisfied it only because every GPIO write cost microseconds. A direct
register store does not, which is why the burst transport holds it explicitly
via `OCLOCK_GPIO_BURST_SETUP_NOPS`. `verifyPhase5Ht1632Render.sh` is what made
that visible, and the operator's confirmation of clean stripes is what
validated the chosen default — the timing numbers alone would have looked
identical if the chips were latching garbage.
