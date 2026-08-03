# Phase 6 strip binding persistence — gate and result

## Result

**Passed.** On 2026-08-03 the Office Clock rebooted and came back with the
strip binding established and the clock running, with nothing done by hand. The
operator confirmed display, LED strip, and motion all correct after the
unattended boot.

This closes the last Phase 6 blocker and retires the longest-standing item on
the write-up's "claims that must wait" list.

| Item | Value |
| --- | --- |
| Board | Raspberry Pi Zero W Rev 1.1 (`9000c1`) |
| Reboot issued | 2026-08-03 06:38:51 EDT |
| Kernel handoff | 13.0 s |
| Binding unit finished | 06:39:58 EDT |
| Clock started | 06:40:57 EDT |
| Total boot | 2 min 24.8 s |
| `oclock.service` `NRestarts` | **0** |
| `get_throttled` | `0x0` |
| Helper SHA-256 | `524ffa6452526b9b6e2dc2cc49a2fcb342a9e34224cdd40d079867c8436d8e48` |

## The problem

The overlay gives the LPD8806 child a project-owned compatible,
`flaviof,oclock-lpd8806`. No in-tree driver claims it, which is deliberate:
modern `spidev` refuses `compatible = "spidev"`, and borrowing another
product's identifier would misdescribe the hardware.

The consequence is that `/dev/spidev4.0` does not exist until something
explicitly writes `spidev` to the child's `driver_override` and binds it. Every
Phase 5 gate did that by hand. A hand-made binding does not survive a reboot,
so the clock could not come back from a power cut on its own.

An explicit non-goal, carried from the original plan: **the application must
not be given privilege to bind its own device.** Binding is a system
responsibility.

## Why a systemd unit rather than the alternatives

Three approaches were considered.

**Change the overlay to a compatible `spidev` already claims.** Rejected. The
sanctioned identifiers in `spidev_dt_ids` all name real products
(`rohm,dh2228fv` and friends). Adopting one would make the Device Tree lie
about the hardware to obtain a side effect, and the overlay comment already
records that decision.

**A udev rule.** Genuinely attractive: it fires exactly when the device
appears, so there is no ordering guesswork. Rejected for two reasons. Binding a
driver from a udev `RUN` is a known hazard, since the bind generates further
uevents while udev holds the device. More decisively, a udev rule fails
*silently*: if it does not fire, the clock starts anyway, cannot open its SPI
output, and `Restart=on-failure` converts that into a crash loop. That exact
crash loop was already hit once during Phase 5.

**A systemd oneshot unit.** Chosen. It logs to the journal, it can fail loudly,
and `Requires=` lets the failure propagate so the clock refuses to start rather
than crash-looping. It also never assumes a bus number.

## What was built

- [`misc/bindOclockStripSpi.sh`](../misc/bindOclockStripSpi.sh) — the
  production binder. Idempotent, waits for the SPI child, verifies the end
  state, and rolls back a partial binding.
- [`misc/oclock-strip-spi.service`](../misc/oclock-strip-spi.service) —
  `Type=oneshot` with `RemainAfterExit=yes`, because the binding is state
  rather than a process. `ExecStop` reverses it.
- [`misc/oclock.service`](../misc/oclock.service) — gains
  `Requires=oclock-strip-spi.service` and the matching `After=`.

The production binder is deliberately *not* the Phase 5 gate helper
([`managePhase5Lpd8806Binding.sh`](../misc/junk/wiringpi-migration/managePhase5Lpd8806Binding.sh)).
The gate helper demands a confirmation word, asserts the board model and the
overlay checksum, and refuses to run while `oclock.service` is active. Every
one of those is correct for proving a trial ran against a reviewed overlay, and
wrong for a boot path. The production binder keeps the guard that carries real
meaning — the live Device Tree child — and drops the ceremony.

## The race the wait loop exists for

`spi_gpio` is a module, loaded by udev during coldplug, so the SPI children are
created well after userspace starts. On this boot:

```
06:38:46  boot
06:39:54  oclock-strip-spi.service starts
06:39:58  strip child bound, /dev/spidev4.0 created
06:40:03  mcp320x spi3.0 probes        <-- five seconds LATER
06:40:57  oclock.service starts
```

