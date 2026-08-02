# WiringPi migration Phase 5 initial hardware result

## Decision

The initial Zero W/Trixie `libgpiod` candidate did **not** pass Phase 5.
Display data, LED colors, motion, MQTT, HTTP shutdown, and onboard Wi-Fi all
worked, but the operator rejected both automatic dimming and output timing.
Do not deploy commit `91d0645`, change the light thresholds, or proceed to
Phase 6 on this evidence.

The preserved Zero/Jessie/WiringPi unit remains the production and rollback
baseline. The modern backend is still opt-in, so this result does not change
the legacy build or its GPIO behavior.

## Evidence reviewed

The reviewed archive is
`oclock-phase5-20260802T035203Z-j6kbP7pV.tar.gz`, SHA-256
`7bd79fbcc7b76ce7e1c1822044d8b05ac0b343e6a5330d1c365ab45af9649a87`.
It records:

- Raspberry Pi Zero W Rev 1.1, revision `9000c1`, running 32-bit Trixie;
- candidate commit `91d0645ff851a2e4c9cc74930ee58b69c9afeeea`;
- candidate binary SHA-256
  `a2bdd74e39b3f279a54c56d222ebe50135eeb6051e4f39f1cbe7b9a3332ef8f1`;
- `libgpiod.so.3` and `libatomic.so.1`, with no WiringPi dependency;
- no firmware throttling and a connected onboard Wi-Fi interface;
- successful motion transition, changing light values, MQTT connection,
  external-data update, HTTP shutdown, and visual display/strip correctness;
- failed operator observations for light-controlled dimming and acceptable
  display/strip response time.

The checksum file supplied with the archive contained its original absolute
`/tmp` pathname. The archive digest was therefore verified directly rather
than with `sha256sum --check` from the attachment directory.

## Light-sensor finding

The ADC path is responsive. After startup, the status samples stayed near
`1021`-`1022`, then fell monotonically through `1015`, `997`, `991`, `990`,
`981`, `887`, `794`, `705`, and `616` while the sensor was covered. The
recorded run ended there.

The application declares a room dark only below `360` and bright again at or
above `500`. It also reports a rolling average of ten readings taken every
600 ms. Consequently, the captured value never entered the dark state, and a
covering action can take about six seconds to replace the complete averaging
window. This is consistent with the reported failure; it is not evidence that
the GPIO input is stuck.

The Phase 0 WiringPi capture similarly showed the filtered value ramping down
and back up, reaching `390` during its short covered interval. Thresholds must
not be inferred from either partial transition. A later candidate trial must
hold the sensor fully covered until the filtered value crosses `360`, then
uncover it until it crosses `500`, while the operator confirms both brightness
transitions. If the value cannot cross those boundaries under sustained dark
and bright conditions, collect the two raw MCP3002 channels before changing
constants.

## Timing finding

The operator described the display and LED output as noticeably slower than
the preserved stack. The process samples support further investigation: the
candidate's lifetime-average `ps` CPU value was still `20.8%` near the end of
the one-minute trial, while the long-running Phase 0 WiringPi process reported
`2.9%`. Startup makes the short-run number an imperfect comparison, so these
figures are diagnostic evidence rather than a final performance budget. The
visible slowdown is nevertheless an acceptance failure by itself.

The current backend makes one `gpiod_line_request_set_value()` call for each
logical GPIO write and one `gpiod_line_request_get_value()` call for each
read. Every software-clock edge therefore crosses the userspace/kernel
boundary. A full 240-pixel LPD8806 frame alone clocks 5,760 data bits, with two
clock writes per bit plus data changes and latch clocks. HT1632 refreshes and
MCP3002 reads add more calls.

Requesting several lines together can make state changes atomic, but it cannot
eliminate the sequential rising and falling clock edges. The Linux GPIO
documentation explicitly recommends using an appropriate kernel subsystem
instead of bit-bashing protocols through the userspace character-device API:

- [GPIO character-device userspace API](https://docs.kernel.org/userspace-api/gpio/chardev.html)
- [kernel subsystems implemented using GPIO](https://docs.kernel.org/driver-api/gpio/drivers-on-gpio.html)

## Follow-up path

The unchanged harness prevents a direct move to the Zero W's fixed hardware
SPI pinout. Two approaches can preserve the physical wiring:

1. Move serial transfers to kernel `spi-gpio`/`spidev` buses on the existing
   offsets. This follows the kernel architecture, but requires transport
   changes in all three device drivers plus a custom, reversible Device Tree
   overlay.
2. Add a Zero-specific experimental backend that retains `libgpiod` for
   validation, direction, initial values, ownership, and cleanup, but uses the
   restricted Raspberry Pi `/dev/gpiomem` mapping for high-rate values. This is
   a smaller compatibility change and close to the preserved data path, but it
   deliberately bypasses the GPIO character-device value ioctls and is not a
   portable Linux backend.

The selected Zero W/Trixie image exposed the restricted mapping and passed the
read-only probe on 2026-08-02. The experiment is implemented under the new
explicit name `GPIO_BACKEND=gpiod-mmap`; see the
[fast-value-path report](wiringpi-phase5-fast-backend.md). The pure
`GPIO_BACKEND=gpiod` implementation and default `GPIO_BACKEND=wiringpi` build
remain unchanged.

The next gates are an Incus compile/test result, a native ARMv6 build with
recorded dependencies and checksum, and the same guarded hardware acceptance.
If the experimental candidate cannot meet the timing budget, discard it and
plan the kernel `spi-gpio` transport rather than reducing refresh behavior.
