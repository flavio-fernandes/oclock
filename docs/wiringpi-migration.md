# WiringPi migration plan

## Status and decision

Phase 0 and Phase 1 preserve the original Zero/Jessie production baseline and
are recorded in the [production baseline](wiringpi-phase0-baseline.md) and
[GPIO interface report](wiringpi-phase1-interface.md). Phases 2 through 4
selected, implemented, and built the modern backend. The intended replacement
is now the captured Raspberry Pi Zero W Rev 1.1 running Raspberry Pi OS Lite
32-bit Trixie; the original Zero/Jessie unit remains the complete rollback
system. See the [retarget decision](wiringpi-zero-w-retarget.md).

The migration should remove direct WiringPi use from the application and device
drivers without making a new GPIO stack a prerequisite for the existing
Raspberry Pi Zero. The legacy build must keep working while the modern Zero W
replacement is proven on the same wiring and electrical load.

The recommended design is:

- put a small, project-owned GPIO interface between the application and all
  platform libraries;
- retain a legacy WiringPi backend for the existing Raspbian 8 (Jessie) system;
- retain and improve the fake backend for development and protocol tests;
- add a `libgpiod` backend as an explicit opt-in for a supported modern
  Raspberry Pi OS;
- migrate software-clocked devices to kernel SPI only as a separate, optional
  hardware profile, because the current wiring does not match the relevant SPI
  pin assignments.

Do not replace WiringPi calls with `libgpiod` calls throughout the existing
drivers. That would couple device protocols to another platform API and make
rollback difficult.

## Compatibility contract

Until the Zero W replacement passes hardware acceptance and soak, all of these
are requirements:

1. Plain `make` still builds the WiringPi-backed `oclock` binary, then preserves
   the existing `root:root` and owner-setuid installation behavior.
2. The Jessie systemd unit, executable path, command-line options, network
   defaults, HTTP behavior, MQTT behavior, and GPIO numbering do not change.
3. No existing wire moves and no boot-overlay changes are required.
4. The original Zero, Jessie card, and known-good binary remain together and
   available as the physical rollback unit.
5. `make sandbox`, `make test`, and `make check-arm-warnings` continue to work
   without GPIO hardware or WiringPi.
6. A modern build can be selected explicitly and must not link WiringPi.
7. The WiringPi backend is not deleted when the modern backend becomes usable.
   It becomes a compatibility backend receiving only maintenance fixes.

Changing the default backend is a final deployment decision, not an early
refactor step.

## Current hardware inventory

`wiringPiSetupGpio()` means every number below is a Broadcom GPIO number, not a
physical header-pin number. This table records the source as of this plan and
must become an executable pin-map test before backend work begins.

| Device | Signal | BCM GPIO | Current direction | Source |
| --- | --- | ---: | --- | --- |
| HT1632 display | chip select | 6 | output | `src/display.cpp` |
| HT1632 display | write clock | 13 | output | `src/display.cpp` |
| HT1632 display | data | 19 | output | `src/display.cpp` |
| HT1632 display | auxiliary clock | 26 | output | `src/display.cpp` |
| LPD8806 strip | data | 21 | output | `src/ledStrip.cpp` |
| LPD8806 strip | clock | 20 | output | `src/ledStrip.cpp` |
| MCP3002 light ADC | clock | 17 | output | `src/lightSensor.cpp` |
| MCP3002 light ADC | ADC data out / Pi data in | 27 | input | `src/lightSensor.cpp` |
| MCP3002 light ADC | ADC data in / Pi data out | 22 | output | `src/lightSensor.cpp` |
| MCP3002 light ADC | chip select | 4 | output | `src/lightSensor.cpp` |
| motion sensor | value | 10 | input | `src/motionSensor.cpp` |

The Raspberry Pi SPI1 functions use GPIO20 for MOSI and GPIO21 for SCLK. The
deployed LPD8806 wiring uses those two GPIOs in the opposite roles. The MCP3002
also uses arbitrary GPIOs instead of the normal SPI0 pins. Enabling a SPI
overlay or replacing either driver with `spidev` would therefore break the
current wiring. Hardware SPI remains worth considering, but only in a named
rewired hardware profile after the GPIO migration is complete.

The current code serializes GPIO access with one `std::recursive_mutex`. Preserve
that ordering initially. Per-device locks or concurrent transfers would be a
separate behavior change.

## Why a measured migration is necessary

Motion sensing is a one-second digital read, but the other devices are
software-clocked:

- the HT1632 driver emits commands and display memory by toggling GPIO;
- each LPD8806 update shifts 720 data bytes for 240 pixels plus its latch
  clocks;
- each MCP3002 sample shifts a command and reads ten result bits;
- the display and LED threads use a 12 ms fast tick.

