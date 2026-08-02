# WiringPi migration Phase 3 target baseline

## Result

The 2026-08-02 UTC target capture establishes the software and GPIO API target
for the modern backend:

- Raspberry Pi OS 32-bit, Raspbian 13.6 (Trixie);
- Linux `6.18.39+rpt-rpi-v6`, running `armv6l` with `armhf` packages;
- libgpiod and its C++ binding version 2.2.1;
- GPIO character-device ABI v2 line-information support;
- a 54-line GPIO chip labelled `pinctrl-bcm2835`;
- office-clock BCM GPIO numbers mapping directly to offsets 4, 6, 10, 13,
  17, 19, 20, 21, 22, 26, and 27.

This is sufficient to select a libgpiod v2 backend with chip-label discovery.
The raw archive remains outside Git.

## Evidence provenance

| Item | Captured value |
| --- | --- |
| Archive | `oclock-phase3-20260802T023816Z-IDejwwC4.tar.gz` |
| SHA-256 | `ea2de8d963ccddda69b751b78342bee0f51ac416277b92c5a90c8a8886281b00` |
| Required checks | 0 failures |
| Explained warnings | 1 collector pattern defect |
| OS | Raspbian GNU/Linux 13.6 (Trixie) |
| Kernel | `6.18.39+rpt-rpi-v6` |
| Machine/package architecture | `armv6l` / `armhf` |
| Compiler | Raspbian GCC/G++ 14.2.0 |
| Firmware | 2026-05-21, revision `288930ab4712b99596f32732664aaaeb881ef1e0` |
| libgpiod | 2.2.1 (`2.2.1-2+rpi1+deb13u1`) |
| GPIO chip | `gpiochip0 [pinctrl-bcm2835] (54 lines)` |
| GPIO ABI probe | v2 line information available |
| Throttling | `throttled=0x0` |

The operator corrected the collector locally before the accepted run because
Raspbian reports the native multi-arch development package as
`libgpiod-dev:armhf`, while the original check accepted only the unqualified
Debian package name. The repository check now accepts either form.

The remaining warning is also a collector defect rather than a power warning.
`vcgencmd get_throttled` returned `throttled=0x0`, but the collector expected
the nonexistent prefix `get_throttled=0x0`. The repository pattern is corrected
and covered by its compatibility test.

## Board qualification

The target capture ran on a **Raspberry Pi Zero W Rev 1.1**, revision `9000c1`.
The production Phase 0 baseline is a **Raspberry Pi Zero Rev 1.2**, revision
`900092`. Both report an ARMv6 BCM2835 and expose the required GPIO controller
and offsets, so the Zero W capture is accepted for operating-system, compiler,
libgpiod API, chip-discovery, and line-offset selection.

It is not exact-board timing evidence. Phase 5 must still build and run both
backends on the production non-W Zero Rev 1.2 with the real display, LED strip,
ADC, and motion sensor. No timing, CPU-budget, or deployment conclusion is
drawn from the Zero W capture.

The Zero W kernel exposes `/dev/gpiochip0` plus a compatibility symlink
`/dev/gpiochip4`. This confirms why the backend must identify the chip by its
`pinctrl-bcm2835` label instead of assuming either device number.

## GPIO and ABI evidence

`gpiodetect` and the read-only ioctl probe independently reported:

```text
gpiochip0 [pinctrl-bcm2835] (54 lines)
chip_name: gpiochip0
chip_label: pinctrl-bcm2835
chip_lines: 54
v2_line_info: available
```

`gpioinfo` reported every office-clock offset as an unclaimed input on the
fresh image, with names matching `GPIO4` through `GPIO27` as applicable. The
kernel configuration includes `CONFIG_GPIOLIB=y`, `CONFIG_GPIO_CDEV=y`, and
the optional v1 compatibility ABI. The backend will use only the v2 API.

The collector did not request lines, sample electrical values, or change GPIO
direction. All line-driving behavior remains deferred to the guarded hardware
trial.

## Build-attempt interpretation

The operator's subsequent plain `make hardware` stopped while compiling
`pulsar/worker.c` because `event2/event.h` was absent. This is not a GPIO,
kernel, or libgpiod failure: `libevent-dev` was not part of the metadata-only
collector prerequisites.

More importantly, plain `make hardware` still intentionally selects WiringPi
at this point. Installing libevent alone would eventually reach the absent
legacy WiringPi headers or library on the clean Trixie card. The next valid
target build is the explicit libgpiod build added by Phase 3; it also requires
`libevent-dev` and `libmosquitto-dev`.

## Selection decision

The modern backend target is now:

- Raspberry Pi OS 32-bit based on Debian 13/Trixie;
- ARMv6/armhf;
- Linux GPIO character-device ABI v2;
- libgpiod 2.2.x API;
- GPIO chip selected by the verified `pinctrl-bcm2835` label;
- BCM GPIO numbers used directly as offsets only after the selected chip's
  label and line count are validated.

WiringPi remains the default hardware backend for the Jessie card. The new
backend remains an explicit opt-in until the exact production-board trial and
rollback gates pass.
