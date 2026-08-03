# Phase 5 guarded LPD8806 colored sequence

> **Retired tooling.** This gate has passed and its scripts and standalone
> build targets have been retired to
> [`misc/junk/wiringpi-migration/`](../misc/junk/wiringpi-migration/CATALOG.md).
> The `make phase5-*` targets referenced below no longer exist. The procedure
> is preserved as the record of how the recorded result was produced, not as a
> command sequence that still runs.

## Status

Passed on 2026-08-02 with all 23 checks, zero failures, and zero warnings. The
strip showed uniform red, green, and blue and ended dark; the slowest frame was
4,424 microseconds. See the
[colored sequence result](wiringpi-phase5-lpd8806-colors-result.md).

## Purpose

Every strip frame measured so far has been all-off. That proved transfer
timing, rollback, and the absence of spurious output, but a dark strip cannot
distinguish *correct data* from *no data*. This gate is the first to latch
non-zero pixel values, so it is the first that can expose a signal-integrity
problem from running the arbitrary-pin `spi-gpio` bus at 2 MHz.

It preserves the existing GPIO wiring, the active overlay, the runtime binding
model, and every rollback rule.

## Scope

One standalone sequence through the already-reviewed runtime `spidev` binding:

1. all 240 pixels red, held about 3 seconds;
2. all 240 pixels green, held about 3 seconds;
3. all 240 pixels blue, held about 3 seconds;
4. one final all-off frame.

Each frame is the same 728 bytes as the accepted all-off gate: 720 GRB data
bytes and eight latch bytes.

### Brightness

Channels are driven at `0x3F`, half of the LPD8806's 7-bit range. The operator
approved half brightness for this gate. Lighting all 240 pixels of a single
channel at full range is a substantial simultaneous current draw, and half
brightness is both sufficient to prove color correctness and comparable to
levels the application itself already uses.

### The final all-off frame is mandatory

LPD8806 pixels latch and retain their last value. A lit frame would outlive the
runtime binding that this gate is required to remove, leaving the strip
illuminated with no bound device to clear it. The helper therefore always ends
with an all-off frame, and the verifier asserts `final_state=off`.

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

experiment_dir=$(mktemp -d /tmp/oclock-lpd-colors-XXXXXXXX)
git archive FETCH_HEAD | tar -x -C "${experiment_dir}"
cd "${experiment_dir}"

start=$(date +%s); make phase5-lpd8806-colors-2mhz; end=$(date +%s)
echo "clean_build_seconds=$((end-start))"
file build/phase5-lpd8806-colors-2mhz
ldd build/phase5-lpd8806-colors-2mhz | grep -E 'libwiringPi|libstdc|libc'
sha256sum build/phase5-lpd8806-colors-2mhz
```

Do not execute the helper directly.

## Visual gate

Confirm `oclock.service` is inactive and the strip is unbound, then run the
guarded verifier. This is an attended gate: watch the strip for the whole
sequence, which lasts about ten seconds.

```sh
sudo misc/junk/wiringpi-migration/managePhase5Lpd8806Binding.sh status

sudo misc/junk/wiringpi-migration/verifyPhase5Lpd8806Colors.sh \
  --tool build/phase5-lpd8806-colors-2mhz \
  --commit "${experiment_commit}" \
  --speed-hz 2000000
```

Type `COLORS` to begin. Acceptance requires all of the following:

- each color appears **uniform across the entire strip**, with no stray,
  flickering, wrong-colored, or dead pixels;
- the colors appear in the order red, then green, then blue;
- red really is red, green really is green, and blue really is blue, which
  confirms the GRB byte order survived the transport;
- the strip ends completely dark;
- every `show()` call meets the 12 ms budget;
- the helper reports `final_state=off` and `frames_sent=4`;
- the strip unbinds cleanly, the MCP3002 keeps its native binding, the service
  stays inactive, and firmware reports no throttling.

Wrong colors, a partially updated strip, or pixels that disagree with their
neighbors would indicate a signal-integrity or byte-order problem rather than a
timing problem. Record any such result and stop; do not retry at a lower speed
to make it pass without first understanding it.

## Boundaries

This gate does not change the overlay, boot files, packages, wiring, service,
or production application speed. It does not run the application or read the
ADC. A successful result supports, but does not by itself authorize, promoting
2 MHz to the production strip speed.
