# Phase 5 HT1632 render result

## Result

The 2026-08-02 guarded matrix gate **failed on timing** with one failure and
zero warnings. Every other check passed, including the operator's visual
confirmation.

This is a transport-timing failure, not a correctness or safety failure. The
panel rendered exactly the expected content.

| Item | Value |
| --- | --- |
| Gate commit | `65ed79d6b837266112bd38689788f0e41cf70969` |
| Helper SHA-256 | `d18f81b73d80682baba36d06885d1803d4f597ccf35833401fb8db98039dde0a` |
| Archive | `oclock-phase5-ht1632-20260803T001326Z-7QMP3rF4.tar.gz` |
| Archive SHA-256 | `8a7158fcb4ccf2803ca710f686a4b9f9dd3e084d9ae125c2bb490ae9ee67a039` |
| Visual result | Even green then red stripes across the whole panel, ending dark |
| Renders within 12 ms | **0 of 20** |

## Measurements

| Statistic | Value |
| --- | --- |
| First green frame | 19,160 us |
| First red frame | 20,094 us |
| Minimum | 18,910 us |
| Mean | 19,164 us |
| Maximum | 21,503 us |

A forced full rewrite costs roughly 19 ms against a 12 ms tick: about 1.6x over
budget, and remarkably consistent.

## What the measurement implies about cost per operation

A full rewrite of the 512-nibble address space emits about 2,048 data bits at
three GPIO writes each, plus per-chip select and addressing overhead across 16
chips. That is on the order of 7,100 GPIO write operations.

At 19,164 microseconds, each write costs roughly **2.7 microseconds**. That is
the same per-edge abstraction cost that defeated the strip before it moved to
kernel SPI: userspace locking, configured-line lookup, virtual dispatch,
validation, and memory barriers around what is ultimately a single register
store.

## This is not a rare worst case

The gate deliberately forced a full rewrite, and the obvious objection is that
`render()` is dirty-tracked so production might rarely pay this cost. It does
pay it.

`src/displayInternal.cpp` updates the clock by selecting a color board, calling
`clear()`, and redrawing text. `HT1632Class::clear()` sets
`_globalNeedsRewriting` for that buffer, which makes the next `render()` rewrite
the entire address space regardless of per-nibble dirty bits. The application
then calls `renderAll()`, which repeats the process for each board.

So the measured 19 ms is the cost of an ordinary clock update, not an
artificial stress case. Clearing and redrawing is also how the display erases
previous digits, so it is not incidental.

## Decision: the bulk transport is justified

The gate was written to answer one question before building anything: does the
matrix still need a bulk transport now that the strip has moved off the
per-edge GPIO path? The measured answer is **yes**.

The design should resolve the four matrix pins once and write the BCM2835
registers directly in a tight loop, bypassing the per-write validation, lookup,
locking, and virtual dispatch. A direct register store is a small fraction of
2.7 microseconds, so a large improvement is available; the required improvement
is at least 1.6x, and more for real margin because `renderAll()` may rewrite
several buffers in one pass.

Keep it narrowly scoped to the HT1632, as the plan requires. Do not generalize
it into a replacement for kernel SPI.

### Alternative that was considered and not chosen

The application could avoid the global `clear()` and redraw only changed
regions, letting existing per-nibble dirty tracking limit the writes. That
changes drawing semantics across the display code, risks stale pixels where
old digits are not erased, and alters behavior that Phase 0 captured as
correct. Fixing the transport is the smaller, better-isolated change. Revisit
this only if the bulk transport proves insufficient.

## State after the gate

- The matrix GPIOs were unclaimed before the run and the helper released them.
- No strip `spidev` binding was created; `spi4.0` stayed unbound.
- The MCP3002 remained on its native `mcp320x` driver.
- `oclock.service` remained inactive and the application never ran.
- Firmware reported `throttled=0x0`.
- The helper blanked the panel before returning, as designed.

## Limits of this evidence

- Timing was measured standalone, not under the application's timer thread with
  the strip and ADC also active. Contention would make it worse, not better.
- Only forced full rewrites were measured. The cost of a genuinely incremental
  render was not separately characterized, because the application does not
  currently produce one.
- The helper ran as the invoking operator, confirming no privilege beyond
  `gpio` group membership is needed.

## Next work

1. ~~Implement the narrow bulk HT1632 transport and re-run this exact gate.~~
   Done and accepted: see the
   [burst result](wiringpi-phase5-ht1632-burst-result.md). The mean fell from
   19,164 to 4,094 microseconds and all 20 renders met the tick.
2. Then run the
   [whole-application trial](wiringpi-phase5-application-trial.md).
3. ~~The strip binding persistence question remains a separate Phase 6
   blocker.~~ **Closed 2026-08-03**; see
   [the binding persistence gate](wiringpi-phase6-binding-persistence.md).
