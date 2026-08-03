# Phase 5 guarded LPD8806 2 MHz experiment

> **Retired tooling.** This gate has passed and its scripts and standalone
> build targets have been retired to
> [`misc/junk/wiringpi-migration/`](../misc/junk/wiringpi-migration/CATALOG.md).
> The `make phase5-*` targets referenced below no longer exist. The procedure
> is preserved as the record of how the recorded result was produced, not as a
> command sequence that still runs.

## Status

Both gates passed on 2026-08-02 with zero failures and zero warnings. The
median `show()` time fell from 20,473 to 3,001 microseconds and all 25 frames
met the 12 ms budget. See the
[2 MHz result](wiringpi-phase5-lpd8806-2mhz-result.md). The production speed is
still 1 MHz; promoting it is a separate reviewed change.

## Purpose

The accepted all-off transfer proved correct frames and rollback at 1 MHz, but
the repeated cadence gate rejected that profile: 0/25 `show()` calls met the
12 ms application tick. The measured median was 20,473 microseconds.

The exact Raspberry Pi `rpi-6.18.y` `spi-gpio` driver inserts a delay for a
requested half-cycle of 500 ns or more, which places 1 MHz on the delayed side
of a sharp threshold. This experiment requests 2 MHz, placing the same
controller on its undelayed/free-running path. It preserves the existing GPIO
wiring, active overlay, frame contents, runtime binding, and rollback rules.

The normal application build remains configured for 1 MHz. Only the separately
named `phase5-lpd8806-all-off-2mhz` helper receives the compile-time override.

## Build gate

Fetch and verify the exact commit supplied for the gate, then extract it into a
new temporary directory. This is a narrow clean helper build; record its time.

```sh
cd ~/oclock-phase3
git fetch origin agent/plan-wiringpi-migration

expected_commit=<commit supplied for the gate>
experiment_commit=$(git rev-parse FETCH_HEAD)
test "${experiment_commit}" = "${expected_commit}" || {
  echo "Unexpected commit: ${experiment_commit}" >&2
  exit 1
}

experiment_dir=$(mktemp -d /tmp/oclock-lpd-2mhz-XXXXXXXX)
git archive FETCH_HEAD | tar -x -C "${experiment_dir}"
cd "${experiment_dir}"

start=$(date +%s); make phase5-lpd8806-all-off-2mhz; end=$(date +%s)
echo "clean_build_seconds=$((end-start))"
file build/phase5-lpd8806-all-off-2mhz
ldd build/phase5-lpd8806-all-off-2mhz | \
  grep -E 'libwiringPi|libstdc|libc'
sha256sum build/phase5-lpd8806-all-off-2mhz
```

Do not execute the helper directly.

The Zero W does not have `/usr/bin/time` installed, and installing a package is
not an authorized target change for this gate. Use the shell wall-clock form
shown above and record that it is wall-clock rather than `time -p` output.
This supersedes the `/usr/bin/time -p` instruction in the resume handoff for
native Pi builds until the package question is separately reviewed.

### Build gate result — 2026-08-02: accepted

The clean helper build passed on the exact Zero W.

| Item | Value |
| --- | --- |
| Experiment commit | `b639cc5430f0fe0469321cb81f99867449b9dea4` |
| Build kind | Clean, in a fresh `git archive` extraction |
| Wall-clock build time | 63 seconds |
| Binary SHA-256 | `7abc8c06975d1c39bf5b0683426a65572756e5301739e30c00a709125e8536eb` |
| Architecture | ELF 32-bit LSB, ARM EABI5, `/lib/ld-linux-armhf.so.3` |
| WiringPi entries in `ldd` | 0 |

The helper links only `libstdc++`, `libgcc_s`, `libc`, `libm`, and the
platform `libarmmem` preload. It was built but deliberately not executed. The
target state was unchanged: overlay active with `spi3.0` bound to `mcp320x`,
`spi4.0` unbound, no `/dev/spidev*`, `oclock.service` inactive, and firmware
reporting `throttled=0x0`.

The remaining one-frame and cadence gates are blocked on an operator who can
watch the physical strip; they must not be run unattended.

**Both ran and passed on 2026-08-02.** The single frame took 3.193 ms and the
25-frame benchmark met the 12 ms budget 25 times out of 25 at a 3.001 ms
median. Production was promoted to 2 MHz on that evidence; see the
[2 MHz result](wiringpi-phase5-lpd8806-2mhz-result.md).

## One-frame safety gate

Confirm that `oclock.service` is inactive and the strip is unbound. Then run
one all-off frame through the existing guarded verifier:

```sh
sudo misc/junk/wiringpi-migration/managePhase5Lpd8806Binding.sh status

sudo misc/junk/wiringpi-migration/verifyPhase5Lpd8806FirstTransfer.sh \
  --tool build/phase5-lpd8806-all-off-2mhz \
  --commit "${experiment_commit}" \
  --speed-hz 2000000
```

Watch the entire strip from before entering `TRANSFER` until after the helper
returns. It must remain completely dark and stable. The verifier must report
the exact `speed_hz=2000000` metadata, successful transfer, clean unbind,
native ADC binding, inactive service, and no throttling.

Stop after any failure and inspect the archive. Do not proceed automatically.

## Repeated cadence gate

Only after the one-frame archive is accepted, use the same binary for 25
all-off frames:

```sh
sudo misc/junk/wiringpi-migration/verifyPhase5Lpd8806Cadence.sh \
  --tool build/phase5-lpd8806-all-off-2mhz \
  --commit "${experiment_commit}" \
  --speed-hz 2000000
```

Enter `BENCHMARK`, watch the entire strip, answer the final visual question,
and share the archive plus adjacent checksum. Acceptance requires all 25
frames to be valid, every `show()` call at or below 12,000 microseconds, a
dark and stable visual result, successful unbind, unchanged native ADC
binding, inactive service, and no throttling.

This gate does not change the overlay, boot files, packages, wiring, service,
or production application speed. A successful result permits a separately
reviewed application/overlay adoption step; it does not deploy the application.
