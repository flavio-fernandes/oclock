# Phase 5 whole-application trial result

## Result

The 2026-08-02 guarded whole-application trial recorded **12 passing checks and
one failure**. The failure was the light-sensor dimming observation. Everything
else passed, including the two observations that rejected both earlier
candidates.

This is the first time the modern stack drove the real Office Clock.

| Item | Value |
| --- | --- |
| Trial commit | `acc5243025deed7f796a88d6a4dcc19c2f1b0020` |
| Binary SHA-256 | `6afd0d5cec62ccd6bd05758ffc9f203bb419903c09b90cc7c6e53f75cc2fad2b` |
| Clean ARM build | 328 seconds wall clock |
| Archive | `oclock-phase5-20260803T004237Z-eZSa2M9Q.tar.gz` |
| Archive SHA-256 | `0372998302935d2b6fb258339fc24483df82ab7b855fb8eb7dd5d88f30b4c1a5` |
| Observation window | 300 seconds |

## What passed

| Observation | Earlier candidates | This trial |
| --- | --- | --- |
| Display panels refresh without corruption | passed | **passed** |
| LED-strip animation smooth, normal colors | **failed** | **passed** |
| Timing versus production | **failed** | **better than production** |
| Motion transitions | passed | passed |
| External MQTT feed updates display | passed | passed |
| Onboard Wi-Fi stable | passed | passed |
| Clean shutdown over HTTP | passed | passed |

The operator rated display and LED response times **better than the original
production clock**, not merely acceptable. Strip smoothness and timing were the
exact gates that rejected the pure-libgpiod and `gpiod-mmap` candidates, so the
mixed kernel-SPI plus burst-matrix architecture is validated on the criteria
that drove the redesign.

The MQTT dictionary populated with live external data during the run, and the
HTTP status endpoint answered in roughly 96 ms while the display and strip were
busy.

## What failed: dimming

The operator did not observe dimming, and the recorded values agree: excluding
the startup sample, the reported light value ranged from **403 to 1023** and
never crossed the 360 dark threshold.

### This looks like test execution, not a product defect

`LightSensor` reads both channels every 600 ms and reports the mean of a
rolling ten-sample window, so the window spans **six seconds** and responds
gradually rather than instantly.

Working from the accepted calibration means of 179 fully covered and
approximately 1022 uncovered, crossing 360 requires eight of ten samples to be
dark:

```
(8 x 179 + 2 x 1022) / 10 = 348   below the 360 threshold
(7 x 179 + 3 x 1022) / 10 = 432   above it
```

Eight dark samples is **4.8 seconds of fully covered sensor**. The observed
minimum of 403 sits between those two figures, which corresponds to roughly
seven dark samples. The most likely explanations are a cover held for about
four seconds rather than the full twelve, or a cover that was not completely
opaque.

Do not change the 360/500 thresholds on this evidence. Re-run the dimming
observation first with a fully opaque cover held for a clear fifteen seconds,
and record the minimum reached. The thresholds are only suspect if a genuinely
dark sensor still fails to cross them.

This retest needs no rebuild. The trial binary is preserved and the procedure
is unchanged.

## A false pass that this trial exposed

The harness reported `OK: status samples cross both dimming thresholds` even
though the sensor never went below 360.

The candidate reports `light_sensor: 0` until its first ADC read completes. The
check treated that startup sentinel as a genuine dark reading, so a single
pre-initialization sample satisfied the dark half of the condition. The
adjacent `status_has_light_change` check had the same weakness: `0` followed by
one constant value counted as changing light.

Both are now fixed to skip leading non-positive samples only, so a genuine `0`
recorded later in real darkness still counts. Verified against three shapes:

- this trial's actual data (`0, 1022, 403`) is now correctly rejected;
- a real crossing (`0, 1022, 179`) is accepted;
- a genuine later zero (`1022, 0`) is still accepted.

Worth noting for the write-up: the automated check and the operator disagreed,
and the operator was right. The failing observation is what prompted the audit
that found the bug. A trial with no human in the loop would have recorded a
clean pass on this point.

## CPU use is materially higher and needs attention

This did not fail a gate, but it is the most significant open concern.

| Measurement | Value |
| --- | --- |
| Phase 0 WiringPi baseline (short interval) | 3.55% |
| Rejected `gpiod-mmap` candidate | 9.14% |
| This trial, mean | **18.29%** |
| This trial, maximum | **75.68%** |
| This trial, first samples | roughly 25 to 29% |
| This trial, final samples | roughly 65 to 75% |

Memory was stable at about 101 MB RSS and 1.2%, so this is not a leak.

The upward trend across the 300-second window is unexplained and must not be
dismissed. Plausible contributors include the display doing more work as the
MQTT dictionary filled, motion waking the display, and strip animation starting
once external data arrived. None of that is established.

Some increase is expected by design and should be stated plainly in the
write-up: kernel `spi-gpio` is still bit-banging. It is faster and better
scheduled than userspace GPIO, but it is CPU-bound, not offloaded. A strip
frame costs roughly 3 to 4.4 ms and a matrix render roughly 4 ms, against a
12 ms tick, so a high duty cycle is the direct consequence of the architecture
that fixed the timing. Real hardware SPI with DMA would offload it, at the cost
of the rewiring this project deliberately avoided.

Before Phase 6, characterize this: run a longer observation, sample with the
display idle versus busy, and determine whether the trend plateaus. A clock
that needs most of a single ARMv6 core has little headroom for anything else.

## State after the trial

- The candidate shut down cleanly over HTTP and returned success.
- The strip `spidev` binding was removed and `spi4.0` is unbound again.
- The MCP3002 remained on its native `mcp320x` driver.
- `oclock.service` remained inactive; the trial never installed anything.
- Firmware reported `throttled=0x0`.
- Physical rollback remains the preserved Zero/Jessie unit.

## Next work

1. Re-run the dimming observation with a fully opaque cover held fifteen
   seconds, using the fixed threshold checks. No rebuild required.
2. Characterize the CPU trend before Phase 6.
3. Resolve the strip binding persistence blocker recorded in the
   [application trial gate](wiringpi-phase5-application-trial.md).
4. Only then plan deployment and a soak.
