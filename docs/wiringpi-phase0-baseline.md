# WiringPi migration Phase 0 baseline

> Historical checkpoint: build-selection statements below describe Phase 0.
> The current tree follows the
> [modern-only build policy](wiringpi-modern-build-policy.md); rollback is the
> preserved physical Zero/Jessie unit.

## Result

Phase 0 was completed on the production Raspberry Pi Zero on 2026-07-30.
The existing clock remained active throughout the collection, all four hardware
functions were observed working, and the captured software state is sufficient
to preserve the current deployment while the GPIO boundary is introduced.

This is a sanitized summary of the production evidence. The raw archive is not
stored in Git because it contains a known-good executable and local runtime and
network details.

## Evidence provenance

| Item | Value |
| --- | --- |
| Collection archive | `oclock-phase0-20260730T214140Z-6cfETNTP.tar.gz` |
| Archive SHA-256 | `e50e238d48dba29c4fff9e44b9bf6b8e146028f15f54121e86f4ff56c476a0b5` |
| Collector result | 0 required failures, 1 explained warning |
| Production source | `536b369f32626280a1815d29dc566391a01ba3c9` |
| Source worktree | clean |
| Deployed binary SHA-256 | `86e0c8a3b2a7b8f2f28b9bafbd8f1bc00ef17a0c94f373233be11e9ff06de0c0` |
| Rollback copy | byte-for-byte identical to the deployed binary |

The source revision is the graceful-worker-shutdown change later merged by
PR 2. The collector warning was not a failed observation: the operator entered
`no` for the optional “Additional notes” prompt, and the original validation
mistook any `no` in the notes file for a negative hardware result. The required
display, LED strip, light sensor, motion sensor, and service-health answers were
all `yes`. The collector has been corrected to distinguish optional notes from
required checks.

## Platform and installed GPIO stack

| Item | Observed value |
| --- | --- |
| Board | Raspberry Pi Zero Rev 1.2, 512 MB, ARMv6 |
| OS | Raspbian GNU/Linux 8 (Jessie) |
| Kernel | Linux 4.9.35+, `armv6l` |
| Firmware | 2017-07-03, revision `4139c62f14cafdb7d918a3eaa0dbd68cf434e0d8` |
| Power/throttling | `get_throttled=0x0`; proper supply accepted as a project input |
| Temperature | 39.0 C |
| Boot GPIO settings | SPI disabled; I2C disabled |
| GPIO character devices | `/dev/gpiochip0` and `/dev/gpiomem` present |
| WiringPi | 2.60 in `/usr/local`, manually installed rather than package-managed |
| WiringPi library SHA-256 | `848cfd6b058ab22eb79c828896e9ba81002cc285d6e9cbd7c5bec37a8f3e44a9` |

The deployed executable dynamically loads `/usr/local/lib/libwiringPi.so`.
Raspbian's package database does not own either that library or the
`/usr/local/bin/gpio` executable. Do not remove or replace these files during
the interface and test phases. They are part of the legacy rollback
environment.

The old kernel exposing a GPIO character device does not by itself make this a
supported modern `libgpiod` target. Backend selection still waits for the
separate modern SD-card image and API checks in Phase 3.

## Wiring and live GPIO snapshot

The project accepts the power supply and physical wiring as known good. The
wiring sources are the current code and the original
[hardware](https://flaviof.com/blog/hacks/office-clock-part1.html) and
[software](https://flaviof.com/blog/hacks/office-clock-part2.html) articles.
`gpio readall` confirmed that every configured direction matched the source:

| Device | Signal | BCM GPIO | Physical pin | Observed mode |
| --- | --- | ---: | ---: | --- |
| HT1632 display | chip select | 6 | 31 | output |
| HT1632 display | write clock | 13 | 33 | output |
| HT1632 display | data | 19 | 35 | output |
| HT1632 display | auxiliary clock | 26 | 37 | output |
| LPD8806 strip | data | 21 | 40 | output |
| LPD8806 strip | clock | 20 | 38 | output |
| MCP3002 light ADC | clock | 17 | 11 | output |
| MCP3002 light ADC | ADC data out / Pi data in | 27 | 13 | input |
| MCP3002 light ADC | ADC data in / Pi data out | 22 | 15 | output |
| MCP3002 light ADC | chip select | 4 | 7 | output |
| Motion sensor | value | 10 | 19 | input |

The values in `gpio readall` were an instantaneous sample taken while
software-clocked transfers were active. They are not safe idle-level
requirements and must not be turned into golden tests. Initial values, transfer
ordering, and cleanup behavior will instead be captured from the current code
in Phases 1 and 2 and compared on hardware in Phase 5.

## Functional and runtime observations

The collector took 45 successful status samples over approximately 60 seconds:

- the display, LED strip, light sensor, motion sensor, and service were all
  visually confirmed healthy;
- motion produced both states and 8 transitions;
- the light sensor changed across a range of 390 to 613, with a mean of 570.8;
- display modes included `basicClock` and `message`;
- LED-strip modes included `rainbow`, `fill`, and `manual`;
- the MQTT connection was healthy in all 45 samples;
- every status request succeeded;
- the service remained active and the captured journal contained no error,
  failure, crash, or restart-loop message.

At capture time the process ran as root with 12 threads. Its long-running CPU
figure was 2.9%, resident memory stayed between 4,772 and 4,776 KiB, and the
short-interval CPU samples averaged 3.55% with a range of 2.31% to 15.38%.
These figures are comparison baselines, not hard performance limits; Phase 5
must compare both backends under the same workload and examine latency and GPIO
waveforms.

## Deployment and rollback contract

The production unit was enabled and active with:

- working directory `/home/pi/oclock.git`;
- executable `/home/pi/oclock.git/oclock`;
- `Restart=on-failure`;
- no configured service user or group, so the process ran as root.

The executable was a root-owned, owner-setuid, 32-bit ARM EABI5 binary with no
file capabilities. That privilege model is intentionally preserved for the
legacy deployment while GPIO behavior changes. Removing setuid or changing the
service identity remains a separate hardening project.

The raw capture contains a byte-identical rollback executable plus its mode,
ownership, dependency, and checksum records. It must remain retained outside
the Pi and outside Git. As planned, it was not launched while the running
service owned GPIO. Executable rollback will be verified in the controlled
Phase 6 maintenance window.

## Phase 0 exit decision

Phase 0 is complete. Work may proceed to Phase 1 with these constraints:

1. Plain `make` and the deployed service continue to select WiringPi.
2. The pin numbers, directions, initialization, cleanup, mutex ordering, and
   software-clocked protocols remain behaviorally unchanged.
3. No wiring, SPI/I2C boot setting, privilege, service, network, or OS change is
   combined with the GPIO interface refactor.
4. The manually installed WiringPi 2.60 runtime and captured binary remain
   available for rollback.
5. No claim about modern-backend timing is made until the Phase 5 Pi Zero
   comparison passes.