WiringPi and the GPIO character-device API have different call paths and timing
costs. Functional unit tests alone cannot show that pulse widths, frame time,
CPU use, or scheduling jitter remain acceptable on a Pi Zero. The new backend
must be measured; acceptable timing must not be assumed.

The Linux GPIO documentation also recommends using a proper kernel subsystem,
such as SPI, when one fits the device. That is a good long-term direction, but
it does not override the no-rewiring compatibility contract.

## Proposed internal boundary

Phase 1 added a small interface under `src/gpio/`. Its public types are owned by
this project and do not expose WiringPi or `libgpiod` headers. It contains only
the behavior currently used:

```cpp
enum class GpioValue { low, high };

class Gpio {
public:
  virtual ~Gpio() {}
  virtual bool initialize() = 0;
  virtual void configureInput(int bcmGpio) = 0;
  virtual void configureOutput(int bcmGpio, GpioValue initialValue) = 0;
  virtual GpioValue read(int bcmGpio) = 0;
  virtual void write(int bcmGpio, GpioValue value) = 0;
  virtual void delayMilliseconds(unsigned int duration) = 0;
};
```

Phase 1 used a separate output-direction operation to preserve the exact legacy
call order. Phase 2 established the safe initial levels from protocol traces
and added the initial value to `configureOutput`. The WiringPi fallback retains
its legacy `pinMode`-then-`digitalWrite` order; a modern backend must apply the
direction and initial value together where its API supports it. These details
remain required before a modern backend is deployed:

- output direction and initial value must be applied together where the backend
  supports it, avoiding a startup glitch;
- ownership and release behavior must be explicit; the current drivers restore
  several output pins to inputs;
- errors need operation, chip, GPIO offset, and backend context;
- the modern backend must identify the intended GPIO chip by label or verified
  configuration rather than assuming `/dev/gpiochip0`;
- line offsets must be verified against Broadcom numbering on the target image;
- line requests shared by different device objects need a defined lifetime;
- backend objects should be injected into device drivers rather than accessed
  through global C function names.

Keep the current mutex outside or immediately inside this boundary during the
first migration. Do not combine dependency injection with a locking redesign.

## Phased implementation

All phases are tracked in PR 3. Keep phase changes in reviewable commits, and
leave the tree deployable at the end of each phase.

### Phase 0: capture the production baseline

**Status: complete.** The 2026-07-30 production capture passed every required
check. See the [sanitized baseline](wiringpi-phase0-baseline.md) for the
hardware, software, runtime, and rollback evidence. The raw archive remains
outside Git.

