# Phase 5 MCP3002 native-IIO conversion

## Status

The application-side conversion passes hardware-free tests, the complete
Trixie Incus suite, and the exact-board first-read gate. Both single-ended
channels returned valid 10-bit values and the hardware/service state remained
unchanged. See the [accepted result](wiringpi-phase5-mcp3002-iio-result.md).
The whole application remains prohibited.

## Design

The live Device Tree overlay binds the existing MCP3002 wiring to Linux's
native `mcp320x` driver. The driver exposes the two single-ended inputs as:

```text
in_voltage0_raw
in_voltage1_raw
```

The application now owns a small `AnalogInput` boundary. Its hardware
implementation searches `/sys/bus/iio/devices/iio:device*`, resolves each
candidate's `of_node`, and accepts exactly one path ending in:

```text
/oclock-adc-spi/mcp3002@0
```

It then requires the IIO name `mcp3002` and readable single-ended attributes
for both channels. It never assumes `iio:device0` or a dynamic SPI bus number.
Reads accept only strict decimal values from 0 through 1023.

`LightSensor` still reads both channels every 600 ms, averages each pair, and
retains the ten-sample moving average. This transport change deliberately did
not recalibrate dimming, so it kept the existing 360/500 hysteresis thresholds.
They were later replaced with a measured **460/700** — see the note at the end
of this document.

## Build boundary

The modern hardware build includes `src/adc/linuxIioAnalogInput.cpp`; the
sandbox uses a deterministic fake. The former GPIO-bit-banged
`mcp300x/mcp300x.cpp` remains only in its historical wire-protocol test and is
not linked into either application binary.

Fixture tests cover:

- discovery by Device Tree identity with a nonzero dynamic IIO number;
- separate channel 0 and channel 1 reads;
- pre-initialization and invalid-channel rejection;
- strict 10-bit range and malformed-value rejection;
- wrong IIO identity and missing channel attributes;
- missing and ambiguous Device Tree matches.

## First-read gate

Before any whole-application run, build `phase5-mcp3002-read` from a pinned PR
commit and use `misc/junk/wiringpi-migration/verifyPhase5Mcp3002FirstRead.sh` to read each raw channel
separately. The gate
must require the exact Zero W/Trixie target, accepted overlay, native
`mcp320x` binding, inactive `oclock.service`, and an unchanged strip binding.
It must archive only sanitized hardware evidence.

The first read passed all 13 checks at commit `d47629b`. It establishes
transport correctness, not calibration. The following operator-assisted
capture recorded controlled covered/uncovered values for both channels before
making any threshold decision.
`misc/junk/wiringpi-migration/collectPhase5Mcp3002Calibration.sh` implements three ten-sample windows
at the application's 600 ms sampling interval: uncovered, covered, and
uncovered again. It reports channels separately and does not change thresholds.

That capture passed; see the
[controlled light result](wiringpi-phase5-mcp3002-calibration-result.md). It
proves a strong covered/uncovered response and preserved the existing
thresholds pending representative-room observation in a later application gate.

That observation happened during the
[whole-application trial](wiringpi-phase5-application-trial-result.md) and
changed the answer. A covered sensor reads far darker than a dark room: the
covered average here was 179.0, while the room with its light actually off
plateaus between 355 and 478. The original 360 low-water mark was therefore
unreachable in practice, and the thresholds are now the measured **460/700**.
