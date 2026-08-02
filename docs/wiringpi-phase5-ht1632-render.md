# Phase 5 guarded HT1632 render gate

## Why measure before building a new transport

The migration plan reserved a "narrow bulk mmap transport" for the matrix. That
direction was chosen while the earlier `gpiod-mmap` candidate was failing, but
that trial drove the **strip and the matrix** through the same per-edge GPIO
path. The strip has since moved to kernel SPI and no longer competes for that
path at all.

So the honest first question is not "how do we build a bulk transport" but
"does the matrix still need one?" This gate measures the existing path before
any new transport is written. If the current renders already fit the 12 ms
tick, a bulk rewrite would be unnecessary complexity in the one place the plan
explicitly warns against forcing a uniform abstraction.

## What the matrix actually costs

The panel is 128x16 across 16 HT1632 chips, with a 512-nibble address space.
`render()` is dirty-tracked, so ordinary clock updates rewrite only changed
chunks. A forced full rewrite is the worst case: roughly 2,048 data bits, each
costing three GPIO writes, plus per-chip select and addressing overhead.

The benchmark deliberately forces that worst case on every measured iteration
by calling `clear()`, which marks the whole buffer dirty, and then redrawing
real content. Measuring dirty-tracked renders would flatter the result.

## Scope and safety

The matrix uses BCM GPIOs 6 (CS), 13 (WR), 19 (data), and 26 (select clock).
None of these are claimed by the SPI overlay, which owns 4, 17, 20, 21, 22, and
27. This gate therefore needs **no spidev binding at all** and is materially
simpler than the strip gates:

- no runtime `driver_override`, no character device, nothing to unbind;
- the MCP3002 keeps its native `mcp320x` binding, untouched;
- the strip is never bound and its GPIOs are never requested;
- `oclock.service` stays inactive and the application never runs.

The verifier refuses a wrong board, revision, OS, or non-ARM helper; refuses to
start if any matrix GPIO already has a consumer; applies a 90-second timeout;
and asserts afterwards that no strip `spidev` node appeared.

The helper runs as the invoking operator rather than root, which also confirms
the matrix path needs no privilege beyond `gpio` group membership.

## Build gate

```sh
cd ~/oclock-phase3
git fetch origin agent/plan-wiringpi-migration

expected_commit=<commit supplied for the gate>
experiment_commit=$(git rev-parse FETCH_HEAD)
test "${experiment_commit}" = "${expected_commit}" || {
  echo "Unexpected commit: ${experiment_commit}" >&2
  exit 1
}

experiment_dir=$(mktemp -d /tmp/oclock-ht1632-XXXXXXXX)
git archive FETCH_HEAD | tar -x -C "${experiment_dir}"
cd "${experiment_dir}"

start=$(date +%s); make phase5-ht1632-render; end=$(date +%s)
echo "clean_build_seconds=$((end-start))"
file build/phase5-ht1632-render
ldd build/phase5-ht1632-render | grep -E 'libwiringPi|libgpiod|libstdc'
sha256sum build/phase5-ht1632-render
```

Do not execute the helper directly.

GCC 14 on ARMv6 emits `parameter passing for argument of type
'std::move_iterator<long long int*>' changed in GCC 7.1` notes while compiling
the sample vector. These are informational psABI notes, not warnings; the build
still succeeds under `-Werror`. Do not treat them as a failure.

### Build gate result — 2026-08-02: accepted

| Item | Value |
| --- | --- |
| Gate commit | `65ed79d0d1c1bcfa5e4b5e6cbb9e2ba1e3f30f1a` |
| Build kind | Clean, in a fresh `git archive` extraction |
| Wall-clock build time | 70 seconds |
| Binary SHA-256 | `d18f81b73d80682baba36d06885d1803d4f597ccf35833401fb8db98039dde0a` |
| Architecture | ELF 32-bit LSB, ARM EABI5 |
| Links | `libgpiod.so.3`, `libstdc++.so.6` |
| WiringPi entries in `ldd` | 0 |

Built at `/tmp/oclock-ht1632-build` on the target and deliberately not
executed. The visual and timing gate below has **not** been run.

## Visual and timing gate

This is an attended gate. The whole sequence takes under 30 seconds.

```sh
sudo misc/verifyPhase5Ht1632Render.sh \
  --tool build/phase5-ht1632-render \
  --commit "${experiment_commit}"
```

Type `RENDER` to begin. The sequence is:

1. vertical stripes every 8 columns in **green**, held about 3 seconds;
2. the same stripes in **red**, held about 3 seconds;
3. 20 timed worst-case renders, which will look like brief flicker;
4. a blank panel.

Stripes rather than a fully lit panel are deliberate. A solid field cannot
reveal an addressing fault, and it draws considerably more current. Stripes
spread across all 16 chips make a misaddressed or dead chip obvious.

Acceptance requires all of the following:

- stripes appear evenly spaced across the **entire** 128x16 panel, with no
  missing, shifted, or duplicated groups, which would indicate a chip
  addressing fault;
- green appears first, then red, confirming both color boards render;
- the panel ends completely dark;
- all 20 timed renders meet the 12 ms budget;
- the helper reports `final_state=off` and `valid_renders=20`;
- no strip `spidev` node appeared, the service stayed inactive, and firmware
  reports no throttling.

## Interpreting the result

- **All 20 renders inside 12 ms:** the existing path is sufficient. Record that
  the bulk transport is not required and remove it from the plan rather than
  building it speculatively.
- **Worst case above 12 ms but typical well under:** the dirty-tracked path may
  still be fine in production, since full rewrites are rare. Measure a realistic
  clock update before deciding.
- **Consistently above 12 ms:** the bulk transport is justified. Its design
  should resolve the four pins once and write the BCM registers directly,
  bypassing the per-edge validation, lookup, locking, and virtual dispatch that
  the earlier analysis identified as the real cost.

Do not reduce the display refresh rate to make a failing path appear
acceptable.

## Boundaries

This gate does not change the overlay, boot files, packages, wiring, service,
or application configuration. It does not run the clock or read the ADC. A
successful result does not authorize deployment; the whole-application trial
remains a separate gate.
