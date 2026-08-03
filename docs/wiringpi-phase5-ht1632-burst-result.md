# Phase 5 HT1632 burst transport result

## Result

The 2026-08-02 re-run of the matrix gate against the new burst value path
**passed all 17 checks with zero failures and zero warnings**. Every render met
the 12 ms tick and the panel rendered identically to the slow path.

This closes the matrix timing question that the
[per-edge result](wiringpi-phase5-ht1632-render-result.md) opened.

| Item | Value |
| --- | --- |
| Gate commit | `a81f8698d451a85cbbb0b4eb359ae082b30a444d` |
| Helper SHA-256 | `5ba9c8df3594bea53bf0f02397b14d8b08a0d0554e27567ef8913b3ad56ca127` |
| Clean build | 141 seconds wall clock |
| Archive | `oclock-phase5-ht1632-20260803T003223Z-OXjbRzuq.tar.gz` |
| Archive SHA-256 | `f445d06633a746ac7fd168751c85c69a1ad4e87191c63d36e1715248fdba0f86` |
| Visual result | Even green then red stripes, indistinguishable from the slow run |
| Renders within 12 ms | **20 of 20** |

## Measurements

| Statistic | Per-edge (rejected) | Burst (accepted) |
| --- | --- | --- |
| Renders within 12 ms | 0 of 20 | **20 of 20** |
| Minimum | 18,910 us | 3,036 us |
| Mean | 19,164 us | 4,094 us |
| Maximum | 21,503 us | 5,120 us |
| First green frame | 19,160 us | 3,042 us |
| First red frame | 20,094 us | 4,379 us |

The mean improved by about 4.7x. The worst observed render kept better than
2.3x margin against the tick.

Dividing the mean by roughly 7,100 GPIO writes per full rewrite puts the cost
near **0.58 microseconds per write**, down from about 2.7. That is consistent
with removing the mutex, the configured-line lookup and validation, and two
virtual dispatches per edge, and with moving the memory barrier from every
store to the burst boundary.

## The visual result carries as much weight as the timing

A fast transport that clocks data the chips never latch correctly would still
have produced these numbers. The operator confirmed even stripes across all 16
chips in both colors, indistinguishable from the slow run, which is what
establishes that the setup timing still satisfies the device.

The HT1632 requires at least 50 ns between a data change and the WR rising
edge. The old path met that incidentally, because every write cost
microseconds. The burst path holds it deliberately through
`OCLOCK_GPIO_BURST_SETUP_NOPS`, defaulting to 32 nops. This run is the evidence
that the default is adequate on this exact board at 1 GHz.

If a future run ever shows garbled or shifted stripes, raise that value first.
Lowering it below the datasheet requirement is not an optimization.

## State after the gate

- The matrix GPIOs were unclaimed before the run and released afterwards.
- No strip `spidev` binding was created; `spi4.0` stayed unbound.
- The MCP3002 remained on its native `mcp320x` driver.
- `oclock.service` remained inactive and the application never ran.
- Firmware reported `throttled=0x0`.
- The helper blanked the panel before returning.

## Limits of this evidence

- Timing was measured standalone. Under the application's timer thread, with
  the strip and ADC also active, contention will make it worse. The margin is
  larger than the strip's, but neither has been measured under real load.
- Both paths were compared for emitted edge order by
  `tests/gpio_burst_tests.cpp`, not by waveform capture. No logic analyzer was
  used, so pulse widths are inferred from correct device behavior rather than
  measured.
- The 32-nop default is validated at 1 GHz on this board only. A different
  clock speed or board would need re-validation.
- 20 renders is not a soak.

## Next work

The two standard devices and the matrix now all meet their budgets. The
remaining Phase 5 work is the
[whole-application trial](wiringpi-phase5-application-trial.md), which is the
first run under real load and the first chance to judge the retained 360/500
light thresholds against representative room light.

**Both of those resolved.** The trial
[passed 13 of 13](wiringpi-phase5-application-trial-result.md), and the light
thresholds did not survive contact with a genuinely dark room: 360 turned out
to be unreachable on this unit, and they are now the measured 460/700.

~~The strip `spidev` binding persistence question remains a separate Phase 6
blocker.~~ **Closed 2026-08-03**; see
[the binding persistence gate](wiringpi-phase6-binding-persistence.md).
