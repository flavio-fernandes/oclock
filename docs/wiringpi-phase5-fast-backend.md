# Phase 5 BCM2835 fast-value-path experiment

## Scope and status

The initial pure-libgpiod Phase 5 candidate was functionally correct but failed
the timing gate. This commit adds a second, explicitly selected experimental
backend for the exact Raspberry Pi Zero W/Trixie target:

```sh
make GPIO_BACKEND=gpiod-mmap hardware
```

It is an ARM build and hardware-test candidate, not a deployment approval.
`GPIO_BACKEND=gpiod` remains the pure GPIO character-device implementation,
and the default `GPIO_BACKEND=wiringpi` legacy build is unchanged.

## Target prerequisite

The read-only follow-up archive
`oclock-phase5-fastgpio-20260802T042948Z-st04XPaP.tar.gz`, SHA-256
`c23458f5018fa846c95597ed110ee78a187c427f4f31055d975066afb419c5d2`,
passed every required check on the selected Zero W:

- Raspberry Pi Zero W Rev 1.1, revision `9000c1`;
- Raspbian 13/Trixie, ARMv6/armhf, kernel `6.18.39+rpt-rpi-v6`;
- `/dev/gpiomem` is character device `242:0`, mode `0660`, owner
  `root:gpio`;
- the invoking `pi` user belongs to `gpio`;
- the `raspberrypi_gpiomem` module owns
  `/sys/devices/platform/soc/20200000.gpiomem`;
- opening the device with `O_RDWR` and mapping it read-only at offset zero
  succeeded; a sample of BCM2835 `GPLEV0` returned `0x304081ff`;
- the collector requested, configured, and drove no GPIO line.

This experiment uses `/dev/gpiomem`, never `/dev/mem`, and therefore does not
hard-code a SoC peripheral base address.

## Ownership and value-path split

Both modern variants use the same `GpiodV2Gpio` implementation for:

- discovery of the chip labelled `pinctrl-bcm2835`;
- validation of all required line offsets and `GPIO<n>` names;
- direction plus initial output value in one line request;
- exclusive kernel ownership for the lifetime of a configured line;
- input/output misuse checks, serialization, contextual errors, and cleanup;
- restoration to input where the existing device drivers require it.

Only steady-state values differ:

| Build | Read/write path |
| --- | --- |
| `GPIO_BACKEND=gpiod` | `gpiod_line_request_get_value()` and `set_value()` |
| `GPIO_BACKEND=gpiod-mmap` | restricted BCM2835 SET, CLEAR, and LEVEL registers |

`Bcm2835GpioRegisters` intentionally has no direction, pull, alternate
function, drive-strength, or clock-management API. It accepts only bank-0
offsets `0..31`; every office-clock line is in that range. Writes use `GPSET0`
or `GPCLR0`, reads use `GPLEV0`, and full memory barriers preserve software
clock ordering.

The kernel line request does not observe each direct value write. It still
prevents another well-behaved GPIO character-device consumer from requesting
the line, while this process assumes sole responsibility for value changes.
This deliberate Raspberry-Pi-specific compromise is why the backend has a
separate name and remains experimental.

## Test coverage

The new register test uses an in-memory 4 KiB register page and verifies:

- supported and rejected offsets;
- high writes select `GPSET0` with the exact one-bit mask;
- low writes select `GPCLR0` with the exact one-bit mask;
- high and low reads sample the expected `GPLEV0` bit;
- an out-of-range write throws before accessing the page.

Repository boundary tests independently verify that:

- the pure `gpiod` build does not include the mmap value implementation;
- the `gpiod-mmap` build includes the common line manager plus only the
  BCM2835 value implementation and its factory;
- neither modern build selects or links WiringPi;
- the legacy and sandbox selections remain unchanged.

Incus can compile and test the code but cannot accept the mapped backend: its
safe `/dev/gpiomem` initialization failure is expected. Native ARMv6 build and
real timing remain exact-board gates.

The unsigned-character warning build exposed a pre-existing startup race while
this experiment was validated: an immediate server configuration failure could
broadcast termination before every application thread created its inbox. The
main thread now creates all inboxes before launching workers, and the smoke
test places a ten-second ceiling on invalid-bind shutdown. This changes no GPIO
or successful-runtime behavior, but makes the Phase 5 initialization-failure
gate deterministic on ARM.

## Native build handoff

Build from a fresh archive of the recorded PR commit on the Zero W:

```sh
make GPIO_BACKEND=gpiod-mmap hardware
file ./oclock
ldd ./oclock | grep -E 'libgpiod|libatomic|libwiringPi'
strings ./oclock | grep '^/dev/gpiomem$'
sha256sum ./oclock
```

The binary must be 32-bit ARM EABI5, resolve `libgpiod.so.3` and
`libatomic.so.1`, contain `/dev/gpiomem`, and have no WiringPi dependency.
Preserve the binary and checksum outside `/tmp` before transferring the
harness.

Run the updated `misc/verifyPhase5GpiodHardware.sh` with that exact binary,
commit, and checksum. The verifier now requires `/dev/gpiomem`, rejects a pure
libgpiod binary, samples interval CPU and HTTP latency, uses a 90-second
window, and requires sustained sensor values on both sides of the existing
`360`/`500` dimming thresholds.

Acceptance still requires normal display refresh and strip animation versus
the preserved WiringPi unit. A functional pass does not waive the overnight
soak, cold-boot Wi-Fi/service, or powered-off rollback gates.
