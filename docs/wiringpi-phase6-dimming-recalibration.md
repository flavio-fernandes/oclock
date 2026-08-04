# Phase 6 dimming recalibration from published telemetry

## Decision

The dimming high-water mark moved from **700 to 600** on 2026-08-04. The
low-water mark stays at **460**. The pair is now `460/600`.

This is the first threshold decision on this project made from a long run of
production telemetry rather than from a bench observation. The office lux feed
has been published to Adafruit IO continuously since 2020, so the migration has
sixty days of before-and-after history at roughly five-minute resolution.

The same review found that the rationale recorded alongside the previous
`460/700` values was **factually wrong**, and that the migration changed the
sensor's range in a way nobody had characterised. Both are corrected below.

## Why the feed is directly comparable to the constants

`MqttClient::publishLightSensor()` publishes `LightSensor::getLightValue()`,
which is the mean of the last ten samples — the very value `updateDim()`
compares against these thresholds. The feed is therefore not a raw ADC trace
that needs interpreting; it is the decision variable itself, sampled every few
minutes. Replaying the hysteresis over the feed reproduces exactly what the
clock did.

The caveat is resolution, not correctness: the sensor thread reads every 600 ms
and the moving average spans six seconds, while the feed publishes every ~5
minutes. Excursions shorter than a publish interval are invisible here.

## Evidence

| Item | Value |
| --- | --- |
| Source | `https://io.adafruit.com/flaviof/feeds/home-lux.office` |
| Window reviewed | 2026-06-05T16:42Z .. 2026-08-04T16:48Z |
| Total samples | 15,884 |
| Pre-migration samples | 15,357 (through 2026-08-01 23:00 EDT) |
| Post-migration samples | 448 |
| Archived copy | [`data/home-lux-office-2026-06-05-to-2026-08-04.csv.gz`](data/home-lux-office-2026-06-05-to-2026-08-04.csv.gz) |
| Archive SHA-256 | `2de20b82a7913302298801b9b6ff26b5afdaf3845cc1c459ecb56afcfc1a08e6` |

The archive is committed rather than kept outside Git, which is a deliberate
exception to this project's evidence convention. Adafruit IO expires feed data
after 60 days and the feed is being made private, so the upstream source will
not exist by October 2026. At 144 KB compressed it is worth carrying: it is the
sole basis for these constants and for the correction below.

**On the boundary.** The guarded application trial ran the new code
intermittently starting 2026-08-01 23:48 EDT, which puts three 1022 readings
into what would naively be "pre-migration" data. The pre-migration window
therefore ends at 2026-08-01 23:00 EDT. Without that cut the pre-migration
maximum is contaminated by the trial itself.

## What the migration actually changed

The dark floor did not move. The bright ceiling did, and it now clips.

| Day | p05 | median | p95 | max |
| --- | ---: | ---: | ---: | ---: |
| 2026-07-30 | 198 | 473 | 661 | 707 |
| 2026-07-31 | 152 | 458 | 654 | 725 |
| 2026-08-01 | 151 | 465 | 644 | 685 |
| 2026-08-02 | 164 | 465 | 1022 | 1022 |
| 2026-08-03 | 149 | 752 | 1022 | 1023 |
| 2026-08-04 | 150 | 617 | 1022 | 1023 |

Over the full 57-day pre-migration window the ceiling is firm: p95 640, p99 658,
maximum **725**, with 11 samples of 15,357 at or above 700 and **none at all
above 750**. Post-migration the plateau is 1022–1023, hard against the 10-bit
ceiling, with **36.2%** of samples pinned there. A live two-channel read on
2026-08-04 returned ch0 1021–1023 and ch1 1023 on all ten samples — both
channels rail together.

**A uniform scale factor is ruled out.** The overall 5th percentile went 187 to
150 and the deep-night median went 253 to 196 — both slightly *down*, not up.
Any divide-by-two in the retired path would have doubled the dark end.
Reading the retired bit-bang in `mcp300x/mcp300x.cpp` confirms it: three config
bits, a fourth tick clocking MSBF, then samples taken after each of clocks 5
through 14, which is a correct B9..B0 capture with no shift or mask.

