# WiringPi migration plan

## Status and decision

**Where this stands as of 2026-08-03.** Phases 0 through 5 are complete and
Phase 6's blocking items are done. The modern stack runs the real Office Clock
on the Zero W, starts itself at boot, and has held an 8 h 53 min unattended
soak at zero restarts. Phase 7 has not started.

| Still open | Why it is not a gate |
| --- | --- |
| Rollback rehearsal | The Zero/Jessie unit is preserved and powered off; swapping it back has never been practiced |
| Hard power-cut recovery | Only a clean reboot has been tested |
| Multi-day observation | Nine hours is the longest run |
| Service trimming (Phase 7) | Deferred deliberately; a before measurement now exists |
| Final service identity and device permissions | Never in scope for this migration |

None of those require code changes. They require elapsed time, a physical
swap, and a separate hardening decision.

Phase 0 and Phase 1 preserve the original Zero/Jessie production baseline and
are recorded in the [production baseline](wiringpi-phase0-baseline.md) and
[GPIO interface report](wiringpi-phase1-interface.md). Phases 2 through 4
selected, implemented, and built the modern backend. The intended replacement
is now the captured Raspberry Pi Zero W Rev 1.1 running Raspberry Pi OS Lite
32-bit Trixie; the original Zero/Jessie unit remains the complete rollback
system. See the [retarget decision](wiringpi-zero-w-retarget.md).

The migration removes WiringPi from the supported current-tree hardware build.
The original Raspberry Pi Zero is protected as a complete physical rollback
unit rather than by requiring the modern branch to rebuild its Jessie stack.
The separate Zero W has now been proven on the same wiring and electrical load:
not one wire moved, and the whole application passed its trial and runs on it.

Keep the future public narrative synchronized with the living
[Office Clock follow-up blog notes](office-clock-part3-blog-notes.md). Update
that notebook whenever a phase changes the installation, dependency,
architecture, hardware result, deployment state, or rollback instructions.

For the remainder of the exact-board work, the user authorized ordinary
OpenSSH over Tailscale as a maintenance path. It is documented separately in
[remote maintenance access](oclock-remote-access.md) and is not an application
dependency or deployment prerequisite.

The recommended design is:

- put a small, project-owned GPIO interface between the application and all
  platform libraries;
- retain historical backend sources only where they help comparison and
  debugging; do not carry them as supported build profiles;
- retain and improve the fake backend for development and protocol tests;
- use `libgpiod` on the supported modern Raspberry Pi OS;
- migrate the two standard clocked devices through separate kernel `spi-gpio`
  controllers in the sole hardware profile, preserving their current
  arbitrary GPIO wiring; use explicit `spidev` binding for the LPD8806 and the
  native IIO driver for the MCP3002; keep the nonstandard HT1632 transport
  separate.

Do not replace WiringPi calls with `libgpiod` calls throughout the existing
drivers. That would couple device protocols to another platform API and make
rollback difficult.

## Compatibility and rollback contract

The 2026-08-02 lean-build decision supersedes the earlier requirement to keep
a current-tree WiringPi build working:

1. Plain `make` and `make hardware` build the selected modern profile and do
   not link WiringPi. Backend and transport selection knobs are rejected.
2. The original Zero, Jessie card, WiringPi installation, service
   configuration, and known-good binary remain together as the physical
   rollback unit. Rollback does not require a current source rebuild.
3. No existing wire moves. The original unit requires no overlay; the
   separate Zero W uses only the explicit, reversible overlay gates.
4. Application command-line options, network defaults, HTTP behavior, MQTT
   behavior, and Broadcom GPIO numbering remain compatible unless a later gate
   explicitly authorizes a change.
5. `make sandbox`, `make test`, and `make check-arm-warnings` work without GPIO
   hardware or WiringPi.
6. Historical WiringPi, pure-libgpiod, and bit-banged transport sources may
   remain for comparison but are not supported, linked, or compile-tested
   hardware profiles.
7. Compilation no longer changes binary ownership or setuid mode. Final
   installation and service permissions require their own tested procedure.

See the [modern build policy](wiringpi-modern-build-policy.md). Changing the
source default does not authorize deployment; Phase 5 acceptance, Phase 6
installation, and physical rollback gates remain mandatory.

## Current hardware inventory

`wiringPiSetupGpio()` means every number below is a Broadcom GPIO number, not a
physical header-pin number. This table records the source as of this plan and
must become an executable pin-map test before backend work begins.

