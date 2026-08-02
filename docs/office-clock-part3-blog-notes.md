# Office Clock follow-up blog notes

## Purpose and maintenance rule

This is the living source notebook for a future follow-up to
[Office Clock Project Part 2: Software](https://flaviof.com/blog/hacks/office-clock-part2.html).
The likely article is “Office Clock Project Part 3” or “Modernizing the Office
Clock a Decade Later,” but the final title is deliberately undecided.

Update this file whenever PR 3 changes the migration decision, installation
procedure, hardware result, dependency set, deployment state, or rollback
story. Do not rely on chat history alone. Keep facts in these categories:

- **Historical** — what the 2016 article and original Pi Zero actually used.
- **Verified** — captured in repository tests or retained hardware evidence.
- **Selected** — approved direction, but not necessarily implemented or
  deployed.
- **Pending** — must not be written as a completed result in the article.

Raw capture archives remain outside Git because they can contain binaries and
local runtime/network details. Record only their sanitized conclusions here
and in the phase reports.

## Current editorial snapshot — 2026-08-02

The original office clock is still recoverable as a complete Raspberry Pi Zero
Rev 1.2/Jessie/WiringPi unit. A separate Raspberry Pi Zero W Rev 1.1 now runs
Raspberry Pi OS Lite 32-bit Trixie and is the selected modernization target.
It keeps the physical clock wiring while replacing the USB Wi-Fi dongle with
the Zero W's onboard radio.

The application now has a project-owned GPIO boundary, deterministic fake
hardware tests, a legacy WiringPi backend, and experimental libgpiod/mmap
backends. The first pure-libgpiod and mapped hardware trials were functional
but too slow, especially for the 240-pixel LPD8806 strip. Automatic dimming
also did not pass because the observed light values did not cross the existing
dark threshold.

The selected next architecture is mixed:

| Device | Selected modern transport | Existing BCM GPIOs | Status |
| --- | --- | --- | --- |
| Motion sensor | libgpiod v2 input | 10 | Implemented and functionally tested |
| LPD8806 strip | kernel `spi-gpio` plus explicit `spidev` binding | clock 20, data 21 | Offline overlay passed; live boot pending |
| MCP3002 ADC | second `spi-gpio` plus native `mcp320x`/IIO | clock 17, MISO 27, MOSI 22, CS 4 | Exact driver verified; conversion waits for strip proof |
| HT1632 matrix | narrow bulk mmap transport | CS 6, WR 13, data 19, select clock 26 | Selected direction; not implemented |

The 2026-08-02 read-only kernel-SPI run established that the exact Zero W
kernel has the SPI core, `spi-gpio`, and `spidev`, and that all office-clock
GPIO offsets are visible without consumers. `dtoverlay`, its overlay catalog,
`dtc`, and `fdtoverlay` are already installed by `raspi-utils-dt` and
`device-tree-compiler`. The collector's reported tooling failure was a false
negative: bare help returns status 1, while both list commands succeeded.

Exact package source also revealed a better ADC path. Kernel 6.18.39's
`mcp320x` driver supports `microchip,mcp3002` and the target armhf
configuration builds it as a module. The modern ADC can therefore use native
IIO raw channels. The exact `spidev` source rejects a generic Device Tree
`spidev` compatible, so the LPD8806 uses an honest project identifier followed
by an explicit, guarded `driver_override` binding.

Phase 6 deployment remains blocked. The article must not yet say that the
modern system is production-ready or that WiringPi has been completely
removed.

## The story worth telling

The useful narrative is not “replace one GPIO library with another.” It is:

1. A 2016 Raspberry Pi project continued doing real work for about a decade.
2. Its operating system, compiler, manually installed WiringPi, and external
   Wi-Fi dongle became the fragile parts—not the display hardware or original
   wiring.
3. Preserving a known-good physical rollback allowed the software to be
   modernized in measured phases.
4. A clean API replacement was functionally correct but exposed the cost of
   performing thousands of GPIO operations through a modern character-device
   path on ARMv6.
5. The failed timing trials informed a better design: use Linux's SPI
   subsystem for byte-oriented devices, retain libgpiod for ordinary GPIO,
   and keep only the truly nonstandard display protocol on a narrow bulk path.
6. Backward compatibility means the modern deployed unit can stop loading
   WiringPi while the old unit and legacy build remain usable as recovery.

Include the failed experiments. They explain why the final design is mixed
instead of pretending the destination was obvious from the beginning.

## What the 2016 Part 2 article says

The original software article is dated 2016-04-05. Its installation section
uses:

- Raspbian Jessie Lite written manually to an SD card with `dd`;
- the default `pi`/`raspberry` login and the old `raspi-config` flow;
- direct edits to `/etc/wpa_supplicant/wpa_supplicant.conf`;
- `ifdown wlan0` and `ifup wlan0` to apply wireless changes;
- `apt-get update` plus `dist-upgrade`;
- `git` and `libevent-dev` from APT;
- WiringPi cloned from `git://git.drogon.net/wiringPi` and installed with
  `./build` into `/usr/local`;
- the `rpi-0.1.y` application branch and plain `make`;
- a manually copied systemd unit in `/lib/systemd/system`;
- root/setuid execution for GPIO access.

The code section describes WiringPi-driven software clocks for the HT1632,
LPD8806, and MCP3002, plus a motion GPIO input. It calls arbitrary-GPIO
bit-banging “plenty fast” and says a full 240-pixel strip update completed
within a millisecond on the old stack. That historical observation is the key
performance comparison for the follow-up.

The old article lists MQTT as a future enhancement. MQTT publishing is now
implemented with libmosquitto for light, motion, display intensity, and display
mode status, and broker connectivity was exercised during the hardware trials.
The current source does not subscribe to a control topic, so do not describe
MQTT as an alternate display-input interface. The separately observed external
display update needs its actual path identified before the blog draft. MQTT
publishing is still a satisfying “future enhancement became real” sidebar.

Editorial correction: the original article calls the executable permission a
“sticky bit.” The captured executable is root-owned with the owner **setuid**
bit. Correct the terminology in the follow-up without silently rewriting the
historical article.

## Old and new installation comparison

This table should become the installation centerpiece of the eventual post.
Update the final column only after Phase 6 acceptance.

| 2016 Part 2 step | Modern status | Follow-up instruction |
| --- | --- | --- |
| Raspberry Pi Zero Rev 1.2 | Preserved rollback hardware | Use a separate Zero W Rev 1.1 for the modern deployment |
| Raspbian Jessie Lite | Obsolete for the modern unit | Raspberry Pi OS Lite 32-bit, Raspbian 13/Trixie |
| Manually image Jessie with `dd` | Historical | Prefer current Raspberry Pi Imager instructions; record exact image/version used for final deployment |
| Default `pi`/`raspberry` credentials | Obsolete and unsafe | Create credentials through the current imaging/first-boot flow; do not publish real credentials |
| USB Wi-Fi dongle | No longer needed on the modern unit | Use Zero W onboard 2.4 GHz Wi-Fi |
| Edit `wpa_supplicant.conf`; run `ifdown`/`ifup` | Obsolete on selected image | Use Trixie's NetworkManager flow; document the final headless provisioning commands after cold-boot validation |
| Clone WiringPi from `git.drogon.net`; run `./build` | Not needed for the modern build | Keep WiringPi only on the preserved Jessie rollback unit and for the explicit legacy source target |
| Install only `git libevent-dev` | Insufficient now | Install modern compiler/build, MQTT, GPIO, and overlay dependencies listed below |
| Clone branch `rpi-0.1.y` | Historical reproducibility branch | Link the merged modernization release/tag after PR 3 and deployment are complete |
| Plain `make` | Still intentionally means legacy WiringPi | Use the final explicit modern build profile; exact name is pending SPI implementation |
| Copy unit to `/lib/systemd/system` | Works historically, final path TBD | Prefer the packaged/reviewed unit and `systemctl`; record exact install path used on Trixie |
| Root/setuid executable | Preserved during migration | Do not claim privilege cleanup; service identity/device permissions are a separate decision |
| Software-bit-bang every peripheral | Too expensive through pure libgpiod | Use subsystem-specific transports: libgpiod, kernel SPI, and a narrow matrix bulk path |

## Installation details to preserve now

### Target platform — verified

- Raspberry Pi Zero W Rev 1.1, revision `9000c1`.
- Raspberry Pi OS Lite 32-bit, Raspbian GNU/Linux 13.6/Trixie.
- Linux `6.18.39+rpt-rpi-v6`, ARMv6 runtime, armhf packages.
- GCC/G++ 14.2.0.
- GPIO character-device ABI v2.
- libgpiod 2.2.1 and GPIO chip label `pinctrl-bcm2835`.
- Onboard Wi-Fi; USB dongle absent during acceptance.

These are evidence values, not timeless minimum-version promises. The final
post should distinguish “the versions I tested” from “the only versions that
can work.”

### Build dependencies — verified for the current libgpiod build

```sh
sudo apt update
sudo apt install -y build-essential pkg-config libevent-dev \
    libmosquitto-dev libgpiod-dev
```

The modern ARMv6 link also needs `libatomic`, which GCC supplies as a runtime
library and the Makefile links explicitly. `gpiod` command-line tools are
useful for read-only diagnosis. Do not add packages to the published command
merely because a development VM happened to contain them.

### Additional final SPI/overlay dependencies — partly verified

The discovery image already has the required inspection/build tools:

- `device-tree-compiler` provides `dtc` and `fdtoverlay`;
- `raspi-utils-dt` provides the working `dtoverlay` tool;
- the selected kernel packages provide `spi-gpio`, `spidev`, and `mcp320x`;
- Linux SPI userspace headers for `SPI_IOC_MESSAGE`; determine which existing
  development package owns the header on the accepted image;
- a project-owned overlay file installed under the authoritative Trixie boot
  overlay directory;
- a reviewed boot configuration line enabling that overlay;
- device-node ownership/permissions appropriate for the unchanged service
  privilege model.

Do not tell readers to run `modprobe`, edit `config.txt`, or bind `spidev`
until the exact reversible procedure passes on the Zero W.

### Build commands and profiles

These commands describe the current tree:

```sh
# Preserved original behavior: WiringPi hardware build.
make
make hardware

# Explicit pure-libgpiod experiment.
make GPIO_BACKEND=gpiod hardware

# Explicit libgpiod plus BCM2835 mmap-value experiment.
make GPIO_BACKEND=gpiod-mmap hardware

# Hardware-free development and tests.
make sandbox
make test
make check-arm-warnings
```

The future kernel-SPI profile name and final install command do not exist yet.
Add them here only when implemented and tested. Plain `make` must continue to
mean WiringPi throughout PR 3 so a checkout cannot silently change the legacy
deployment contract.

### Boot overlay procedure — written, not yet exercised live

The final procedure needs all of the following, in this order:

1. Show how to inspect current GPIO consumers and SPI devices.
2. Stop the application before any overlay claims its existing GPIOs.
3. Install the reviewed project overlay defining two independent `spi-gpio`
   controllers on the unchanged strip and ADC pins.
4. Explain the project-compatible plus explicit `spidev` override for the
   LPD8806 and native `microchip,mcp3002`/IIO binding for the ADC.
5. Add one clearly marked boot configuration entry; retain a byte-for-byte
   backup of the original configuration.
6. Reboot and verify controller, device-node, pin-consumer, and permission
   state before starting the application.
7. Provide disable and uninstall commands, including how to recover by
   editing the SD card from another machine if boot or GPIO ownership fails.
8. State that current libgpiod GPIO drivers must not request lines owned by the
   new SPI controllers.

The LPD8806 has no chip-select wire. The overlay and userspace transfer use the
kernel's no-CS behavior without inventing a wiring change. The MCP3002 keeps
CS on BCM 4, while its full-duplex protocol is performed by the native kernel
driver and exposed as two IIO raw channels.

### Service procedure — mostly preserved, final ordering pending

The current unit uses:

```text
WorkingDirectory=/home/pi/oclock.git
ExecStart=/home/pi/oclock.git/oclock
Restart=on-failure
After=network.target
```

The service still runs as root because no `User=` or `Group=` is set. The
legacy executable is root-owned and owner-setuid. PR 3 intentionally does not
combine GPIO migration with privilege hardening.

Before the final post, decide and verify whether the Trixie service needs:

- `network-online.target` rather than only `network.target`, based on MQTT and
  onboard-Wi-Fi cold-boot behavior;
- ordering after the SPI device nodes exist;
- any explicit supplementary group or udev rule;
- a modern unit installation directory such as `/etc/systemd/system` rather
  than reproducing the old `/lib/systemd/system` copy command.

Do not broaden this migration merely to make the blog instructions look more
modern. Record the tested deployment first; describe hardening as follow-up
work if it remains separate.

## What is no longer needed on the modern unit

Once the modern deployment passes—and not before—the follow-up can state:

- no USB Wi-Fi dongle;
- no Jessie installation or in-place Jessie upgrade;
- no clone/build/install of WiringPi under `/usr/local`;
- no `gpio readall` dependency for normal operation;
- no direct WiringPi calls outside the retained legacy backend;
- no per-edge userspace GPIO operations for full LPD8806 frames or MCP3002
  samples;
- no hardware-SPI pin rewiring for the selected `spi-gpio` design;
- no assumption that the Broadcom GPIO controller is always
  `/dev/gpiochip0` or `/dev/gpiochip4`.

Do not say that WiringPi disappeared from the repository. The compatibility
backend, default legacy build, original binary, Jessie SD card, and original
Pi remain intentional recovery assets.

## What the final modern installation will need

This is the publication checklist. Replace each pending item with the exact
tested command or file before drafting the article:

- [x] Separate Zero W instead of upgrading the original unit.
- [x] Raspberry Pi OS Lite 32-bit Trixie on ARMv6/armhf.
- [x] Onboard Wi-Fi connected without the USB dongle.
- [x] GCC 14, libevent, libmosquitto, libgpiod v2, and libatomic-capable build.
- [x] Project-owned GPIO API and explicit modern build selection.
- [x] Deterministic fake protocol tests and Incus validation.
- [x] `raspi-utils-dt` identified as the installed provider of `dtoverlay`.
- [x] Disabled-by-default Office Clock Device Tree overlay added to the repo.
- [x] Offline target merge of that overlay accepted on the exact Zero W.
- [x] Checksum-pinned install, enable, inspect, disable, and SD-card rescue
  commands written.
- [ ] Live overlay boot and normal-disable procedure exercised.
- [ ] Final overlay uninstall procedure exercised after the live gates.
- [ ] Project-owned SPI userspace transport plus deterministic fake.
- [ ] LPD8806 conversion preserving 720 GRB bytes and eight latch bytes.
- [ ] LPD8806 Zero W timing acceptance at the existing 12 ms application tick.
- [ ] MCP3002 native-IIO conversion and raw channel verification.
- [ ] Controlled dark/bright samples and a separate threshold decision.
- [ ] HT1632 bulk transport and timing acceptance.
- [ ] Final modern build profile name and binary dependency evidence showing
  no WiringPi.
- [ ] Cold-boot NetworkManager, HTTP, MQTT, and service-order validation.
- [ ] Overnight soak with CPU, latency, throttling, and Wi-Fi evidence.
- [ ] Physical rollback exercise using the preserved Zero/Jessie unit.
- [ ] Merged commit, release/tag, and stable source links for the article.

## Migration chronology and evidence

### Phase 0 — original production baseline, complete

- Raspberry Pi Zero Rev 1.2, Jessie, WiringPi 2.60 in `/usr/local`.
- Known-good binary preserved byte-for-byte outside Git.
- Display, strip, light, motion, MQTT, HTTP, and service were healthy.
- Short-interval CPU average: 3.55%.
- Existing pins and physical wiring captured.

### Phase 1 — application-owned GPIO interface, complete

- Direct WiringPi use isolated in one backend translation unit.
- Device classes receive the project-owned interface.
- Legacy pin order, delays, mutex, binary name, linking, and build defaults
  preserved.
- Corrected guarded hardware run passed display, strip, light, motion, MQTT,
  external updates, clean shutdown, and production restoration.

### Phase 2 — deterministic device protocol tests, complete

- Thread-safe configurable fake GPIO added.
- Tests cover initial levels, directions, bit ordering, GRB data, latch clocks,
  MCP3002 commands/data, motion mapping, cleanup, and serialization.
- These tests make transport changes reviewable without real GPIO hardware.

### Phase 3/4 — modern target and libgpiod, complete for build coverage

- Zero W/Trixie/libgpiod v2 target selected and built natively.
- Incus validates compatibility, fake protocols, sanitizers, ARM unsigned-char
  warnings, smoke behavior, and repeated graceful shutdown.
- Commit `91d0645` produced a 32-bit ARM EABI5 binary linking libgpiod and
  libatomic without WiringPi.

### Phase 5 — exact-board experiments, incomplete

- Pure libgpiod: functional, but dimming and timing failed.
- Restricted `/dev/gpiomem` probe: passed without driving a line.
- libgpiod plus mmap values: clearly faster and functional, but still too slow,
  especially on the strip; dimming still failed.
- Mapped candidate post-startup CPU average: 9.14% versus legacy 3.55%.
- Light samples ranged 478–1023 and never crossed the current low threshold
  of 360. Do not call that a proven ADC bug or change calibration from this
  run alone.
- Kernel-SPI discovery accepted: core, `spi-gpio`, `spidev`, GPIO metadata,
  overlay tools, and boot location passed. Exact source review found native
  MCP3002/IIO support and confirmed that LPD8806 requires an explicit spidev
  override. The disabled project overlay then compiled and merged against the
  exact active Zero W Device Tree with zero failures. Live boot is pending.

### Phase 6/7 — pending

- No modern candidate has deployment approval.
- No overlay has been enabled.
- No soak or physical rollback exercise has passed.
- The modern backend must not become the default until these gates complete.

## Technical explanations to prepare for readers

### Why libgpiod alone was not fast enough

A modern GPIO character-device call is appropriate for configuration, normal
inputs, and occasional values. It was a poor fit for producing every edge of a
high-rate software serial stream on this ARMv6 application. The 240-pixel strip
emits 5,760 data bits per frame and at least 11,648 fixed clock-edge writes
before data changes are counted. The per-edge path included locking, virtual
dispatch, pin lookup/validation, and either an ioctl or a mapped register
barrier.

### Why kernel `spi-gpio` is different from hardware SPI

`spi-gpio` is a kernel software SPI controller that can use arbitrary GPIOs,
so it preserves the existing wires. `spidev` gives the strip application a
buffered SPI transaction; one userspace operation can submit a complete frame
instead of making thousands of GPIO calls. The ADC goes one step further: its
native kernel driver performs the SPI transaction and presents IIO channel
files.

A fixed hardware SPI controller would be faster but uses designated alternate
function pins. The strip's current clock/data order and the ADC's arbitrary
pins do not match those interfaces, so hardware SPI would mean rewiring. It is
a fallback only if kernel software SPI misses the final timing budget.

### Why the HT1632 remains special

The matrix has an extra panel-select shift chain plus command/data fields of
3, 7, and 4 bits. That does not map cleanly to ordinary byte-oriented spidev
transactions. Keeping one narrowly bounded bulk implementation is clearer
than forcing unlike protocols through an artificial common abstraction.

### Why the ADC threshold is a separate question

The ADC can return changing values while the display still never dims. First
prove both MCP3002 IIO raw channels and record controlled covered/uncovered
values. Only then decide whether Trixie/transport behavior changed the signal
or whether the old threshold simply does not fit the new physical setup.

## Proposed article outline

1. **A ten-year-old clock still ticking**
   - Link Part 1 and Part 2.
   - Show the original Zero/Jessie/WiringPi unit and explain why it remains
     valuable as rollback.
2. **What aged and what did not**
   - Same panels, strip, ADC, PIR, power, wiring, HTTP modes, and animations.
   - Obsolete OS/toolchain/GPIO library and unnecessary Wi-Fi dongle.
3. **The compatibility contract**
   - Separate Zero W, no wiring change, legacy default retained, measurements
     before deployment.
4. **Building a seam around the hardware**
   - `Gpio` interface, WiringPi/libgpiod/fake implementations, protocol tests.
5. **The experiment that worked but was too slow**
   - Pure libgpiod and mmap results, CPU/timing observations, dimming nuance.
6. **Choosing the Linux subsystem that fits each device**
   - Motion/libgpiod, strip and ADC/kernel SPI, matrix/bulk path.
7. **Installing it in 2026**
   - Final OS image, NetworkManager, packages, overlay, build, service, and
     rollback commands. Fill only after acceptance.
8. **What ten years changed in the application**
   - MQTT graduated from “future enhancement”; graceful shutdown and tests;
     HTTP/display behavior remains familiar.
9. **Measurements and final acceptance**
   - Strip frame time, matrix timing, CPU, HTTP latency, light values, Wi-Fi,
     soak, and rollback.
10. **Lessons from keeping old hardware alive**
    - Modernization is not library substitution; preserve working baselines;
      failed measurements are design input.

## Visuals and snippets to capture before publication

- Original Pi Zero with USB Wi-Fi dongle beside the Zero W without it.
- Existing wiring/pin table, clearly labelled BCM rather than physical pin
  numbering.
- Small architecture diagram: application devices to WiringPi on legacy and
  to libgpiod/SPI/bulk transports on modern.
- `file` and `ldd` output for final ARM binary, showing no WiringPi.
- `gpiodetect`, the final strip `/dev/spidev*`, MCP3002 IIO channels, and GPIO
  consumer excerpts with local identifiers removed.
- A logic-analyzer view or measured frame-time table comparing legacy,
  libgpiod, mapped, and final SPI paths.
- CPU/HTTP latency comparison under the same animation workload.
- Covered/uncovered raw ADC values plus final threshold behavior.
- Cold-boot and overnight-soak summary.
- Screenshot/photo demonstrating MQTT-triggered external display input, since
  the 2016 post listed MQTT as future work.
- A short rollback photo or timeline showing the original unit returning to
  service.

Do not publish raw evidence archives, private broker addresses, SSIDs,
credentials, serial numbers, MAC addresses, or private network topology.

## Claims that must wait

Do not write any of these in past tense until their gates pass:

- “The Office Clock no longer uses WiringPi.”
- “Kernel software SPI is as fast as the original WiringPi implementation.”
- “The display dimming bug is fixed.”
- “The service reliably starts after Wi-Fi and SPI are ready.”
- “The Zero W is the deployed production clock.”
- “Rollback has been tested.”
- “These are the final installation commands.”

The safe phrasing before final deployment is: “The selected modern profile no
longer links WiringPi; the repository and original recovery unit retain it for
backward compatibility.”

## Source map for the future author

- Overall plan: [`wiringpi-migration.md`](wiringpi-migration.md)
- Original baseline: [`wiringpi-phase0-baseline.md`](wiringpi-phase0-baseline.md)
- GPIO seam: [`wiringpi-phase1-interface.md`](wiringpi-phase1-interface.md)
- Protocol tests: [`wiringpi-phase2-protocol-tests.md`](wiringpi-phase2-protocol-tests.md)
- Target image: [`wiringpi-phase3-target-baseline.md`](wiringpi-phase3-target-baseline.md)
- Zero W decision: [`wiringpi-zero-w-retarget.md`](wiringpi-zero-w-retarget.md)
- Modern backend: [`wiringpi-phase3-backend.md`](wiringpi-phase3-backend.md)
- First hardware result: [`wiringpi-phase5-initial-result.md`](wiringpi-phase5-initial-result.md)
- Mapped result: [`wiringpi-phase5-fast-result.md`](wiringpi-phase5-fast-result.md)
- Kernel-SPI discovery: [`wiringpi-phase5-kernel-spi-discovery.md`](wiringpi-phase5-kernel-spi-discovery.md)
- Kernel-SPI result: [`wiringpi-phase5-kernel-spi-result.md`](wiringpi-phase5-kernel-spi-result.md)
- Disabled SPI overlay gate: [`wiringpi-phase5-spi-overlay.md`](wiringpi-phase5-spi-overlay.md)
- Offline SPI overlay result: [`wiringpi-phase5-spi-overlay-result.md`](wiringpi-phase5-spi-overlay-result.md)
- Guarded live SPI boot: [`wiringpi-phase5-spi-live-boot.md`](wiringpi-phase5-spi-live-boot.md)
- Exact resume state: [`wiringpi-resume-handoff-2026-08-02.md`](wiringpi-resume-handoff-2026-08-02.md)
- Original hardware article: [Part 1](https://flaviof.com/blog/hacks/office-clock-part1.html)
- Original software article: [Part 2](https://flaviof.com/blog/hacks/office-clock-part2.html)

## Notebook update log

- **2026-08-02:** Created from the 2016 Part 2 installation/code narrative,
  Phases 0–5 evidence, the Zero W/Trixie retarget, failed libgpiod/mmap trials,
  selected mixed SPI architecture, and initial kernel-SPI collector summary.
  The raw SPI archive, overlay design, final dependencies, final install
  procedure, hardware timing, soak, and deployment remain pending.
- **2026-08-02:** Processed the exact kernel-SPI archive. Corrected the false
  `dtoverlay` failure, recorded installed overlay packages and exact downstream
  source hashes, selected native MCP3002/IIO plus explicit LPD8806 spidev
  binding, and added the disabled overlay/offline verification gate. Live
  overlay installation and all transfers remain pending.
- **2026-08-02:** Accepted the exact-board offline overlay merge with zero
  failures. Recorded source/artifact/archive hashes and the unchanged pin map.
  Added checksum-pinned live enablement, timestamped boot-config backup,
  read-only post-boot inspection, normal disablement, and offline SD-card
  rescue instructions. None has been exercised live yet.
