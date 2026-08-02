# Phase 5 LPD8806 colored sequence result

## Result

The 2026-08-02 guarded colored gate **passed all 23 checks with zero failures
and zero warnings**. This is the first strip gate to latch non-zero pixel data,
and it closes the signal-integrity question left open by the all-off timing
work: the 2 MHz `spi-gpio` bus carries real pixel data correctly over the
existing arbitrary-pin wiring.

| Item | Value |
| --- | --- |
| Gate commit | `0b79317efe6eef0a090ce5ac1602093dcfb01a13` |
| Helper binary SHA-256 | `bc478b2c990d6e7da4539266d61b8509afe7c938a946a38f294890e2218d3a76` |
| Clean helper build | 84 seconds wall clock |
| Archive | `oclock-phase5-lpd-colors-20260802T193302Z-kqlndaE4.tar.gz` |
| Archive SHA-256 | `dc433937ec177c1ea482c6fc97adbb1023ea63686fcb031fb7219c68f24f83e4` |
| Brightness | 63 of 127 (half), operator approved |
| Frames | 4 of 4 sent, all valid |
| Visual result | Solid red, then green, then blue, each uniform, ending dark |

## Frame timing

Every frame met the 12 ms budget with better than 2.7x margin.

| Frame | Elapsed |
| --- | --- |
| red | 3,036 us |
| green | 4,424 us |
| blue | 4,334 us |
| off | 4,377 us |
| Maximum | 4,424 us |

The colored frames ran modestly slower than the 3,001-microsecond all-off
median. The payload size is identical at 728 bytes, so this is not a transfer
volume effect; it is most consistent with ordinary scheduling variation on a
single-core ARMv6 while the process also sleeps between frames. It is not large
enough to threaten the budget, but do not quote the all-off median as the
expected production figure. Treat roughly 4.4 ms as the observed worst case for
a single strip frame at 2 MHz.

## What this gate proves

- **Correct data, not just correct silence.** A dark strip cannot distinguish
  valid output from no output. Uniform full-strip color across all 240 pixels
  does.
- **Byte order survived the transport.** The LPD8806 wire order is GRB, not
  RGB. Red rendering as red rather than green confirms the application's frame
  assembly reaches the device intact through kernel `spidev`.
- **No signal integrity failure at double the clock rate.** No stray,
  flickering, dead, or wrong-colored pixels appeared at any point.
- **The frame buffer matched intent before transfer.** The helper reads back
  probe pixels at indices 0, 1, 120, and 239 against the expected packed color
  and aborts before `show()` on mismatch. No mismatch occurred.
- **The SPI path performed no GPIO operations**, asserted through `FakeGpio`.

## State after the gate

- The strip's temporary `spidev` binding and character device were removed.
- The final all-off frame left the strip dark, so no lit state outlived the
  binding.
- The MCP3002 remained bound to its native `mcp320x` driver.
- `oclock.service` remained inactive and the application never ran.
- Firmware reported `throttled=0x0`, so half brightness across all 240 pixels
  produced no detectable supply problem.

## Limits of this evidence

- Only uniform full-strip colors were tested. Per-pixel patterns, gradients,
  and animation transitions have not yet been driven through this path.
- Half brightness was used. Full brightness across all 240 pixels remains
  untested and is a larger simultaneous current draw.
- The sequence was standalone. It did not run under the application's timer
  thread, alongside matrix updates and ADC reads.
- Four frames is not a soak. Sustained animation behavior is still unproven.

## Next work

This result supports promoting 2 MHz to the production strip speed in
[`StripSpeed.h`](../src/spi/StripSpeed.h). After that, the remaining Phase 5
work is the narrow HT1632 bulk transport, then a guarded whole-application
run that can finally observe representative-room dimming behavior.
