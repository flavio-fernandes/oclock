# Modern hardware build and rollback policy

## Decision

As of 2026-08-02, the current tree supports one hardware build: the selected
Raspberry Pi Zero W, 32-bit Trixie stack. `make` and `make hardware` both build
that profile. The old source-level WiringPi build and the too-slow pure
libgpiod/per-edge profiles are no longer supported or tested.

This is a deliberate reduction in migration baggage. The complete original
Pi Zero/Jessie/WiringPi unit, SD card, configuration, and preserved executable
are the rollback mechanism. If the modern work reaches a brick wall, rollback
means powering down the Zero W and reconnecting that known-good unit; it does
not require rebuilding the old stack from the current branch.

## Selected build

The hardware build currently combines:

- libgpiod v2 for GPIO discovery, ownership, direction, and ordinary input;
- the BCM2835 mapping exposed through `/dev/gpiomem` for the remaining
  high-rate value operations;
- Linux `spi-gpio` and `spidev` for one complete LPD8806 frame per transfer;
- Linux `mcp320x` and IIO sysfs for both MCP3002 raw channels;
- libatomic for ARMv6 64-bit atomic operations;
- the existing libevent and libmosquitto application dependencies.

Use:

```sh
make
# or, equivalently
make hardware
```

The former `GPIO_BACKEND` and `STRIP_TRANSPORT` variables are removed.
Supplying either causes an immediate error. This prevents copied experimental
commands from silently selecting or appearing to select a stale profile.

The Makefile also no longer changes `oclock` to `root:root` or adds its owner
setuid bit. Installation, service identity, and device permissions will be
defined and tested explicitly during the deployment phase rather than being a
side effect of compilation.

## What remains in the repository

The WiringPi, pure-libgpiod, GPIO-bit-banged LPD8806, and null-SPI source files
remain for historical comparison, protocol debugging, and review of behavior.
They are not selected by the hardware build, linked into its binary, or part
of the supported compiler/test matrix. Their presence must not be interpreted
as a compatibility promise.

The fake GPIO and fake SPI implementations remain supported test tools. They
continue to verify application and wire-protocol behavior without GPIO
hardware or WiringPi.

## Safety boundary

This build-policy decision was never deployment approval on its own. The gates
it deferred to have since run, so this section records both the boundary and
where it now stands.

Every gate this policy named as preceding a full application trial has passed:
the [MCP3002/IIO conversion](wiringpi-phase5-mcp3002-iio.md) with hardware-free,
live-read, and controlled covered/uncovered evidence; the standalone
[guarded LPD8806 all-off transfer](wiringpi-phase5-lpd8806-first-transfer.md);
the dedicated strip cadence gate, which rejected 1 MHz and accepted
[2 MHz](wiringpi-phase5-lpd8806-2mhz-result.md); the
[HT1632 burst transport](wiringpi-phase5-ht1632-burst-result.md); and
representative-room dimming observation, which
[retuned the thresholds](wiringpi-phase5-application-trial-result.md) to a
measured 460/700.

The whole application then passed its
[trial](wiringpi-phase5-application-trial-result.md) 13 checks to 0, and
`oclock.service` is now installed, enabled, and running on the Zero W alongside
`oclock-strip-spi.service`, which establishes the strip binding at boot.

What this policy still forbids, and what has genuinely not changed:

- **No wiring change.** Not one wire has moved, and
  `make test-spi-overlay` pins all six BCM numbers so none quietly can.
- **No privilege change.** The Makefile still does not chown or setuid, and the
  application was never given permission to bind its own SPI device. Final
  service identity and device permissions remain a separate, untested question.
- **Rollback is still the preserved Zero/Jessie unit**, powered off and
  physically intact. Swapping it back has not been rehearsed.

Threshold changes are no longer forbidden but are still evidence-gated: 460/700
replaced 360/500 only after the values were measured in the actual room.
