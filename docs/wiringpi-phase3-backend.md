# WiringPi migration Phase 3 backend handoff

> Historical checkpoint: the explicit backend command below records the
> Phase 3 experiment. The selector is no longer supported; see the
> [modern build policy](wiringpi-modern-build-policy.md).

## Status

The libgpiod v2 backend is implemented as an explicit build option. It compiles
and links against libgpiod 2.2.1 in an isolated Debian 13/Trixie Incus
container, and the complete fake-GPIO and application test suite passes there.
Commit `91d0645` also builds on the ARMv6/armhf target image. No libgpiod binary
has yet driven production hardware.

The existing Jessie deployment remains unchanged: plain `make` and
`make hardware` select WiringPi, retain the `oclock` filename, and preserve the
original `make` ownership and setuid behavior. The WiringPi source remains in
the tree.

## Building the modern backend

Install the build dependencies on Raspberry Pi OS 32-bit Trixie:

```sh
sudo apt update
sudo apt install -y build-essential pkg-config libevent-dev \
    libmosquitto-dev libgpiod-dev
```

Then build without changing ownership, mode, or the running service:

```sh
make GPIO_BACKEND=gpiod hardware
```

The build rejects unknown backend names and rejects libgpiod versions other
than major version 2. Because both backends produce `oclock`, every hardware
build relinks the executable; switching a populated build tree cannot silently
reuse the other backend's binary.

The ARMv6/GCC 14 target emits an out-of-line operation for the application's
existing 64-bit `std::atomic` counter. The modern build therefore links
`libatomic` explicitly. This dependency is scoped to `GPIO_BACKEND=gpiod`, so
the legacy Jessie/WiringPi link command remains unchanged.

## Backend behavior

`src/gpio/gpiodV2Gpio.cpp` implements the existing project-owned `Gpio`
interface using the libgpiod v2 C API:

- it searches `/dev/gpiochip*` and selects the chip whose label is
  `pinctrl-bcm2835`, rather than assuming `gpiochip0` or `gpiochip4`;
- it validates the chip line count and the captured `GPIO<N>` names for every
  office-clock BCM offset before any line is requested;
- each configured pin has one line request, with output direction and initial
  value applied in the initial request to avoid a direction/value gap;
- BCM numbers remain direct offsets only after chip validation;
- reads, writes, and reconfiguration are serialized because libgpiod does not
  provide internal synchronization for a shared request object;
- failures include the operation, selected chip path, BCM offset, and system
  error where available;
- project delay calls use `std::chrono` and retain millisecond semantics.

The first backend intentionally preserves the existing polling and software
bit-banging behavior. Edge events, `spidev`, batching, and timing changes are
outside Phase 3.

## Host validation

The isolated validation environment was Debian 13/Trixie on x86_64 with GCC
14.2.0 and libgpiod 2.2.1. It established:

- a clean `make GPIO_BACKEND=gpiod hardware` compile and link;
- a dynamic dependency on `libgpiod.so.3` and no WiringPi dependency;
- a clear, nonzero initialization failure when the container exposed no
  `/dev/gpiochip*` device;
- passing compatibility, backend-boundary, fake protocol, sanitizer, ARM
  unsigned-character warning, smoke, and shutdown tests.

The selected compiler also exposed four source files that used
`std::runtime_error` through transitive headers. They now include
`<stdexcept>` directly; this does not change runtime behavior.

Host validation cannot establish ARMv6 compatibility, electrical behavior, or
software-bit-bang timing. Those remain Raspberry Pi gates.

The first ARMv6 build compiled every source file and then exposed the missing
explicit `libatomic` link dependency. This was a target-linker finding rather
than a GPIO failure. Repeating the build from commit `91d0645` passed in 4
minutes 21 seconds and produced:

```text
ELF 32-bit LSB executable, ARM, EABI5, interpreter /lib/ld-linux-armhf.so.3
libgpiod.so.3 => /lib/arm-linux-gnueabihf/libgpiod.so.3
libatomic.so.1 => /lib/arm-linux-gnueabihf/libatomic.so.1
```

No WiringPi dependency was present. The binary SHA-256 was
`a2bdd74e39b3f279a54c56d222ebe50135eeb6051e4f39f1cbe7b9a3332ef8f1`.

## Remaining gates

1. Connect the Zero W/Trixie replacement to the unchanged office-clock wiring;
   retain the original Zero/Jessie unit unchanged and powered off.
2. In Phase 5, run the guarded modern candidate on the Zero W target.
3. Validate display, LED strip, ADC/light, motion, MQTT, shutdown, and timing.
4. Validate boot-time onboard Wi-Fi reconnection and complete an overnight
   network and application soak.
5. Exercise rollback by powering off the Zero W and reconnecting the preserved
   Zero/Jessie unit.

The Zero W capture selects the exact modern platform but is not a substitute
for driving the real peripheral load.
