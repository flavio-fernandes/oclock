# WiringPi migration Phase 2 protocol tests

## Result

Phase 2 makes the existing GPIO behavior deterministic and testable without
selecting or deploying a new production backend. The fake accepts fixed or
queued input values and retains an ordered history of initialization,
direction, read, write, and delay operations. Focused tests reduce that raw
history to intentional device-level bitstreams and transaction boundaries.

The complete sandbox, sanitizer, warning, shutdown-stress, and Valgrind suite
passed in the isolated `oclock-dev` Incus environment. The default hardware
build still selects WiringPi, and the sandbox still has no WiringPi dependency.

## Explicit startup contract

`Gpio::configureOutput` now includes the required initial value. This lets a
future GPIO character-device backend request direction and value together,
avoiding an unintended pulse when its API supports that operation.

The accepted startup values are:

| Device | BCM GPIO | Role | Direction and initial value |
| --- | ---: | --- | --- |
| HT1632 | 6 | chip select | output, high (deselected) |
| HT1632 | 13 | write clock | output, low |
| HT1632 | 19 | data | output, low |
| HT1632 | 26 | select-chain clock | output, low |
| LPD8806 | 21 | data | output, low |
| LPD8806 | 20 | clock | output, low |
| MCP3002 | 17 | clock | output, low |
| MCP3002 | 27 | controller input / ADC output | input |
| MCP3002 | 22 | controller output / ADC input | output, low |
| MCP3002 | 4 | chip select | output, high (inactive) |
| PIR sensor | 10 | motion | input; high means motion |

These values follow the first intentional state used by each device protocol.
They are not inferred from a one-time pin snapshot.

WiringPi cannot provide the same line-request operation that a modern GPIO
character-device API can. Its compatibility implementation therefore performs
`pinMode(pin, OUTPUT)` followed immediately by `digitalWrite` with the stated
initial value. This preserves the deployed library and its established
direction-before-write behavior while making the desired state explicit. A
compile-only test builds this backend against a minimal WiringPi declaration
fixture with ARM-compatible unsigned-`char` warnings treated as errors.

## Fake backend

`FakeGpio` is public test support under `src/gpio/FakeGpio.h`. It provides:

- a configurable initialization result;
- a persistent input value per BCM GPIO;
- queued per-read input values, followed by the persistent value;
- an ordered, thread-safe operation history and history reset;
- optional real-time delays for the sandbox application;
- an opt-in yield after each operation for concurrency tests.

The normal sandbox factory retains real-time millisecond delays and default-low
inputs, matching the Phase 1 fake. Tests instantiate a no-sleep fake directly.
Configuration helpers do not appear in the GPIO history.

## Protocol coverage

`tests/gpio_protocol_tests.cpp` checks:

- HT1632 startup directions and levels; MSB-first command ID and PWM command
  bits; the daisy-chain selection pulses for all 16 active controllers; and 16
  complete render transactions, each containing the write ID, seven-bit
  address, and 32 four-bit words;
- LPD8806 startup levels; the full 720-byte transfer for 240 pixels; GRB byte
  order at both ends of the strip; unchanged pixels encoded as `0x80`; and 64
  zero latch clocks after the frame;
- MCP3002 startup directions and levels; channel-one command bits; ten queued
  samples converted to the expected integer; 15 total clocks; ten reads; and
  chip select remaining active for the complete exchange;
- PIR input configuration and the preserved low/false, high/true mapping;
- MCP3002 destruction returning clock, chip select, and controller-output pins
  to inputs, while its controller-input pin remains an input;
- LPD8806 pin replacement returning the prior data and clock pins to inputs
  before configuring the replacement pair;
- two concurrent device operations remaining as two complete, non-interleaved
  transactions under the shared recursive GPIO mutex.

Assertions are made on decoded protocol bits, byte counts, selection clocks,
and ownership boundaries. They do not depend on private helper calls, object
layout, or allocation behavior.

## Validation

The isolated Incus run rebuilt from a clean tree and passed:

```text
make sandbox
make test
make check-arm-warnings
make valgrind
git diff --check
```

This includes legacy CLI compatibility, the WiringPi boundary check, core
AddressSanitizer and UndefinedBehaviorSanitizer tests, the new GPIO protocol
tests with leak detection, a compile-only legacy backend check, the
unsigned-`char` warning build, both smoke paths, 20 graceful-shutdown stress
iterations, and the Valgrind smoke test. The concurrency-sensitive protocol
test also passed 100 consecutive local iterations.

The host checkout lacks the libevent development headers, so its full
application build is not used as evidence. Its focused protocol and legacy
backend compile checks passed; leak detection was exercised in Incus because
the desktop host runs test processes under tracing.

## Compatibility and next gate

Phase 2 does not change the BCM pin map, wiring, global GPIO mutex, device
bit order, transfer length, polling model, service configuration, build
selection, or deployed binary. Raw GPIO traces are generated during tests and
are not committed.

Phase 3 requires selecting the Pi Zero operating-system image and the matching
`libgpiod` API before adding a modern backend. That work must use a separate SD
card and must not upgrade the known-good Jessie production card in place.
