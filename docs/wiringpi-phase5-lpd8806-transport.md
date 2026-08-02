# Phase 5 LPD8806 SPI transport

## Status

The project-owned SPI output boundary, deterministic fake, Linux `spidev`
implementation, and opt-in LPD8806 integration are implemented. Hardware-free
tests prove that each 240-pixel `show()` submits one 728-byte transfer: 720 GRB
bytes followed by eight zero latch bytes. The legacy GPIO implementation and
default build remain unchanged.

This checkpoint is not hardware acceptance. It must build natively on the
Zero W before a separate guarded verifier is allowed to bind, open, and send
the first frame.

## Build selection

Plain `make` and `make hardware` still select WiringPi and the original
bit-banged strip. The experimental strip transport requires an exact explicit
pair:

```sh
make GPIO_BACKEND=gpiod-mmap STRIP_TRANSPORT=spidev hardware
```

The Makefile rejects `STRIP_TRANSPORT=spidev` with the legacy or pure-libgpiod
backend. This is deliberately still a partial modern profile: the MCP3002
application path has not moved to native IIO, so do not run the resulting
whole application while the overlay owns the ADC GPIOs.

## Transport behavior

[`SpiOutput`](../src/spi/SpiOutput.h) separates byte transfers from GPIO edge
operations. The Linux implementation:

- discovers exactly one strip child by the resolved Device Tree suffix
  `/oclock-strip-spi/lpd8806@0`;
- derives `/dev/spidev*` from that live child and never assumes a bus number;
- requires the previously reviewed binding to have created a character
  device;
- opens it with close-on-exec;
- configures mode 0, no chip select, eight bits per word, MSB first, and
  1 MHz;
- verifies the effective settings;
- submits each complete frame with one `SPI_IOC_MESSAGE(1)` call;
- rejects missing initialization, missing data, oversized payloads, short
  transfers, and kernel errors.

The transport does not bind or unbind a driver and does not edit boot state.
Those lifecycle operations stay in the guarded manager until the final
service installation is designed and exercised.

## LPD8806 compatibility behavior

The existing constructor retains configurable GPIO data and clock pins,
bit-by-bit output, pin replacement, shared recursive locking, and delay
behavior. A second constructor accepts `SpiOutput` without removing or
rewriting the legacy path.

In SPI mode:

- `begin()` sends the same all-zero latch clocks as the old implementation;
- `show()` preserves the full 240-pixel update rather than the disabled
  largest-changed-pixel optimization;
- GRB ordering and the high marker bit in every color byte are unchanged;
- the data and latch are one contiguous transfer;
- no GPIO configure or write operation is used for the strip;
- attempting to replace nonexistent SPI GPIO pins is rejected.

The existing shared mutex still brackets each complete strip operation. This
preserves serialization expectations while the other transports are migrated.

## Deterministic validation

`make test-spi-output` uses `FakeSpiOutput` and sanitizer instrumentation to
verify:

- initialization success and failure;
- rejection of a transfer before initialization;
- the initial eight-byte zero latch;
- exactly one transfer per `show()`;
- exact first and last GRB pixels in a 720-byte payload;
- `0x80` for every untouched color component;
- exactly eight trailing zero latch bytes;
- no strip GPIO operations;
- rejection of SPI-mode pin replacement;
- preserved millisecond-delay delegation;
- no transfer for a zero-length strip.

The original GPIO protocol test still proves the bit-banged wire sequence and
pin replacement. Build-boundary tests prove that the legacy and sandbox
profiles select the null SPI factory, while only the explicit modern pair
selects the Linux implementation.

## Native build gate

Build from an archive of the expected PR commit on the Zero W, but do not bind
the strip and do not run the binary. Then use the metadata-only collector:

```sh
time make GPIO_BACKEND=gpiod-mmap STRIP_TRANSPORT=spidev hardware
file ./oclock
ldd ./oclock | grep -E 'libgpiod|libatomic|libwiringPi'
strings ./oclock | grep -F '/oclock-strip-spi/lpd8806@0'
sha256sum ./oclock
dpkg-query -S /usr/include/linux/spi/spidev.h

misc/collectPhase5Lpd8806Build.sh \
  --binary ./oclock \
  --commit "${transport_commit}"
```

The accepted binary must be ARM EABI5, link libgpiod and libatomic without
WiringPi, and contain the Device Tree discovery suffix. Preserve that binary
for the subsequent first-transfer gate.

## First-transfer gate requirements

After the native build passes, add a separate verifier that:

1. keeps `oclock.service` inactive;
2. binds only the discovered strip child;
3. opens and verifies the resulting character device;
4. sends a controlled all-off frame and measures wall-clock transfer time;
5. asks the operator to confirm that the strip stays visibly off and stable;
6. captures kernel, device, CPU, timing, and throttling evidence;
7. closes and unbinds on every normal or error exit.

Do not run the full application until the MCP3002 application path uses IIO;
the live overlay already owns its former GPIO pins.