The power supply and physical wiring are accepted as known-good inputs. The
wiring record is the BCM inventory above, the current source, and the original
[hardware](https://flaviof.com/blog/hacks/office-clock-part1.html) and
[software](https://flaviof.com/blog/hacks/office-clock-part2.html) build
articles.

Before changing GPIO code, extract the collector from this PR without switching
the deployed worktree, then run it:

```sh
cd /home/pi/oclock.git
phase0_collector=/tmp/collectHardwareBaseline.sh
git fetch origin agent/plan-wiringpi-migration
git show FETCH_HEAD:misc/collectHardwareBaseline.sh >"${phase0_collector}"
chmod 0755 "${phase0_collector}"
sudo "${phase0_collector}"
```

It samples for 60 seconds by default. During that window, walk into and out of
the PIR sensor's field of view and visually check the display and LED strip.
The script records:

- Pi model, OS, kernel, firmware, boot configuration, and throttling state;
- WiringPi version and how it was installed;
- the systemd unit, selected properties, status, recent journal, and process
  resource samples;
- GPIO device nodes, `gpio readall`, executable metadata, dependencies,
  capabilities, and checksum;
- repeated read-only `/status` responses covering display mode, LED-strip mode,
  light values, motion changes, and MQTT state;
- source revision and worktree state;
- a byte-identical copy of the active executable with ownership, mode, and
  checksum metadata;
- an operator checklist for the visual observations.

The script does not stop or restart the clock, drive GPIO, install packages, or
change configuration. It creates a timestamped directory and `.tar.gz` archive.
Answer its observation prompts, review `operator-notes.md`, then retain the
archive off-device.

The active service plus its executable checksum establishes which binary is
known-good. Do not start the rollback copy concurrently with the service merely
to test it; verify executable rollback during the Phase 6 maintenance window.
Logic-analyzer capture belongs in the Phase 5 side-by-side test, where both
backends can be measured under the same procedure.

### Phase 1: introduce the interface with no production change

**Status: complete.** The Jessie build and corrected Pi Zero functional
acceptance passed on 2026-07-31 UTC.
See the
[Phase 1 interface report](wiringpi-phase1-interface.md) for the concrete API,
backend selection, preserved behavior, repository enforcement, and safe
hardware build handoff.

1. Add the project-owned GPIO interface.
2. Move every WiringPi include and call into one WiringPi backend translation
   unit.
3. Convert `main`, `MotionSensor`, `Mcp300x`, `HT1632Class`, and `LPD8806` to use
   the injected interface.
4. Convert `fakeWiringPi` into a fake implementation of the same interface.
5. Keep `make`, `make hardware`, binary names, link flags, pin values, locking,
   and service behavior unchanged.
6. Add a repository check that rejects direct WiringPi includes or calls
   outside the legacy backend.

At the end of this phase, the production executable should still use WiringPi
and should emit the same GPIO operation traces as the baseline implementation.
This phase provides isolation, not a new deployment.

### Phase 2: make protocol behavior testable

**Status: complete.** The deterministic protocol suite and the full Incus
validation passed on 2026-07-31 UTC. See the
[Phase 2 protocol report](wiringpi-phase2-protocol-tests.md) for the startup
levels, fake-backend API, wire-level assertions, cleanup contract, and
validation evidence.

Enhance the fake backend so tests can configure input values and record ordered
operations. Add focused tests for:

- initialization direction and safe initial output level for every pin;
- HT1632 command bit order, select behavior, and render transaction boundaries;
- LPD8806 GRB byte order, 240-pixel transfer length, and latch clock count;
- MCP3002 command bits, sampled input bits, returned value, and chip-select
  lifetime;
- motion input mapping;
- cleanup that returns the same pins to input as the legacy code;
- serialization of complete device transactions.

Use golden protocol traces only for intentional device-level behavior. Do not
freeze incidental C++ call structure into the tests.

### Phase 3: select and add the modern backend

**Status: complete.** The accepted capture selects Raspberry Pi OS 32-bit,
Debian 13/Trixie, Linux GPIO ABI v2, and libgpiod 2.2 on ARMv6/armhf. Commit
`91d0645` built successfully on that ARMv6/armhf target and linked libgpiod and
libatomic without WiringPi. See the sanitized
[target baseline](wiringpi-phase3-target-baseline.md) and the
[target-selection handoff](wiringpi-phase3-target-selection.md). The opt-in
backend and its host-side validation are described in the
[Phase 3 backend handoff](wiringpi-phase3-backend.md).

The capture and build ran on the intended BCM2835 Zero W target. They establish
OS, compiler, API, chip-label, offset, and binary compatibility, but not timing
against the real peripheral load. Phase 5 remains the physical acceptance gate.

Select the production OS and kernel before selecting a `libgpiod` API version:

1. Boot a separate modern target and SD card; do not upgrade the working
   Zero/Jessie rollback unit in place.
2. Confirm the image supports ARMv6 and exposes GPIO character devices.
3. Record `uname -a`, `gpiodetect`, and `gpioinfo`.
4. Confirm which `libgpiod` major version the image supports.
5. Prefer the version 2 API on a kernel supporting GPIO character-device ABI
   v2. The v1 ABI first appeared in Linux 4.8 and is now obsolete, so do not
   choose it merely because the current Debian 12 VM packages libgpiod 1.6.3.
6. If the selected Pi image offers only libgpiod 1.x, make an explicit decision
   between a small, time-limited v1 backend and selecting a newer image. Keep
   v1 and v2 implementation details in separate translation units rather than
   scattering version conditionals through device drivers.

The modern build should be explicit, for example:

```sh
make GPIO_BACKEND=gpiod hardware
```

It must fail clearly when the requested API or GPIO chip is unavailable. An
unknown backend value must also fail instead of silently falling back.

Initially preserve polling, pin directions, bit order, and mutex behavior.
Using edge events for motion is a possible later optimization, not part of the
first equivalence test.

### Phase 4: validate in Incus

**Status: complete for host-side coverage.** A Debian 13/Trixie container with
GCC 14.2.0 and libgpiod 2.2.1 compiled and linked the modern backend. The full
fake-GPIO, protocol, compatibility, sanitizer, warning, smoke, and shutdown
suite passed. The container's deliberate lack of `/dev/gpiochip*` also
confirmed a safe, contextual initialization failure.

Keep the `oclock-dev` VM for repeatable x86 testing. On 2026-07-30 it was
Debian 12 with kernel 6.1; `libgpiod-dev` 1.6.3 was available but not installed.
The VM exposed no `/dev/gpiochip*` and had neither `gpio-mockup` nor `gpio-sim`
installed, so it can currently validate compilation, fake traces, application
tests, sanitizers, and warnings, but not real GPIO timing.

For each implementation PR:

```sh
make sandbox
make test
make check-arm-warnings
make valgrind
git diff --check
```

Add an Incus image or snapshot with the selected `libgpiod` development package
for compile and link coverage. If a GPIO simulator is added later, use it to
test line request, direction, value, contention, and error paths. Continue to
keep protocol trace tests on the project fake because they need deterministic
operation history.

An x86 VM result is never evidence that Pi Zero pulse timing is acceptable.

### Phase 5: run the Zero W hardware trial

**Status: target peripheral trial pending.** The accepted software capture and
build used the intended Zero W/Trixie target. It must now drive the unchanged
office-clock wiring without the USB Wi-Fi dongle. See the guarded
[Phase 5 hardware-trial handoff](wiringpi-phase5-hardware-trial.md).

Run the accepted libgpiod binary on the Zero W and compare it with Phase 0,
Phase 1, and protocol-trace evidence from the preserved Zero/Jessie unit:

- startup and shutdown pin levels, including visible glitches;
- HT1632 bit order, clock idle state, pulse widths, and full render time;
- LPD8806 bit order, latch sequence, full-strip frame time, and animation
  smoothness;
- MCP3002 clocking and light values across dark and bright conditions;
- motion transitions;
- CPU and memory use;
- HTTP response latency while display and strip updates are busy;
- onboard Wi-Fi association, boot-time reconnection, signal stability, HTTP,
  and MQTT behavior without the USB dongle;
- logs and recovery after intentional initialization failures;
- at least an overnight soak test.

Use a logic analyzer for waveform comparison where possible. The acceptance
criterion is correct device behavior with margin and no missed application
deadlines, not identical nanosecond timing.

If the GPIO backend or onboard Wi-Fi cannot meet the acceptance budget,
reconnect the preserved Zero/Jessie unit. Move affected devices to a kernel
driver or `spidev` only in a later rewired profile. Do not hide a timing failure
by reducing refresh behavior.

### Phase 6: opt-in deployment with rollback

Only after Phase 5 passes:

1. Install the modern binary and service on the Zero W/Trixie unit; preserve
   application arguments, paths, and runtime defaults initially.
2. Confirm NetworkManager reconnects onboard Wi-Fi after a cold boot and the
   service starts only after usable networking.
3. Repeat the functional checklist and monitor it for several days.
4. Exercise rollback by powering off the Zero W and reconnecting the preserved
   Zero/Jessie unit. No package downgrade or source rebuild should be required.

The Zero W, Trixie, GCC 14, libgpiod, and onboard Wi-Fi are the approved target
change. Do not additionally combine this with removal of setuid, service-user
changes, application-default changes, or rewiring. Those may be good follow-up
projects, but they make failures harder to attribute and rollback harder to
trust.

### Phase 7: make modern hardware the preferred path

After a sustained successful deployment:

- document the tested Pi image, kernel, `libgpiod`, and firmware versions;
- make the modern backend the preferred target for that OS;
- keep an explicit legacy target that reproduces the current Pi Zero build;
- archive the baseline binary, SD-card image, wiring record, and acceptance
  results;
- stop adding features to the WiringPi backend, but keep it buildable and
  covered by interface-level tests.

“No longer using WiringPi” should mean the actively deployed modern image and
normal modern builds do not load or link it. It should not mean removing the
only tested recovery path for the original hardware.

## PR acceptance checklist

Every migration PR should answer all of these:

- Does plain `make` still preserve the legacy deployment contract?
- Is the production backend choice explicit in the diff and build output?
- Can the sandbox and tests run without WiringPi?
- Are all GPIO numbers still Broadcom numbers with the same directions?
- Are initial output values and release behavior defined?
- Are errors actionable and free of silent fallback?
- Does the PR avoid unrelated service, network, privilege, and wiring changes?
- Is rollback possible by selecting the previous binary or backend?
- Were Incus checks run?
- If GPIO behavior changed, were Pi Zero hardware and timing checks run?

## Decision points that need hardware evidence

The following should remain open until measured:

- whether character-device GPIO is fast and stable enough for the HT1632,
  LPD8806, and MCP3002 software clocks on a Pi Zero;
- which current Raspberry Pi OS image is the supportable modern baseline;
- whether a libgpiod v1 transition backend has enough value to justify its
  maintenance;
- whether the LED strip and ADC should eventually be rewired for hardware SPI;
- whether pin ownership can be split per device without changing scheduling.

## References

- [Linux GPIO character-device API v2](https://www.kernel.org/doc/html/latest/userspace-api/gpio/chardev.html)
- [Linux GPIO character-device API v1 and deprecation status](https://www.kernel.org/doc/html/latest/userspace-api/gpio/chardev_v1.html)
- [libgpiod documentation](https://libgpiod.readthedocs.io/)
- [Linux SPI userspace API](https://www.kernel.org/doc/html/latest/spi/spidev.html)
- [Raspberry Pi GPIO and SPI pin mappings](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html)