**That test now exists, split across two files** because the pins themselves
split during the migration. The seven still defined in application source —
the four matrix pins, both strip pins, and motion — are asserted by
[`tests/compatibility.sh`](../tests/compatibility.sh). The MCP3002's four moved
into the Device Tree when the ADC became a native IIO device and are asserted
against the merged overlay by [`tests/spi-overlay.sh`](../tests/spi-overlay.sh)
(`make test-spi-overlay`). Between them all eleven numbers below are checked,
so "no existing wire moves" fails on a laptop rather than on the clock.

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
also uses arbitrary GPIOs instead of the normal SPI0 pins. The fixed hardware
SPI controllers therefore require rewiring and are not selected. Kernel
`spi-gpio` controllers can use the existing arbitrary pins and expose their
transactions through `spidev`; this is the selected modern follow-up. Fixed
hardware SPI remains a fallback only if measured `spi-gpio` timing is still
insufficient.

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
git show FETCH_HEAD:misc/junk/wiringpi-migration/collectHardwareBaseline.sh >"${phase0_collector}"
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
5. Keep the then-current `make`, `make hardware`, binary names, link flags, pin
   values, locking, and service behavior unchanged.
6. Add a repository check that rejects direct WiringPi includes or calls
   outside the legacy backend.

At the end of this historical phase, the production executable still used WiringPi
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

At this historical phase checkpoint, the modern build was explicit:

```sh
make GPIO_BACKEND=gpiod hardware
```

That selector was retired after the native mixed-transport build passed. The
current tree uses plain `make` and rejects the old variable. It must still fail
clearly when the required API or GPIO chip is unavailable.

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

**Status: complete.** The whole-application trial passed all 13 checks with
zero failures on 2026-08-02 at commit `9677a0e`. Strip smoothness, timing, and
automatic dimming — the three observations that rejected the earlier
candidates — all passed, with timing rated better than production. See the
[trial result](wiringpi-phase5-application-trial-result.md).

