# Phase 5 guarded LPD8806 first transfer

## Status

The standalone all-off transfer tool and guarded verifier are implemented and
have passed on the exact Zero W. The first attempt stopped before transferring
because `spi-gpio` rejected the unsupported userspace `SPI_NO_CS` mode bit;
cleanup passed. The corrected mode-0 retry sent the expected 728-byte frame,
passed all 17 checks, and restored the safe unbound state. See the
[first-transfer results](wiringpi-phase5-lpd8806-first-transfer-result.md).

The accepted 20,956-microsecond `show()` measurement proves a real transfer,
but exceeds the existing 12 ms application tick. A repeatable strip cadence
benchmark remains required before full Phase 5 performance acceptance.
`misc/verifyPhase5Lpd8806Cadence.sh` reuses the accepted all-off helper for 25
measured frames, binds only once, and unbinds before visual confirmation.

## Scope

This gate authorizes exactly one LPD8806 frame through the already-reviewed
runtime `spidev` binding:

- 240 off pixels, represented by 720 bytes of `0x80`;
- eight zero latch bytes;
- one 728-byte `SPI_IOC_MESSAGE(1)` submission at 1 MHz.

The helper uses the application's `LPD8806::show()` frame assembly and Linux
SPI transport, but does not start `oclock`. A fake GPIO object makes it an
error for the standalone path to perform an unexpected GPIO operation.

The verifier refuses the wrong board, revision, OS, overlay checksum, tool
architecture, active service, or pre-existing strip binding. It requires the
operator to type `TRANSFER`, binds only the strip child discovered by Device
Tree path, applies a 15-second process timeout, immediately unbinds after the
tool exits, and then asks whether the strip remained completely off and
stable. Its exit trap also attempts unbind after every error or interruption.
A reboot clears the runtime-only binding if an emergency unbind itself fails.

## Build without running

Fetch the expected PR commit, verify it, and extract it without changing the
target checkout:

```sh
cd ~/oclock-phase3
git fetch origin agent/plan-wiringpi-migration

expected_commit=<commit supplied for the gate>
transfer_commit=$(git rev-parse FETCH_HEAD)
test "${transfer_commit}" = "${expected_commit}" || {
  echo "Unexpected commit: ${transfer_commit}" >&2
  exit 1
}

transfer_dir=$(mktemp -d /tmp/oclock-lpd-first-transfer-XXXXXXXX)
git archive FETCH_HEAD | tar -x -C "${transfer_dir}"
cd "${transfer_dir}"

time make phase5-lpd8806-all-off
file build/phase5-lpd8806-all-off
ldd build/phase5-lpd8806-all-off | grep -E 'libwiringPi|libstdc|libc'
sha256sum build/phase5-lpd8806-all-off
```

The tool must be 32-bit ARM EABI5 and must not resolve WiringPi. Do not execute
it directly: the verifier supplies the target, service, binding, timeout,
rollback, and evidence boundaries.

## Run the gate

Confirm `oclock.service` is inactive and the strip is unbound, then run:

```sh
sudo misc/managePhase5Lpd8806Binding.sh status

sudo misc/verifyPhase5Lpd8806FirstTransfer.sh \
  --tool build/phase5-lpd8806-all-off \
  --commit "${transfer_commit}"
```

Watch the complete LED strip from before typing `TRANSFER` until the tool has
returned. The expected observation is no flash, color, or instability. Answer
the final prompt and share the generated `.tar.gz` plus adjacent `.sha256`.

After the verifier returns, its result must show:

- the standalone tool succeeded;
- all five frame metadata values match;
- a positive transfer wall time was captured;
- the operator accepted the visual result;
- the strip is unbound and its character device is gone;
- MCP3002 remains on `mcp320x`;
- `oclock.service` remains inactive;
- firmware throttling evidence was recorded.

Do not run the full application after this gate. MCP3002/IIO conversion is the
next application change. Keep final strip cadence acceptance separate from
this one-frame functional and rollback gate.