The strip child happened to be present when the binder ran, so the wait
contributed nothing on this particular boot. The ADC child appearing five
seconds afterwards is the point: these children materialize asynchronously
across that window, and the ordering between them is not guaranteed. A binder
without the wait would work on most boots and fail on some, which is the worst
possible failure mode for a device that is supposed to recover from power cuts
unattended.

The helper waits up to 90 seconds and the unit allows 120, sized against the
earlier observation of the MCP3002 probing about 54 seconds after boot on a
cold start.

## Evidence

### Offline: tests/strip-binding.sh

Fourteen checks against a fake sysfs tree, wired into `make test`. Covers what
a single reboot cannot: every refusal path, the idempotent re-run, and the
exact writes performed.

| Check | What it protects |
| --- | --- |
| Refuses when the strip child never appears | No silent success |
| Names `dtoverlay=oclock-spi` when the DT child is absent | Actionable failure |
| Refuses when two children match | Never picks one arbitrarily |
| Refuses a child bound to another driver | Never steals from `mcp320x` |
| Writes `spidev` to `driver_override`, then the child name to `bind` | Correct sequence |
| Leaves the ADC child untouched | Blast radius |
| Second bind succeeds without writing to `bind` | Idempotent |
| `unbind` reverses and clears `driver_override` | Clean teardown |
| `unbind` on an unbound tree is a no-op | Safe to re-run |

Four mutations were introduced and all four were caught: a wrong
`driver_override` value, the ambiguity guard removed, the idempotent early exit
removed, and the foreign-driver guard removed.

Writing these tests found a real defect. The helper originally sampled the
driver symlink and the character device once, immediately after writing to the
bind control. Neither is guaranteed to be visible in that instant on a
single-core ARMv6 during boot. It now polls briefly for the end state. That bug
would have produced exactly the kind of intermittent boot failure this gate
exists to prevent, and no amount of rebooting a healthy unit would have found
it reliably.

### On the unit, before rebooting

The reboot was made the *second* test, not the first.

| Step | Result |
| --- | --- |
| `systemctl start` against an existing manual binding | Idempotent path, unit active |
| `systemctl stop` | Unbound, `/dev/spidev4.0` removed |
| Binder hidden, then `systemctl start oclock` | Dependency failed; **clock stayed inactive, `NRestarts=0`** |
| Binder restored, `systemctl start oclock` alone | Pulled in the binding, bound from cold, clock started |

The third row is the design's central claim, demonstrated rather than asserted:
a failed binding stops the clock from starting instead of feeding a crash loop.

### The reboot

`oclock-strip-spi.service` and `oclock.service` were enabled, the unit
rebooted, and both came up unattended. `NRestarts=0`, `throttled=0x0`, the ADC
still bound to its native `mcp320x` driver, and the operator confirmed display,
strip, and motion.

## What this exposed for Phase 7

Boot took 2 min 24.8 s. The binding unit is 4.96 s of that. The weight is
elsewhere:

| Unit | Time |
| --- | --- |
| `NetworkManager.service` | 59.6 s |
| `cloud-init-main.service` | 21.6 s |
| `dev-mmcblk0p2.device` | 15.3 s |
| `tailscaled.service` | 7.2 s |
| `oclock-strip-spi.service` | 5.0 s |

`cloud-init` on a wall clock is pure overhead, and it feeds directly into the
service-trimming work already queued for Phase 7 as the modern replacement for
the Jessie-era `avahi` / `bluez` / `triggerhappy` removals. There is now a
concrete before measurement to trim against.

## Notes

- Both units are installed to `/usr/lib/systemd/system/`, matching where
  `oclock.service` already lived on this unit rather than introducing a second
  copy under `/etc/`.
- `ExecStop` unbinds. Stopping `oclock-strip-spi.service` therefore also stops
  `oclock.service`, which `Requires=` it. That is intended.
- This gate proves recovery from a **clean reboot**. A hard power cut skips the
  orderly `ExecStop`, which should be strictly easier, since boot always starts
  from an unbound child. It has not been tested and is not claimed.
