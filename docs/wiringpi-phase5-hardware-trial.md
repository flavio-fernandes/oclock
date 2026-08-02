# WiringPi migration Phase 5 exact-board hardware trial

## Purpose and rollback boundary

Phase 3 selected and built the modern stack on a Raspberry Pi Zero W proxy.
Phase 5 must run the libgpiod candidate on the actual production Raspberry Pi
Zero Rev 1.2, revision `900092`, with the office-clock wiring unchanged.

The Trixie candidate and Jessie production environments are separate SD cards.
That makes a same-boot WiringPi-versus-libgpiod comparison impossible: Jessie
does not provide the selected GPIO v2/libgpiod 2.2 stack, while the clean
Trixie card intentionally has no preserved WiringPi installation. The modern
run is therefore compared with the accepted Phase 0 and Phase 1 production
evidence on the same physical board.

Rollback crosses a physical boundary and cannot be fully automated. The trial
script stops and restores any service that was active on the candidate card,
but the operator must power off before reinstalling the untouched Jessie card.

## Before moving the card

Preserve these identifiers from the accepted build:

| Item | Value |
| --- | --- |
| Source commit | `91d0645ff851a2e4c9cc74930ee58b69c9afeeea` |
| Binary SHA-256 | `a2bdd74e39b3f279a54c56d222ebe50135eeb6051e4f39f1cbe7b9a3332ef8f1` |
| Binary | ARM EABI5, armhf interpreter |
| GPIO dependency | `libgpiod.so.3` |
| Atomic dependency | `libatomic.so.1` |
| Forbidden dependency | WiringPi |

Copy or otherwise retain that exact binary on the Trixie card. Fetch the
current PR branch as well so its verifier is available. Do not rebuild between
copying the checksum and running the trial unless the new binary is inspected
and recorded separately.

## Physical procedure

1. Confirm the known-good Jessie card is labelled and the Phase 0 rollback
   binary remains preserved.
2. Gracefully shut down and remove power from the production Pi Zero.
3. Remove the Jessie card and keep it outside the trial system.
4. Insert the Trixie candidate card and boot the same non-W Pi Zero Rev 1.2.
5. Do not start `oclock` manually. Fetch the verifier and let it check the
   model, revision, OS, architecture, GPIO chip, throttling, binary checksum,
   dependencies, and port/service state before it requests confirmation.
6. Run the short trial and exercise display, strip, light, motion, and the
   normal MQTT data feed when prompted.
7. Share the generated archive even if a check fails.
8. Run `sudo poweroff`, wait until activity stops, and remove power.
9. Reinstall the Jessie card, boot, and confirm its WiringPi service, display,
   strip, light, motion, HTTP status, and MQTT behavior.

## Safety behavior

`misc/verifyPhase5GpiodHardware.sh` refuses to drive lines unless all of these
preconditions hold:

- model `Raspberry Pi Zero Rev 1.2`, revision `900092`;
- `armv6l` with `armhf` packages on Trixie;
- a discoverable `pinctrl-bcm2835` GPIO chip;
- no firmware throttling indication when `vcgencmd` is available;
- the supplied binary is 32-bit ARM and matches the operator-supplied SHA-256;
- libgpiod 3 ABI and libatomic resolve, WiringPi does not, and no dependency is
  missing.

The script requires the literal confirmation `RUN`, records status and process
samples, requests clean HTTP shutdown, escalates signals only during cleanup,
and restores a service that it stopped on the candidate card. Its EXIT and
signal traps remain active after GPIO use begins.

## Acceptance and later gates

The initial trial passes only if display and strip output are visually correct
and responsive, light and motion transitions appear in status samples, MQTT
connects, the external feed updates the display, and HTTP shutdown is clean.

A 60-second functional result is not soak or waveform evidence. After the
short exact-board check passes, retain the Jessie card as rollback and plan a
longer candidate-card soak plus logic-analyzer comparison before Phase 6
service deployment.
