# WiringPi migration Phase 5 Zero W hardware trial

## Purpose and rollback boundary

Phase 3 selected and built the modern stack on a Raspberry Pi Zero W Rev 1.1,
revision `9000c1`. That Zero W is now the intended modern deployment board.
Phase 5 must connect it to the office-clock wiring unchanged and run the
selected `gpiod-mmap` candidate against the real electrical load.

The modern Zero W/Trixie and legacy Zero/Jessie environments are separate
complete units. A same-boot WiringPi-versus-libgpiod comparison is neither
possible nor desirable: Jessie does not provide the selected GPIO v2/libgpiod
2.2 stack, while the clean Trixie card intentionally has no preserved WiringPi
installation. The modern run is compared with the accepted Phase 0 and Phase 1
legacy evidence and deterministic protocol traces.

Rollback crosses a physical boundary and cannot be fully automated. The trial
script stops and restores any service active on the Trixie card, but the
operator must power off before reconnecting the preserved Zero/Jessie unit.

## Before connecting the target

Preserve these identifiers from the accepted build:

| Item | Value |
| --- | --- |
| Source commit | `1f5605dbc6e8b43165190a150d47c9c1fee86a9d` |
| Binary SHA-256 | `af7a168af98c7e4765a91d70fd2926281e47fe647e804ccc24d0db92e3c2338e` |
| Binary | ARM EABI5, armhf interpreter |
| GPIO dependency | `libgpiod.so.3` |
| Atomic dependency | `libatomic.so.1` |
| Fast-value path | `/dev/gpiomem` |
| Forbidden dependency | WiringPi |

Retain that exact binary persistently on the Zero W's Trixie card. Fetch the
current PR branch so its verifier is available. Do not rebuild between copying
the checksum and running the trial unless the new binary is inspected and
recorded separately. Remove the USB Wi-Fi dongle; onboard Wi-Fi is part of the
target acceptance.

## Physical procedure

1. Confirm the original Zero, Jessie card, and Phase 0 binary remain together,
   labelled, and known-good as the rollback unit.
2. Gracefully shut down both boards and remove power.
3. Transfer the unchanged office-clock GPIO harness and normal power connection
   from the original Zero to the Zero W. Leave the USB Wi-Fi dongle detached.
4. Boot the Zero W from its Trixie card and reconnect over onboard Wi-Fi.
5. Do not start `oclock` manually. Fetch the verifier and let it check the Zero
   W model/revision, OS, architecture, GPIO chip, throttling, connected Wi-Fi,
   binary checksum, dependencies, and port/service state.
6. Run the short trial and exercise display, strip, light, motion, and the
   normal MQTT data feed when prompted.
7. Share the generated archive even if a check fails.
8. On failure, run `sudo poweroff`, wait until activity stops, disconnect the
   Zero W, and reconnect the preserved Zero/Jessie unit.
9. Confirm rollback health: WiringPi service, display, strip, light, motion,
   HTTP status, and MQTT behavior.

## Safety behavior

`misc/junk/wiringpi-migration/verifyPhase5GpiodHardware.sh` refuses to drive lines unless all of these
preconditions hold:

- model `Raspberry Pi Zero W Rev 1.1`, revision `9000c1`;
- `armv6l` with `armhf` packages on Trixie;
- a discoverable `pinctrl-bcm2835` GPIO chip;
- a Wi-Fi device connected through NetworkManager, with no USB dongle attached;
- no firmware throttling indication when `vcgencmd` is available;
- the supplied binary is 32-bit ARM and matches the operator-supplied SHA-256;
- `/dev/gpiomem` is a readable and writable character device, and its exact
  path is embedded in the supplied binary;
- libgpiod 3 ABI and libatomic resolve, WiringPi does not, and no dependency is
  missing.

The script requires the literal confirmation `RUN`, records status and process
samples, requests clean HTTP shutdown, escalates signals only during cleanup,
and restores a service that it stopped on the Trixie card. Its EXIT and
signal traps remain active after GPIO use begins.

## Acceptance and later gates

The first exact-board trial on 2026-08-02 did not pass. Functional I/O and
onboard Wi-Fi worked, but the operator rejected dimming behavior and the
noticeably slower display/strip timing. See the
[initial Phase 5 result](wiringpi-phase5-initial-result.md). Phase 6 remains
blocked; the pure `libgpiod` candidate is not a deployment candidate.

The follow-up `gpiod-mmap` run also failed. It improved speed, preserved all
functional paths, and shut down cleanly, but did not cross the dark threshold
and still missed the operator's timing gate, particularly for the LED strip.
See the [`gpiod-mmap` result](wiringpi-phase5-fast-result.md). Phase 6 remains
blocked.

The follow-up fast-path trial passes only if display and strip output are
visually correct and responsive, light and motion transitions appear in status
samples, MQTT connects, the external feed updates the display, onboard Wi-Fi
remains stable, HTTP shutdown is clean, and sustained light samples cross both
existing automatic-dimming thresholds.

A short functional result is not soak or waveform evidence. After the guarded
check passes, retain the complete Zero/Jessie rollback unit and run a
longer Zero W network/application soak plus logic-analyzer comparison before
Phase 6 service deployment.