The mechanism behind the top-end shift is **not established**. The leading
hypothesis is that the retired `_tickClock()` sampled Dout immediately after the
falling edge with no settling allowance, so bits transitioning 0->1 could be
read early as 0 — an error that grows with the number of set bits, hence severe
at 1022 (`1111111110`) and negligible at 150 (`0010010110`). That fits the
observed shape but has not been measured, and the hardware to measure it is
retired. Treat it as an open question.

## The correction to the previous rationale

`src/lightSensor.cpp` previously recorded:

> The original 360 was therefore unreachable: the darkest the room ever got
> still read above it, so dimming could never engage.

The telemetry contradicts this. **5,049 of 15,357 pre-migration samples (32.9%)
were below 360.** Replaying the original `360/500` hysteresis over the entire
57-day pre-migration series produces **106 dim events and 105 bright events** —
roughly two transitions a day, every day. It behaved exactly as designed,
entering dark nightly around 00:48–01:59 and returning to bright around
07:47–09:59.

The bench measurement that produced the claim ("452 to 478, sustained and fully
settled") was taken at a moment that was not deep-night dark. It was a real
reading of a real condition; it simply was not the darkest condition the room
reaches.

This does not invalidate moving to 460 — the scale genuinely changed underneath
the thresholds — but the stated reason was wrong and is now corrected in the
source comment, in `tests/compatibility.sh`, and here.

## Why the bands are cleaner now

Splitting by hour of day exposes something the covered-sensor bench test could
not:

| | night 01-05h p95 | day 09-17h p05 | separation |
| --- | ---: | ---: | ---: |
| Pre-migration (n=15,357) | 630 | 420 | **-210, overlapping** |
| Post-migration (n=448) | 408 | 615 | **+207, clean** |

Before the migration a bright night could read *higher* than a dull day, which
is why any threshold pair was marginal. After it, the two conditions are
separated by roughly 180–200 counts. For a two-state dim/bright decision the
clipping at the top costs nothing and the separation is a straight improvement.

The cost is elsewhere: the feed is no longer usable as a lux *measurement*,
because everything above roughly 1022 is indistinguishable. Restoring that
would be a change to the resistor divider, not to software.

## Why 600 and not 700

Both marks were checked against the post-migration series by replaying
`updateDim()`'s hysteresis.

`460` is well placed and does not move. It sits above the night p95 of 408, so
nights reliably engage, and 127 counts below the daytime minimum of 587, so
daylight cannot trip it.

`700` sat **above the daytime floor of 587**. On 2026-08-04, a day lit only by
daylight, the clock entered dim at 00:03 and did not return to bright until
09:52. 2026-08-03 escaped this only because a room lamp came on at 06:23 and
drove the value straight to 1022.

Replaying the 08-04 morning:

| High-water mark | Returns to bright |
| ---: | --- |
| 700 (previous) | 09:52 |
| 600 (selected) | 07:24 |
| 560 | 06:48 |

600 leaves 124 counts of margin above the brightest observed night sample (476),
so it cannot false-trigger in the dark, while cutting roughly 2.5 hours off a
naturally lit morning. 560 was considered and rejected as unnecessarily close to
the night band for the 36 minutes it would gain.

## Anomaly noted, not fixed

One sample — 2026-08-03 23:43 EDT — published **0**, which means ten consecutive
ADC reads returned 0. It caused a spurious four-minute dim. Zeros also appear in
both channels of the covered window in
[the calibration result](wiringpi-phase5-mcp3002-calibration-result.md), so this
is not new to the feed. Pre-migration data never went below 96 in three days.

One sample in 441 is not urgent and no code change was made. A floor sanity
check in `LightSensor::doSensorRead()` would be the cheap mitigation if it
recurs. Recorded here so the next person sees it as known rather than novel.

## What did not change

- No wiring, no overlay, no Device Tree, no service, and no GPIO line.
- `LightSensor::maxLightValuesSize` remains 10.
- The sensor read cadence remains 600 ms.
- `updateDim()`'s hysteresis direction and brightness levels (1 dim, 16 bright)
  are untouched.
