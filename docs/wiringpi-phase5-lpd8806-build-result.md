# Phase 5 native LPD8806/spidev build result

## Decision

The selected LPD8806/spidev application profile built successfully on the
exact Raspberry Pi Zero W/Trixie ARMv6 target on 2026-08-02. The metadata-only
collector passed all seven checks. The binary was inspected and preserved but
was not executed, and no device was opened or hardware state changed.

This result completes the native build gate and authorizes preparation of the
separate, guarded all-off first-transfer trial. It does not authorize running
the whole application because the MCP3002 application path has not yet moved
to the overlay's native IIO interface.

## Evidence

| Item | Value |
| --- | --- |
| Source commit | `37b679746b17ffefa9cd8cc0354a41a3d4d4eee9` |
| Capture | `oclock-phase5-lpd-build-20260802T155424Z-fLC2skZi.tar.gz` |
| Capture SHA-256 | `391b33b555555620ad560591e8d08dfa6a81079ac60e4639e6765b976c95fd28` |
| Candidate SHA-256 | `834191828a27ac801e0e959435e0397068b4b9b2335455b4128d29256dbd8807` |
| Preserved candidate | `/home/pi/oclock-phase5/oclock-lpd-spidev-37b6797` |
| Native build time | 5m15.348s real, 4m53.872s user, 12.826s system |
| Collector result | 7 OK, 0 failures |

The adjacent checksum matched before extraction, and the archive contained no
absolute or parent-traversal paths. Raw evidence and the executable remain
outside Git.

## Accepted observations

- The executable is 32-bit ARM EABI5 for the selected ARMv6/armhf target.
- It resolves libgpiod major 3, the userspace ABI supplied by libgpiod 2.2,
  and the ARM atomic runtime.
- It has no WiringPi dependency.
- It contains the resolved Device Tree suffix
  `/oclock-strip-spi/lpd8806@0` instead of a hard-coded SPI bus number.
- The installed `linux-libc-dev` package owns the Linux spidev userspace
  header used by the transport.

The build command used the then-explicit experimental selectors. Those knobs
were subsequently removed by the
[modern build policy](wiringpi-modern-build-policy.md); the same selected
profile is now built with plain `make` or `make hardware`.

## Next gate

Prepare a standalone verifier that keeps `oclock.service` inactive, binds only
the dynamically discovered strip child, configures and opens its character
device, sends one 728-byte all-off frame, measures the transfer, asks for a
visual confirmation, and closes and unbinds on every exit path. Do not use the
full application as that verifier.
