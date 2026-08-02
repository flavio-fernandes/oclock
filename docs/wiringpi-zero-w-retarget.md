# Zero W and Trixie production-target decision

## Decision

On 2026-08-02 the modern office-clock deployment target changed from upgrading
the original Raspberry Pi Zero in place to deploying a separate Raspberry Pi
Zero W Rev 1.1, revision `9000c1`, with Raspberry Pi OS Lite 32-bit Trixie,
GCC 14, GPIO character-device ABI v2, and libgpiod 2.2.

The original Raspberry Pi Zero Rev 1.2, Jessie card, WiringPi installation, and
known-good Phase 0 binary remain together as the complete legacy rollback
unit. Source compatibility is also retained: plain hardware builds continue to
select WiringPi, while the modern target remains explicit.

## Why this is a controlled retarget

The Zero W was already the Phase 3 evidence and build host. It uses the same
BCM2835, ARMv6 architecture, 512 MB memory class, 40-pin GPIO layout, and
captured BCM offsets as the original Zero. The accepted modern binary already
builds natively on its Trixie image and resolves libgpiod and libatomic without
WiringPi.

The change therefore does not redesign the GPIO boundary, pin map, device
protocols, or application behavior. It turns the previously labelled proxy
into the intended target and changes rollback from an SD-card swap to a
complete powered-off unit swap.

The integrated 2.4 GHz Wi-Fi replaces the USB Wi-Fi dongle. This simplifies
the installed hardware, but makes onboard wireless behavior part of production
acceptance rather than an incidental setup detail.

Primary hardware references:

- [Raspberry Pi Zero-series hardware](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#zero-series)
- [Raspberry Pi Zero W product and longevity](https://www.raspberrypi.com/products/raspberry-pi-zero-w/)
- [Raspberry Pi OS networking and NetworkManager](https://www.raspberrypi.com/documentation/configuration/computers/raspberry-pi.html#networking)

## Consequences and controls

- The modern unit gains a maintained OS, compiler, kernel, and GPIO userspace.
- The Zero W is not a compute upgrade; GPIO bit-bang timing and CPU use still
  require real-load measurement.
- Trixie uses NetworkManager, so wireless autoconnect, address behavior, MQTT,
  HTTP reachability, and cold-boot ordering need explicit validation.
- Radio load can add power demand and scheduling jitter. The trial retains the
  firmware throttling gate and adds Wi-Fi observation and soak requirements.
- The USB Wi-Fi dongle must be absent during target acceptance.
- The original unit must not be upgraded or repurposed before the modern unit
  completes functional, timing, network, soak, and rollback gates.
- Service privilege cleanup, rewiring, hardware SPI conversion, and changes to
  application defaults remain out of scope for this retarget.

## Acceptance sequence

1. Preserve the accepted ARMv6 binary and its checksum on the Zero W/Trixie
   card.
2. Connect the unchanged office-clock harness to the powered-off Zero W.
3. Run the guarded Phase 5 trial, which requires the exact Zero W revision,
   connected Wi-Fi, expected binary, libgpiod, libatomic, and no WiringPi.
4. Compare physical behavior and timing with the Phase 0/1 legacy evidence.
5. Complete a cold-boot and overnight network/application soak.
6. Exercise rollback by powering off the Zero W and reconnecting the preserved
   Zero/Jessie unit.
7. Only then enable the modern service as the normal deployment.
