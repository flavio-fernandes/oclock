# Phase 5 whole-application trial result

## Result

**Phase 5 is complete.** The 2026-08-02 whole-application trial passed all 13
checks with zero failures on its final run, after a threshold retune that the
earlier runs made possible.

This was the first time the modern stack drove the real Office Clock, and it
passed every observation that rejected both earlier candidates.

| Item | Value |
| --- | --- |
| Accepted trial commit | `9677a0e7a0171e091f9553b2eb70d51e40f14812` |
| Binary SHA-256 | `3eacccb0f71fd1f08c97c87f8146018721ed6f65e9efa494676bcb965c8881c9` |
| Clean ARM build | 329 seconds wall clock |
| Archive | `oclock-phase5-20260803T012026Z-sAPEC92W.tar.gz` |
| Archive SHA-256 | `10a1328189e014d4166bad09c84b44543e4a58e628e1a3118a5ef1feb181d7b1` |
| Checks | **13 of 13 passed, 0 failures** |

## The three runs

| Run | Commit | Result | What it established |
| --- | --- | --- | --- |
| 1 | `acc5243` | 12 pass, 1 fail | Strip smoothness and timing passed; dimming failed |
| 2 | `acc5243` | 11 pass, 2 fail | Room-light-off plateau of 452-478 proved 360 was unreachable |
| 3 | `9677a0e` | **13 pass, 0 fail** | Retuned 460/700 thresholds engage and recover correctly |

## What passed

| Observation | Earlier candidates | This trial |
| --- | --- | --- |
| Display panels refresh without corruption | passed | passed |
| LED-strip animation smooth, normal colors | **failed** | **passed** |
| Timing versus production | **failed** | **better than production** |
| Automatic dimming | **failed** | **passed** |
| Motion transitions | passed | passed |
| External MQTT feed updates display | passed | passed |
| Onboard Wi-Fi stable | passed | passed |
| Clean shutdown over HTTP | passed | passed |

The operator rated display and LED response times **better than the original
production clock**. Strip smoothness and timing were the exact gates that
rejected the pure-libgpiod and `gpiod-mmap` candidates, so the mixed
kernel-SPI plus burst-matrix architecture is validated on the criteria that
drove the redesign.

## How the dimming question was actually settled

The first run's failure was initially attributed to the operator not holding a
cover long enough for the six-second averaging window. That theory was wrong,
and the second run disproved it: with a sustained cover the reported value
reached a clear steady state of 452 to 478 and stayed there for roughly 45
seconds. The averaging window was never the problem.

The decisive input was the operator switching **the actual room light off**
rather than covering the sensor. That is the real condition the clock should
dim in. It established that the original 360 low-water mark was simply
unreachable on this unit: the darkest the room ever got still read above it, so
dimming could never have engaged on any candidate, including during the Phase 0
comparison.

Room darkness also turned out to vary between runs:

| Condition | Reported value |
| --- | --- |
| Room lit | approximately 1022 |
| Room dark, run 2 | 452 to 478 |
| Room dark, run 3 | 355 to 366, minimum 355 |

That spread is why 460 is a better choice than it first appears. A 360
threshold would have engaged on run 3's darker conditions and missed run 2's
entirely. 460 covers both.

The accepted run shows both transitions firing cleanly:

```
1022 ... 559 -> 443   crosses the 460 low-water mark, enters dark
         355 to 443   stays dark, all below the 700 high-water mark
         491 -> 624 -> 759   crosses 700, returns to bright
```

The high-water mark moved from 500 to 700 as a necessary companion rather than
a preference. Entering dark needs one sample below the low-water mark, but
leaving dark needs a sample at or above the high-water mark, and 500 sat only
22 counts above run 2's observed dark maximum of 478 — close enough that a
slightly brighter night could oscillate between dim and bright.

## A false pass that these runs exposed

Run 1's harness reported `OK: status samples cross both dimming thresholds`
even though the sensor never went below 360. The candidate publishes
`light_sensor: 0` until its first ADC read completes, and the check counted
that startup sentinel as a genuine dark reading. The adjacent
`status_has_light_change` check had the same weakness.

Both now skip leading non-positive samples only, so a genuine `0` recorded
later in real darkness still counts. Run 2 then correctly reported
`FAIL: status samples cross both dimming thresholds` on real data, which is the
fix proving itself before the accepted run.

Worth recording for the write-up: the automated check and the operator
disagreed, and the operator was right. The failing human observation is what
prompted the audit that found the bug. A fully automated trial would have
banked a clean pass on that point and shipped a clock that could never dim.

## CPU: still open, and now more interesting

CPU differed sharply between runs, so the earlier concern should not be treated
as settled in either direction.

| Measurement | Run 1 (300 s) | Run 3 (180 s) |
| --- | --- | --- |
| Mean | 18.29% | **4.72%** |
| Maximum | 75.68% | **29.79%** |
| Trend | rose from ~25% to ~75% | no comparable rise |

Phase 0 WiringPi baseline was 3.55%. Run 3's 4.72% mean is close to it; run 1's
18.29% is not. Memory was stable at about 101 MB RSS in both.

The most likely explanation is that run 1 was 300 seconds and run 3 only 180,
and run 1's rise appeared late in its window. Something time-dependent — a
periodic display mode, an animation, or accumulated dictionary content — may
become expensive after several minutes. That is a hypothesis, not a finding.

Some cost is inherent and should be stated plainly: kernel `spi-gpio` is still
bit-banging, so it is CPU-bound rather than offloaded. A strip frame costs
roughly 3 to 4.4 ms and a matrix render roughly 4 ms against a 12 ms tick.
Fixing the latency did not make the work free.

**Characterize this before Phase 6** with a single long run, at least 15
minutes, sampling whether the rise reproduces and plateaus. A clock that
periodically needs most of a single ARMv6 core has little headroom.

## State after the trial

- The candidate shut down cleanly over HTTP and returned success.
- The strip `spidev` binding was removed and `spi4.0` is unbound again.
- The MCP3002 remained on its native `mcp320x` driver.
- `oclock.service` remained inactive; no trial installed anything.
- Firmware reported `throttled=0x0`.
- Physical rollback remains the preserved Zero/Jessie unit.

## Remaining Phase 6 blockers

1. **Strip binding persistence.** The application opens `/dev/spidev4.0` but
   never binds it, and the binding does not survive a reboot. See the
   [application trial gate](wiringpi-phase5-application-trial.md).
2. **CPU characterization**, as above.
3. No soak has been run. The longest continuous observation so far is five
   minutes.
