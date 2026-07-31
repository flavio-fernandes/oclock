# WiringPi migration Phase 1 interface

## Result

Phase 1 isolates platform GPIO access without selecting a new production GPIO
stack. The application and device drivers now depend on the project-owned
`Gpio` interface. Plain `make` and `make hardware` still select and link the
WiringPi backend; sandbox, tests, and ARM warning builds select the fake
backend and do not link WiringPi.

This phase is deliberately a one-for-one refactor. It does not change BCM pin
numbers, directions, read/write ordering, the global recursive mutex, service
configuration, privileges, wiring, or boot settings.

## Interface and ownership

`src/gpio/Gpio.h` owns these platform-neutral operations:

- initialize BCM-numbered GPIO access;
- configure a pin as input or output;
- read or write a digital value;
- perform the millisecond delay historically supplied by WiringPi.

`main` creates the selected backend, initializes it, and retains ownership until
every worker has joined and every device singleton has shut down. Worker
parameters carry a non-owning pointer to the same backend. `Display`,
`LedStrip`, `LightSensor`, and `MotionSensor` pass it by reference to the
HT1632, LPD8806, and MCP300x drivers.

There is one backend object and one existing GPIO mutex. Phase 1 does not split
line ownership or change locking. In particular, the motion read retains its
existing locking behavior rather than quietly combining the interface refactor
with a concurrency change.

## Backend selection

| Build | Backend source | WiringPi link |
| --- | --- | --- |
| `make` / `make hardware` | `src/gpio/wiringPiGpio.cpp` | yes |
| `make sandbox` | `src/gpio/fakeGpio.cpp` | no |
| tests and ARM warning build | `src/gpio/fakeGpio.cpp` | no |

Both backend sources implement the same `createGpio()` factory. The Makefile
selects exactly one implementation, so application code contains no
`FAKE_WIRING` conditional and cannot silently choose a backend at runtime.

The legacy implementation is the only production translation unit allowed to
include `wiringPi.h` or call `wiringPiSetupGpio`, `pinMode`, `digitalRead`,
`digitalWrite`, or `delay`. It maps each interface operation directly to the
same WiringPi operation. The fake returns the same default low input value as
the previous fake and retains its C++ millisecond sleep.

## Preserved operation ordering

Phase 1 intentionally preserves separate “configure output” and “write value”
operations. Adding an initial write while changing direction would alter the
known-good call sequence without evidence for every safe idle level.

Phase 2 will record the exact initialization and protocol traces. Before a
modern backend is enabled, the interface can then be extended to request an
explicit initial output value atomically where supported, with a proven
legacy-equivalent fallback. No initial level should be inferred solely from
the instantaneous Phase 0 `gpio readall` snapshot.

The source conversion retains:

- HT1632 data, write-clock, chip-select, and auxiliary-clock ordering;
- LPD8806 data transitions, clock pulses, and latch clocks;
- MCP3002 command, sampling, clock, and chip-select ordering;
- motion input polarity;
- MCP3002 and LPD8806 direction-release behavior;
- WiringPi delay behavior for the legacy display fade and LED scan mode.

## Repository enforcement

`tests/gpio-boundary.sh` fails when:

- production source accesses WiringPi outside the legacy backend;
- the public interface or fake backend mentions WiringPi;
- an accepted BCM pin constant changes;
- the hardware dry-run does not compile the legacy backend and link WiringPi;
- the sandbox dry-run selects or links WiringPi.

The check is part of `make test`. Phase 2 will add semantic GPIO trace tests;
the Phase 1 boundary test prevents new platform coupling in the meantime.

## Pi Zero confirmation

### Jessie build: passed

The non-disruptive production build was completed on 2026-07-30 from commit
`cf8e543e5638abc76732ca7247a1d65dacab981d`. It compiled and linked on the Pi
Zero in 3 minutes 38 seconds. The result was a dynamically linked, 32-bit ARM
EABI5 executable targeting GNU/Linux 2.6.32, and `ldd` resolved
`libwiringPi.so` to `/usr/local/lib/libwiringPi.so`.

The source was extracted with `git archive`, so the resulting directory
intentionally had no `.git` metadata. A `git status` failure in that directory
is expected and does not affect the build evidence.

### Reproducing the build

Automated x86 validation cannot prove that the Jessie compiler accepts the
hardware backend or that virtual dispatch preserves timing on the Pi Zero.
Perform the following build without changing or stopping the deployed
worktree:

```sh
cd /home/pi/oclock.git
git fetch origin agent/plan-wiringpi-migration
phase1_commit=$(git rev-parse FETCH_HEAD)
phase1_dir=$(mktemp -d /tmp/oclock-phase1-build-XXXXXXXX)
git archive FETCH_HEAD | tar -x -C "${phase1_dir}"
cd "${phase1_dir}"
make hardware
file ./oclock
ldd ./oclock | grep wiringPi
printf 'phase1_commit=%s\nphase1_dir=%s\n' \
    "${phase1_commit}" "${phase1_dir}"
```

Do not start that candidate while `oclock.service` is running.

### Maintenance-window functional check

Extract the guarded verifier from the PR without changing the deployed
worktree, then run it against the already-built candidate:

```sh
cd /home/pi/oclock.git
git fetch origin agent/plan-wiringpi-migration
phase1_verifier=/tmp/verifyPhase1Hardware.sh
git show FETCH_HEAD:misc/verifyPhase1Hardware.sh >"${phase1_verifier}"
chmod 0755 "${phase1_verifier}"
sudo "${phase1_verifier}" \
    --binary /tmp/oclock-phase1-build-mileEVez/oclock \
    --commit cf8e543e5638abc76732ca7247a1d65dacab981d
```

Read the summary before typing the required `RUN` confirmation. During the
60-second observation window, watch the display and LED strip, cover and
uncover the light sensor, move into and out of the PIR field, and trigger the
normal external data feed.

The script:

1. stops the service;
2. runs this candidate as root on the normal production-compatible
   `0.0.0.0:80` HTTP endpoint, allowing the existing external controller to
   reach it;
3. checks display, LED strip, light changes, motion changes, MQTT broker
   connectivity, the external data feed, status, and clean HTTP shutdown;
4. restarts the unchanged production service;
5. confirms it is active and the known-good hardware is normal.

It traps normal exit, errors, interruption, and terminal hangup and attempts to
stop the candidate and restart the production service in every case. It
produces a timestamped archive and checksum containing the status samples and
operator answers. The Phase 0 binary remains the rollback artifact throughout
this check.

The first maintenance run on 2026-07-30 provided useful but non-acceptance
evidence. The candidate exited cleanly after HTTP shutdown, production was
restored, the status endpoint showed both motion states and light values from 0
through 694, and 51 of 52 samples reported a healthy MQTT broker connection.
The operator also observed normal display, LED-strip, motion-sensor, and
light-sensor behavior.

That run exposed two verifier defects:

- prompts emitted on standard output were captured with their answers, causing
  every affirmative operator observation to be reported as a failure;
- the loopback-only test endpoint prevented the normal external controller
  from reaching the candidate, so its data-driven display update could not be
  checked.

The verifier now emits prompts on standard error and runs the candidate on the
same HTTP bind address and port as production during the guarded maintenance
window. A repeat run is required to close the operator and external-input
acceptance gates. The raw result archive remains outside Git.
