# Phase 5 guarded LPD8806 2 MHz experiment

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

/usr/bin/time -p make phase5-lpd8806-all-off-2mhz
file build/phase5-lpd8806-all-off-2mhz
ldd build/phase5-lpd8806-all-off-2mhz | \
  grep -E 'libwiringPi|libstdc|libc'
sha256sum build/phase5-lpd8806-all-off-2mhz
```

Do not execute the helper directly.

## One-frame safety gate

Confirm that `oclock.service` is inactive and the strip is unbound. Then run
one all-off frame through the existing guarded verifier:

```sh
sudo misc/managePhase5Lpd8806Binding.sh status

sudo misc/verifyPhase5Lpd8806FirstTransfer.sh \
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
sudo misc/verifyPhase5Lpd8806Cadence.sh \
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
