# Phase 5 MCP3002 controlled light-capture result

## Decision

The exact Zero W passed the controlled native-IIO light capture on 2026-08-02.
All 30 two-channel samples were valid, hardware and service state did not
change, and firmware reported no throttling.

The sensor response is strong and repeatable. The existing 360/500 hysteresis
thresholds are preserved: this test proves fully covered behavior, not the
actual room's nighttime range. A later whole-application trial must verify
dimming under representative room conditions before any recalibration.

## Evidence

| Item | Value |
| --- | --- |
| Reader source commit | `d47629b0670d0357f7e18842e22e6e3cc6c7ab07` |
| Collector commit | `b9a42e159b4cdb78a0f81f7512f1e8425a9a6641` |
| Capture | `oclock-phase5-mcp3002-calibration-20260802T173207Z-vMTv7G4N.tar.gz` |
| Capture SHA-256 | `08824038c645661b201ef555e0f30149dfaafde0a18b4a088e50b4bf09a5b3af` |
| Samples | 10 pairs per window at 600 ms |
| Verifier result | 3 checks passed; 0 failures; 0 warnings |

The checksum matched before extraction, every archive member used a relative
non-traversing path, and a scan found no SSH or private-tailnet topology
markers. Raw evidence remains outside Git.

## Results

| Window | Ch0 range/mean | Ch1 range/mean | Pair mean | Below 360 | Below 500 |
| --- | --- | --- | ---: | ---: | ---: |
| Uncovered baseline | 987–1011 / 997.4 | 990–1003 / 997.9 | 997.3 | 0/10 | 0/10 |
| Fully covered | 0–481 / 229.9 | 0–318 / 128.4 | 179.0 | 6/10 | 10/10 |
| Uncovered restored | 948–1023 / 994.2 | 951–1023 / 996.1 | 995.0 | 0/10 | 0/10 |

The restored pair mean is within 2.3 counts of baseline. The fully covered mean
is 818.3 counts below baseline. Both channels independently moved in the same
direction and recovered.

## Threshold interpretation

`updateDim()` declares a bright room dark only after the ten-sample moving
average falls below 360. Once dark, it returns to bright only above 500. The
fully covered ten-sample mean of 179 would enter dark mode, and the restored
mean of 995 would return to bright mode with substantial margin.

The covered window includes transitional and heterogeneous samples, including
six pair averages below 360 and all ten below 500. That is useful evidence for
the moving average, but a hand covering the sensor is not a calibrated room
illumination. Keep the thresholds unchanged until the guarded application can
be observed in representative bright and dark room conditions.

## Safety observations

- The collector reused the accepted standalone reader; no native rebuild was
  needed.
- MCP3002 remained bound to `mcp320x`.
- The strip remained unbound.
- `oclock.service` remained inactive.
- No GPIO line, SPI binding, boot file, threshold, or service state changed.
