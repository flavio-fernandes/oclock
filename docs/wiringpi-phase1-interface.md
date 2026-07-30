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

Do not start that candidate while `oclock.service` is running. Share the
command output first. A short maintenance-window functional check will then:

1. stop the service;
2. run this candidate as root on a loopback test port;
3. check display, LED strip, light changes, motion changes, status, and clean
   HTTP shutdown;
4. restart the unchanged production service;
5. confirm it is active and the known-good hardware is normal.

The Phase 0 binary remains the rollback artifact throughout this check.
