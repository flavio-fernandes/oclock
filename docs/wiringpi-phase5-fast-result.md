# Phase 5 `gpiod-mmap` hardware result

## Decision

The first exact-board `gpiod-mmap` candidate did **not** pass Phase 5. It was
clearly faster than the pure-libgpiod build, and all functional paths remained
correct, but the operator still found overall timing unacceptable and the LED
strip especially slow. Automatic dimming also did not occur. Do not deploy
commit `1f5605d` or proceed to Phase 6 on this result.

This is a performance and calibration result, not evidence that the fast path
was limited to the HT1632 matrix. The same mapped value implementation served
the matrix, LED strip, MCP3002 ADC, and motion input.

## Evidence

The supplied archive
`oclock-phase5-20260802T050318Z-GF94nKjP.tar.gz`, SHA-256
`a1aa2a71c89c0342f95b280a09c19919d5c9cce20d392e4221cf66240a339379`,
matched its checksum. It records:

- Raspberry Pi Zero W Rev 1.1, revision `9000c1`, ARMv6/armhf Trixie;
- source commit `1f5605dbc6e8b43165190a150d47c9c1fee86a9d`;
- candidate SHA-256
  `af7a168af98c7e4765a91d70fd2926281e47fe647e804ccc24d0db92e3c2338e`;
- `/dev/gpiomem` available, no firmware throttling, and no WiringPi
  dependency;
- clean HTTP shutdown, changing motion and light status, MQTT connection,
  external-data update, correct display contents, correct strip colors, and
  stable onboard Wi-Fi;
- failed automatic-dimming, threshold-crossing, and acceptable-timing gates.

The operator described the new candidate as definitely faster than the pure
backend but still slower than the preserved WiringPi clock, with the strip
remaining particularly slow.

## Light finding

The sensor was responsive, but its ten-sample average ranged from `478` to
`1023`. Six samples fell between the existing thresholds, but none fell below
the `360` low-water mark required to enter the dark state. The code therefore
correctly remained bright; the `500` high-water mark is only used to leave a
dark state that had already been entered.

This result does not justify changing the thresholds yet. The modern run's
bright value saturated near `1022`, while the Phase 0 status capture ranged
from `390` to `613`. The next sensor capture should record both raw MCP3002
channels under the same uncovered and fully covered conditions on the
preserved WiringPi unit and the Zero W candidate. That will distinguish target
calibration from a serial-transfer error before constants change.

## Why the strip remained slow

`LPD8806::show()` does use the mapped backend. A full 240-pixel frame sends
5,760 data bits, requiring 11,520 rising/falling clock writes, another 128
clock writes for the latch, and data-line changes. In commit `1f5605d`, every
one of those edges still passes independently through:

1. the virtual `Gpio::write()` boundary;
2. the backend mutex;
3. configured-line lookup and direction validation;
4. the virtual mapped-value boundary;
5. offset validation and mask construction;
6. a register write and full memory barrier.

The experiment removed the libgpiod ioctl from each edge, which explains the
visible improvement, but it did not remove the remaining per-edge machinery.
The matrix benefits more visibly because its transactions contain far fewer
edges than a full strip refresh.

After the startup interval, recorded host CPU use averaged `9.14%`, ranged
from `5.15%` to `16.42%`, and remained above the Phase 0 short-interval average
of `3.55%`. Status requests averaged about `8.1` ms and peaked at `68.2` ms.
Those measurements support the operator's timing rejection; visual rejection
alone is sufficient to fail the gate.

## Bounded follow-up

One more mmap experiment is reasonable before moving to kernel `spi-gpio`:

- add a project-owned bulk clocked-output operation with a compatibility
  fallback expressed in ordinary `Gpio::write()` calls;
- let the mmap backend validate and lock the data/clock lines once per
  transaction, then emit the complete bit sequence without per-edge map lookup,
  mutex acquisition, or virtual dispatch;
- keep the WiringPi backend and default build behavior unchanged;
- add deterministic byte, latch, and transaction-serialization tests;
- measure complete HT1632 and LPD8806 transfer duration on the Zero W, and use
  logic-analyzer evidence before weakening memory ordering.

If that bounded bulk-transfer implementation still misses the preserved
timing, stop extending the mmap backend and move the unchanged pins to a
reversible kernel `spi-gpio`/`spidev` design.
