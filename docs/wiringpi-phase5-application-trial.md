# Phase 5 guarded whole-application trial

## Status

Ran on 2026-08-02: **12 checks passed, one failed.** Strip smoothness and
timing — the two observations that rejected both earlier candidates — passed,
with timing rated better than production. Dimming failed, most likely because
the cover was not held long enough for the six-second averaging window. See the
[trial result](wiringpi-phase5-application-trial-result.md).

The trial also exposed a false pass in the harness: a startup `light_sensor: 0`
sentinel satisfied the dark-threshold check. Fixed.

## Prerequisite discovered while preparing this gate

**The application cannot drive the strip on its own.**

`LinuxSpidevOutput` discovers the strip child by Device Tree identity and then
opens `/dev/spidev4.0`. It never binds the device. That node only exists while
the reviewed runtime `driver_override` binding is in place, and that binding is
deliberately transient: every gate so far has removed it before exiting, and a
reboot clears it.

So the trial sequence is not simply "start the service". It is:

1. bind the strip explicitly;
2. run the application;
3. stop the application;
4. unbind the strip.

If the application is started without the binding, the strip transport will
fail to open. That should surface as an actionable startup error rather than a
silent fallback; confirming this is itself one of the trial's checks.

### This is an open Phase 6 problem, not just a trial inconvenience

A deployed clock must survive a power cut without a human running a bind
command. Nothing in the current tree makes the strip binding persistent. Before
Phase 6 can proceed, one of these must be designed and reviewed separately:

- a `udev` rule that applies `driver_override` and binds the child when it
  appears;
- a systemd unit ordered before `oclock.service` that performs the bind;
- a Device Tree change that binds the strip at boot, which earlier kernel
  source review found `spidev` deliberately resists for a generic compatible.

Do not solve this inside the application by having it bind its own device. That
would require privilege the application does not otherwise need and would
couple it to a kernel binding detail. Record the chosen mechanism as its own
gate with its own rollback.

## Scope

The existing [`verifyPhase5GpiodHardware.sh`](../misc/junk/wiringpi-migration/verifyPhase5GpiodHardware.sh)
trial harness is transport-agnostic: it validates target identity, starts the
candidate, samples HTTP status, checks light values and dimming thresholds,
collects CPU and memory, and asks the operator for visual observations. It is
reusable here.

What differs for the current candidate:

- the strip must be bound first, as described above;
- the ADC needs no binding, because `mcp320x`/IIO is already bound at boot;
- the matrix needs no binding, because its GPIOs are outside the overlay;
- the strip now runs at 2 MHz through kernel SPI rather than bit-banged GPIO.

## Procedure

Confirm the overlay is active, the ADC is on `mcp320x`, and the service is
inactive. Then bind the strip for the duration of the trial:

```sh
sudo misc/junk/wiringpi-migration/managePhase5Lpd8806Binding.sh status
sudo misc/junk/wiringpi-migration/managePhase5Lpd8806Binding.sh bind --confirm BIND
```

Run the trial with the modern binary built from the gate commit:

```sh
sudo misc/junk/wiringpi-migration/verifyPhase5GpiodHardware.sh \
  --binary build/oclock \
  --commit "${trial_commit}" \
  --sha256 "${trial_sha256}" \
  --duration 300
```

Always unbind afterwards, including after a failure:

```sh
sudo misc/junk/wiringpi-migration/managePhase5Lpd8806Binding.sh unbind --confirm UNBIND
```

## What to observe

The harness prompts for these, and they are the whole point of the gate:

- **Strip animation smoothness and color.** This is the observation that failed
  on both earlier candidates. The strip now has better than 2x timing margin
  per frame, but margin on a standalone frame is not the same as smooth
  animation under the real timer thread while the matrix and ADC also run.
- **Matrix content and legibility**, including correct digits and no flicker.
- **Automatic dimming.** Cover the light sensor for at least 12 seconds until
  the reported value crosses below 360, then uncover it and confirm recovery
  above 500.
- **Motion transitions**, HTTP responsiveness while the display is busy, and
  MQTT behavior.

## The light threshold question

The 360/500 hysteresis has been retained through every gate so far. The
controlled capture measured 997.3 uncovered, 179.0 fully covered, and 995.0
restored, which proves the sensor responds but says nothing about representative
room light: a hand over the sensor is not dusk.

**Outcome:** the question was answered and the answer was that 360 was
unreachable. See
[the trial result](wiringpi-phase5-application-trial-result.md); the thresholds
are now 460/700. The rest of this section is the gate as it was written.

This trial is the first opportunity to observe a real bright-to-dark transition.
Record the observed values and whether dimming engaged naturally. Do not change
the thresholds during the trial; that is a separate reviewed change informed by
what this run measures.

## Acceptance and rollback

Acceptance requires correct device behavior with margin and no missed
application deadlines, plus clean shutdown and no throttling. A pass authorizes
planning Phase 6; it is not itself a deployment.

Rollback remains physical: power off the Zero W and reconnect the preserved
Zero/Jessie unit. Nothing in this gate modifies that unit. The strip binding
must be removed whether the trial passes or fails, so the target returns to the
same safe state every other gate has left it in.

## Boundaries

This gate does not change wiring, the overlay, boot files, packages, service
configuration, or application defaults. It does not install the modern binary
as the deployed clock. It stops and restores the candidate service only on the
experimental Trixie card.