The three items that blocked Phase 6 at that point — strip binding persistence,
CPU characterization, and a soak — were all closed on 2026-08-03. See
[Phase 6](#phase-6-opt-in-deployment-with-rollback) below.

The historical record of the failed candidates follows.

**Earlier status: pure and first mapped target trials failed.** Both 2026-08-02
exact-board runs passed functional display, strip, ADC response, motion, MQTT,
HTTP, and onboard-Wi-Fi checks, but failed automatic-dimming and
acceptable-timing observations. The mapped backend was clearly faster, but the
operator still found the strip especially slow. See the
[initial Phase 5 result](wiringpi-phase5-initial-result.md), the
[`gpiod-mmap` result](wiringpi-phase5-fast-result.md), and guarded
[hardware-trial handoff](wiringpi-phase5-hardware-trial.md). Phase 6 remains
blocked, neither modern candidate may be deployed, and the preserved
Zero/Jessie unit remains the production baseline.

The exact Zero W subsequently passed the restricted `/dev/gpiomem` target
probe. A separately named `GPIO_BACKEND=gpiod-mmap` experiment now retains
libgpiod line validation, configuration, ownership, and cleanup while moving
only high-rate values to the BCM2835 mapping. See the
[fast-value-path report](wiringpi-phase5-fast-backend.md). Its native ARMv6
build passed at commit `1f5605d`, but its first guarded physical trial failed
the dimming and timing gates. The user selected kernel `spi-gpio` for the
LPD8806 and MCP3002 on their existing pins, while retaining a narrow mmap
option only for the nonstandard HT1632 protocol. Exact kernel review then
selected `spidev` for the strip and the native MCP3002 IIO driver for the ADC.
The exact stopping state and next steps are preserved in the
[2026-08-02 resume handoff](wiringpi-resume-handoff-2026-08-02.md).

The selected follow-up is a mixed transport that preserves every existing
wire: libgpiod for motion, a kernel `spi-gpio`/`spidev` strip bus, a second
`spi-gpio` bus using the native MCP3002 IIO driver, and a narrowly scoped bulk
mmap path for the nonstandard HT1632 select protocol. The read-only
[kernel-SPI discovery](wiringpi-phase5-kernel-spi-discovery.md) established
exact kernel support, binding behavior, pin consumers, boot paths, and rollback
constraints; see its [accepted result](wiringpi-phase5-kernel-spi-result.md).

The repository now contains a disabled-by-default project
[overlay](wiringpi-phase5-spi-overlay.md). Its exact-board
[offline merge](wiringpi-phase5-spi-overlay-result.md) passed with zero
failures. The separately reversible [live boot](wiringpi-phase5-spi-live-boot.md)
also passed with zero failures and warnings; see the accepted
[live-boot result](wiringpi-phase5-spi-live-boot-result.md). The overlay is now
active on the experimental Zero W, the MCP3002 is bound to `mcp320x`, the
LPD8806 child is deliberately unbound, and `oclock.service` remains inactive.
The guarded, runtime-only
[LPD8806 spidev binding](wiringpi-phase5-lpd8806-binding.md) then passed with
eight checks and explicit rollback; see its accepted
[result](wiringpi-phase5-lpd8806-binding-result.md). The project-owned SPI
output boundary and deterministic frame tests are implemented, and the exact
Zero W [native build](wiringpi-phase5-lpd8806-build-result.md) passed seven
checks at commit `37b6797`; see the
[transport checkpoint](wiringpi-phase5-lpd8806-transport.md). The standalone
[guarded all-off transfer](wiringpi-phase5-lpd8806-first-transfer.md) is now
implemented. Its [first-transfer result](wiringpi-phase5-lpd8806-first-transfer-result.md)
records both the safe pre-payload `SPI_NO_CS` failure and the corrected mode-0
retry. The retry sent one 728-byte all-off frame, passed all 17 checks, and
restored the unbound state. Its 20,956-microsecond `show()` time exceeds the
12 ms application tick. The subsequent
[25-frame cadence result](wiringpi-phase5-lpd8806-cadence-result.md) rejected
the 1 MHz profile: all frames were valid and visually safe, but 0/25 met the
budget (20,473 microseconds median; 24,722 microseconds at the 95th
percentile). The full application must not run yet. A guarded 2 MHz all-off
experiment will test the running kernel's undelayed `spi-gpio` path without
rewiring or changing the live overlay. **That experiment passed**, production
moved to 2 MHz, and the full application has since run; see the
[2 MHz result](wiringpi-phase5-lpd8806-2mhz-result.md) and the
[trial result](wiringpi-phase5-application-trial-result.md). Its
[MCP3002/IIO path](wiringpi-phase5-mcp3002-iio.md) is now implemented without
requesting the overlay-owned ADC GPIOs. Its guarded
[exact-board first read](wiringpi-phase5-mcp3002-iio-result.md) passed 13 checks
and preserved all hardware/service state. The subsequent
[controlled light capture](wiringpi-phase5-mcp3002-calibration-result.md)
recorded a 997.3 uncovered mean, 179.0 fully covered mean, and 995.0 restored
mean. The 360/500 thresholds remained unchanged at that point, pending
representative room-light behavior during a later guarded application run.
**That run happened and changed them to a measured 460/700**: with the room
light actually off the sensor only falls to 355-478, so the original 360
low-water mark was unreachable. See
[the trial result](wiringpi-phase5-application-trial-result.md).

Run every follow-up modern transport profile on the Zero W and compare it with
Phase 0, Phase 1, and protocol-trace evidence from the preserved Zero/Jessie
unit:

- startup and shutdown pin levels, including visible glitches;
- HT1632 bit order, clock idle state, pulse widths, and full render time;
- LPD8806 bit order, latch sequence, full-strip frame time, and animation
  smoothness;
- MCP3002 IIO raw values on both channels across dark and bright conditions;
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

If the modern profile or onboard Wi-Fi cannot meet the acceptance budget,
reconnect the preserved Zero/Jessie unit. The selected follow-up moves the
LPD8806 and MCP3002 to kernel `spi-gpio` controllers without rewiring. The
strip uses an explicit `spidev` binding and the ADC uses the native IIO driver.
The 1 MHz path missed that timing budget, but the isolated 2 MHz `spi-gpio`
path passed on 2026-08-02 with a 3,001-microsecond median and 25 of 25 frames
inside the 12 ms tick. See the
[2 MHz result](wiringpi-phase5-lpd8806-2mhz-result.md). A fixed hardware-SPI
rewiring profile is therefore no longer the expected path for the strip. Do not
hide a timing failure by reducing refresh behavior.

### Phase 6: opt-in deployment with rollback

**Status: the blocking items are done; the waiting items are not.** As of
2026-08-03 the modern stack is installed, enabled, and starts itself at boot on
the Zero W/Trixie unit. What remains is elapsed time and a rollback rehearsal,
neither of which can be hurried.

Only after Phase 5 passes:

0. ~~**Make the strip `spidev` binding survive a reboot.**~~ **Done
   2026-08-03.** The application only opens `/dev/spidev4.0`; it never binds
   the device, and the reviewed `driver_override` binding was runtime-only. A
   deployed clock cannot depend on a human running a bind command after every
   power cut. Solved with a systemd oneshot unit ordered before
   `oclock.service`, not by giving the application privilege to bind its own
   device. A `udev` rule and a Device Tree change were both considered and
   rejected with reasons recorded. See
   [the binding persistence gate](wiringpi-phase6-binding-persistence.md).
1. **Done 2026-08-03.** The modern binary runs from
   `/home/pi/oclock.git/oclock` with the original application arguments, paths,
   and runtime defaults. The only service change is the added dependency on the
   binding unit.
2. **Done 2026-08-03.** NetworkManager reconnected onboard Wi-Fi after the
   reboot, and MQTT reconnected on its own. Note that `oclock.service` orders
   itself `After=network.target` rather than waiting for full connectivity;
   this has not caused a failure across the observed boots, but see the note
   below.
3. **Partially done.** The functional checklist was repeated after the
   unattended boot and an 8 h 53 min soak was observed; see
   [the overnight soak result](wiringpi-phase6-overnight-soak-result.md).
   "Several days" of monitoring has not happened and is not claimed.
4. **Not exercised.** Rollback remains the preserved Zero/Jessie unit, powered
   off and physically intact. Swapping it back has not been rehearsed since the
   modern unit took over.

The Zero W, Trixie, GCC 14, libgpiod, and onboard Wi-Fi are the approved target
change. Compilation no longer applies owner/setuid changes as a side effect,
but do not additionally combine deployment with service-user changes,
application-default changes, privilege hardening, or rewiring. Those may be
good follow-up projects, but they make failures harder to attribute and
rollback harder to trust.

### Phase 7: complete and document the modern deployment

After a sustained successful deployment:

- document the tested Pi image, kernel, `libgpiod`, and firmware versions;
- keep the modern profile as the only supported current-tree hardware build;
- archive the baseline binary, SD-card image, wiring record, and acceptance
  results;
- keep historical backends only as long as they remain useful diagnostic
  references; do not restore a compatibility build obligation.

“No longer using WiringPi” should mean the actively deployed modern image and
normal builds do not load or link it. Recovery remains the separately
preserved original hardware stack.

## PR acceptance checklist

Every migration PR should answer all of these:

- Does plain `make` select only the documented modern hardware profile?
- Do stale backend/transport selector variables fail clearly?
- Does the hardware binary avoid WiringPi?
- Can the sandbox and tests run without WiringPi?
- Are all GPIO numbers still Broadcom numbers with the same directions?
- Are initial output values and release behavior defined?
- Are errors actionable and free of silent fallback?
- Does the PR avoid unrelated service, network, privilege, and wiring changes?
- Is physical rollback possible with the preserved original unit?
- Were Incus checks run?
- If GPIO behavior changed, were Pi Zero hardware and timing checks run?

## Decision points that need hardware evidence

The following should remain open until measured:

- whether every device holds its timing under the real application tick and
  during sustained animation (the strip, ADC, and matrix each meet their
  budgets standalone, but none has been measured under combined load);
- whether the 2 MHz strip holds its timing under the real application tick
  alongside matrix and ADC work, and during sustained animation (single-frame
  cadence and colored correctness are both already accepted);
- whether native MCP3002/IIO values preserve useful light-sensor behavior;
- whether the LED strip and ADC should eventually be rewired for hardware SPI;
- whether pin ownership can be split per device without changing scheduling.

## References

- [Linux GPIO character-device API v2](https://www.kernel.org/doc/html/latest/userspace-api/gpio/chardev.html)
- [Linux GPIO character-device API v1 and deprecation status](https://www.kernel.org/doc/html/latest/userspace-api/gpio/chardev_v1.html)
- [libgpiod documentation](https://libgpiod.readthedocs.io/)
- [Linux SPI userspace API](https://www.kernel.org/doc/html/latest/spi/spidev.html)
- [Raspberry Pi GPIO and SPI pin mappings](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html)
