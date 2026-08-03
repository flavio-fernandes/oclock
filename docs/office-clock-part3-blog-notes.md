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

## Current editorial snapshot — 2026-08-03

The original office clock is still recoverable as a complete Raspberry Pi Zero
Rev 1.2/Jessie/WiringPi unit. A separate Raspberry Pi Zero W Rev 1.1 now runs
Raspberry Pi OS Lite 32-bit Trixie and is the selected modernization target.
It keeps the physical clock wiring while replacing the USB Wi-Fi dongle with
the Zero W's onboard radio.

The experimental Zero W also gains optional OpenSSH-over-Tailscale maintenance
access so the remaining hardware gates can be driven and collected directly.
This is development infrastructure, not an Office Clock dependency: normal
Wi-Fi, HTTP, MQTT, and service behavior must continue without Tailscale.

The application now has project-owned GPIO and SPI boundaries plus
deterministic fake-hardware tests. Historical WiringPi, pure-libgpiod, and
bit-banged implementations remain in the source for diagnosis, but the current
tree supports only the selected modern build. The first pure-libgpiod and
mapped hardware trials were functional but too slow, especially for the
240-pixel LPD8806 strip. Automatic dimming also did not pass, and the cause
turned out to be neither the transport nor the migration: the original 360 dark
threshold was unreachable on this unit, so dimming could never have engaged on
any candidate. It is now measured and retuned to 460/700, and passes.

The selected next architecture is mixed:

| Device | Selected modern transport | Existing BCM GPIOs | Status |
| --- | --- | --- | --- |
| Motion sensor | libgpiod v2 input | 10 | Implemented and functionally tested |
| LPD8806 strip | kernel `spi-gpio` plus explicit `spidev` binding | clock 20, data 21 | Accepted at 2 MHz on both timing (25/25 all-off frames inside 12 ms, 3.001 ms median) and colored correctness (uniform RGB, 4.424 ms worst frame); production speed promoted to 2 MHz |
| MCP3002 ADC | second `spi-gpio` plus native `mcp320x`/IIO | clock 17, MISO 27, MOSI 22, CS 4 | Native reads and controlled covered response accepted; thresholds retained pending room trial |
| HT1632 matrix | narrow bulk mmap burst path | CS 6, WR 13, data 19, select clock 26 | Implemented and accepted: the per-edge path was rejected at 19.2 ms (0/20 in budget), the burst path passed 20/20 at a 4.094 ms mean with visually identical output |

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

**Phase 5 is complete.** The whole-application trial passed all 13 checks with
zero failures on 2026-08-02.

**Phase 6's blocking items are also complete, as of 2026-08-03.** Both named
blockers closed. The strip `spidev` binding is now made at boot by a systemd
oneshot unit, proved by an actual reboot after which the clock came back with
nothing done by hand. The soak blocker closed with an 8 h 53 min unattended run
at zero restarts. The clock is currently keeping time on the modern stack and
starts itself.

What the article still must not say: that rollback has been tested, that a hard
power cut has been survived, that it has run for days or weeks, or that
WiringPi has been completely removed. The honest present-tense summary is that
the modern unit is running and self-starting, and that the original Zero/Jessie
unit remains preserved, powered off, and unrehearsed as a recovery path.

The checksum-pinned overlay has now also booted successfully on the exact
Zero W. Its two controllers claimed only the six reviewed GPIOs; the MCP3002
appeared through `mcp320x`/IIO and the strip child remained unbound. Runtime
bus identifiers were `spi3.0` and `spi4.0` on that boot, but installation and
application code must discover children by Device Tree path instead of
assuming those numbers. Onboard Wi-Fi returned after reboot, although this
single observation is not yet cold-boot or soak evidence.

The following binding-only trial discovered the strip by that Device Tree
path, created `/dev/spidev4.0` as a `root:spi` mode-`0660` character device,
and preserved the native ADC binding. The collector performed no device open
or transfer. Explicit unbind then removed the node and cleared the override,
returning the strip to its original unbound state.

The following native ARMv6 build at commit `37b6797` passed seven metadata
checks. Its preserved binary links libgpiod and libatomic without WiringPi,
discovers the strip by Device Tree suffix, and has SHA-256
`834191828a27ac801e0e959435e0397068b4b9b2335455b4128d29256dbd8807`.
It was not executed. After that gate, the project intentionally removed its
backend-selection knobs: plain `make` now means the selected modern profile,
and the original physical unit—not a current-tree WiringPi build—is rollback.

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
6. Backward compatibility means preserving the complete known-good physical
   unit while allowing the current source and deployment to stop carrying
   WiringPi as a supported build dependency.

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
| Clone WiringPi from `git.drogon.net`; run `./build` | Not needed for the modern build | Keep it only on the preserved Jessie rollback unit; the current tree no longer supports a WiringPi target |
| Install only `git libevent-dev` | Insufficient now | Install modern compiler/build, MQTT, GPIO, and overlay dependencies listed below |
| Clone branch `rpi-0.1.y` | Historical reproducibility branch | Link the merged modernization release/tag after PR 3 and deployment are complete |
| Plain `make` | Now selects the modern Zero W profile | Build with `make` or `make hardware`; transport selector variables are retired |
| Copy unit to `/lib/systemd/system` | Works historically, final path TBD | Prefer the packaged/reviewed unit and `systemctl`; record exact install path used on Trixie |
| Root/setuid executable | No longer a compiler side effect | The Makefile does not chown or setuid; define and test final service identity/device permissions separately |
| Software-bit-bang every peripheral | Too expensive through pure libgpiod | Use subsystem-specific transports: libgpiod, kernel SPI, and a narrow matrix bulk path |
| Repeated manual command/result relay | Replaced for development | Optional OpenSSH over Tailscale with a dedicated source-restricted key; not required by the application |

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
- `linux-libc-dev` provides the Linux SPI userspace header for
  `SPI_IOC_MESSAGE` on the accepted image;
- a project-owned overlay file installed under the authoritative Trixie boot
  overlay directory;
- a reviewed boot configuration line enabling that overlay;
- device-node ownership/permissions appropriate for the unchanged service
  privilege model.

The checksum-pinned overlay install and guarded `config.txt` edit have passed
on the Zero W. The next helper may load and bind `spidev` only for a
runtime-only, no-transfer gate; do not publish those commands as final install
instructions until the transport and deployment procedure pass.

### Build commands and profiles

These commands describe the current tree:

```sh
# Selected modern Zero W hardware build.
make
make hardware

# Hardware-free development and tests.
make sandbox
make test

# Opt-in, not part of make test.
make valgrind
make test-spi-overlay
```

`GPIO_BACKEND` and `STRIP_TRANSPORT` are retired; supplying either is an error.
The WiringPi and slow experimental commands remain valid only when checking out
the historical commits that introduced them. The ADC and matrix conversions are
now complete and the whole application has run against the live overlay, but
these are still build commands, not an install procedure — see the service and
overlay sections below for that.

The full target reference lives in
[`docs/development.md`](development.md#make-targets). Do not reproduce the
table in the article; link it. What belongs in the prose is the handful of
targets that are interesting *because of this migration*:

- **`gpio-boundary`** is the guard that keeps the whole premise honest. It is a
  `grep` over the production sources for direct WiringPi calls outside the one
  legacy backend file. The claim "the application no longer talks to WiringPi"
  is enforced by a test rather than by discipline, which is the difference
  between a migration and an intention.
- **`check-arm-warnings`** compiles everything with `-funsigned-char` and
  warnings as errors. Plain `char` is signed on x86-64 and unsigned on ARM, so
  a laptop will happily compile a signedness bug that only misbehaves on the
  clock. This target is how an x86-64 dev box stays honest about an ARMv6
  target, and it is a genuinely transferable tip for readers cross-developing
  for a Pi.
- **`test-spi-overlay`** pins all six BCM GPIO numbers in the merged Device
  Tree. That is the no-rewiring contract — the promise that not one wire moves
  — encoded as an assertion. If someone tidies a pin number, it fails on a
  laptop instead of on a clock that no longer lights up.
- **`valgrind`** is worth a mention precisely because it is *not* in
  `make test`. The six C++ test binaries already run under AddressSanitizer and
  UBSan, but those cover units in isolation. `make valgrind` runs the **whole
  application** — every thread, the event loop, the MQTT client, and the
  shutdown path, together — under `--leak-check=full` with
  `--errors-for-leak-kinds=definite,possible`. That combination is where the
  interesting leaks actually live. The good part, measured rather than assumed:
  it finishes in **about 3.5 seconds**. It sits outside `make test` only
  because it needs `valgrind` installed, not because it is expensive — which
  makes "I'll run it later" a much weaker excuse than it sounds.
- **`test-strip-binding`** is the newest and makes a point worth stating
  plainly: it found a real defect that the reboot could not. Writing tests for
  a fourteen-line shell helper sounds like ceremony until one of them catches
  a race that would have produced an intermittently dead clock months later.

There is a smaller lesson in `compatibility` that may be worth a sentence.
Four of its assertions inspected build output via `make -n`, and `make -n`
prints nothing when the artifact is already built — so those assertions matched
nothing and passed vacuously. They only ever worked because that target ran
first in a clean tree. Running the suite twice in a row exposed it. A test that
passes for the wrong reason is worse than no test, and the way this one was
found — by accident, while checking whether *different* assertions could fail —
is the honest version of how such things usually surface.

### Boot overlay procedure — live enable and inspection passed

The final procedure needs all of the following, in this order:

1. Show how to inspect current GPIO consumers and SPI devices.
2. Stop the application before any overlay claims its existing GPIOs.
3. Install the reviewed project overlay defining two independent `spi-gpio`
   controllers on the unchanged strip and ADC pins.
4. Explain the project-compatible plus explicit `spidev` override for the
   LPD8806 and native `microchip,mcp3002`/IIO binding for the ADC.
5. Add one clearly marked boot configuration entry; retain a byte-for-byte
   backup of the original configuration.
6. Reboot and verify controllers, native ADC/IIO binding, pin consumers, and
   permission state before any application or userspace strip binding.
7. Provide disable and uninstall commands, including how to recover by
   editing the SD card from another machine if boot or GPIO ownership fails.
8. State that current libgpiod GPIO drivers must not request lines owned by the
   new SPI controllers.

The LPD8806 has no chip-select wire. The overlay and userspace transfer use the
kernel's no-CS behavior without inventing a wiring change. The MCP3002 keeps
CS on BCM 4, while its full-duplex protocol is performed by the native kernel
driver and exposed as two IIO raw channels.

The live gate installed `/boot/firmware/overlays/oclock-spi.dtbo`, added one
managed `dtoverlay=oclock-spi` line, and retained a timestamped byte-identical
boot-config backup. Normal disablement and eventual uninstall still need to be
exercised. The LPD8806 `spidev` override is intentionally runtime-only; a
reboot clears it without removing the boot overlay.

### Service procedure — SPI ordering now settled, Wi-Fi ordering still open

The original unit used:

```text
WorkingDirectory=/home/pi/oclock.git
ExecStart=/home/pi/oclock.git/oclock
Restart=on-failure
After=network.target
```

The verified modern unit adds a dependency on the binding service, and nothing
else:

```text
Requires=oclock-strip-spi.service
After=oclock-strip-spi.service
```

`Requires=` rather than `Wants=` is the whole point and is worth a sentence in
the article. Without the binding there is no `/dev/spidev*`, the clock cannot
initialize its SPI output, and `Restart=on-failure` turns that into a crash
loop. Refusing to start is the better failure, and it was demonstrated by
hiding the binder and confirming the clock stayed inactive with `NRestarts=0`.

The companion unit is
[`misc/oclock-strip-spi.service`](../misc/oclock-strip-spi.service), a
`Type=oneshot` with `RemainAfterExit=yes` because the binding is state rather
than a process, and an `ExecStop` that reverses it.

The existing service runs as root because no `User=` or `Group=` is set. The
legacy executable is root-owned and owner-setuid. The current Makefile no
longer applies ownership or setuid changes during compilation, but PR 3 has not
yet selected a new service identity or completed privilege hardening.

Before the final post, decide and verify whether the Trixie service needs:

- `network-online.target` rather than only `network.target`. Still open, and
  now with a measurement behind it: on the verified boot MQTT needed two
  connect attempts before succeeding, which is consistent with starting before
  connectivity was usable. It recovers on its own, so this is a tidiness
  question rather than a defect;
- ~~ordering after the SPI device nodes exist~~ — **settled.** See above;
- any explicit supplementary group or udev rule. A udev rule was evaluated for
  the binding and rejected; the reasons are in
  [the binding persistence gate](wiringpi-phase6-binding-persistence.md);
- a modern unit installation directory such as `/etc/systemd/system` rather
  than reproducing the old `/lib/systemd/system` copy command. Both verified
  units currently live in `/usr/lib/systemd/system` to match where
  `oclock.service` already was, rather than introducing a second shadowing
  copy. Worth revisiting for the published instructions.

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

Tailscale is not in this “no longer needed” list. It is an optional maintenance
tool on the experimental unit, not part of the application dependency set.

Do not say that every historical WiringPi source file disappeared from the
repository. Those sources remain diagnostic references, while the original
binary, Jessie SD card, and original Pi are the intentional recovery assets.

## Trimming unneeded services — pending, deferred to Phase 7

**Status: pending.** Not yet applied or measured. Do not present the commands
below as tested until a gate records before/after evidence.

The 2016 setup trimmed background services on Jessie with:

```sh
sudo systemctl disable avahi-daemon && \
sudo systemctl stop avahi-daemon && \
sudo apt-get remove -y bluez bluez-firmware pi-bluetooth triggerhappy && \
echo ok
```

That recipe is partly obsolete. A survey of the actual Trixie target on
2026-08-02 found:

| Item | State on the Zero W/Trixie target |
| --- | --- |
| `avahi-daemon.service` | enabled and active |
| `avahi-daemon.socket` | enabled and active — the Jessie command misses this |
| `bluetooth.service` | enabled and active |
| `bluez`, `bluez-firmware` | installed |
| `pi-bluetooth` | **not installed** |
| `triggerhappy` | **not installed** |
| `hciuart.service` | **not present on this image** |
| `ModemManager`, `cups` | not present |

So half the original command list no longer applies, and the part that does
applies differently.

### The modern equivalent

```sh
# avahi: disable the socket too, or socket activation restarts the service.
sudo systemctl disable --now avahi-daemon.service avahi-daemon.socket
sudo systemctl mask avahi-daemon.service avahi-daemon.socket

# Bluetooth: stop the stack, then remove the packages that remain.
sudo systemctl disable --now bluetooth.service
sudo apt purge -y bluez bluez-firmware
sudo apt autoremove -y
```

`systemctl disable --now` replaces the old separate disable-then-stop pair.
Masking avahi is what actually prevents it coming back through socket
activation or a dependency.

Optionally, disable the Bluetooth radio at the hardware level in
`/boot/firmware/config.txt`:

```text
dtoverlay=disable-bt
```

Note what that overlay actually does: it returns the PL011 UART to GPIO14/15.
The Office Clock uses GPIOs 4, 6, 10, 13, 17, 19, 20, 21, 22, 26, and 27, so
there is no conflict — but it is a **boot-file change**, and this project gates
those separately with a backup and a documented rollback. Treat it as its own
step, not as part of an `apt` cleanup.

### What must not be trimmed

This is the important half, and the reason the naive "disable everything"
advice is dangerous on this particular device:

- **`systemd-timesyncd`.** This is a *clock*. Wrong time is the most visible
  possible failure, and it would look like an application bug rather than a
  trimming mistake. Currently active and synchronized; leave it alone.
- **NetworkManager and the Wi-Fi stack.** Onboard Wi-Fi replacing the USB
  dongle is a migration requirement, and Trixie manages it through
  NetworkManager.
- **Bluetooth on a Zero W is not free of Wi-Fi.** Both are functions of the
  same BCM43438. Disabling Bluetooth does not disable Wi-Fi, but any claim
  about the radio should be verified rather than assumed.
- **avahi has a cost.** Disabling it removes mDNS, so `oclock.local` stops
  resolving. That was acceptable in 2016; confirm it is still acceptable before
  repeating it, because it changes how the clock is reached on the LAN.

### Why this is deferred

The migration plan is explicit that deployment must not be combined with
unrelated service, privilege, or configuration changes, because it makes
failures harder to attribute and rollback harder to trust. Service trimming is
exactly such a change. It belongs in Phase 7, after a sustained successful
deployment, as its own reviewed step.

When it is done, measure it: record CPU, memory, and boot time before and
after. The 2016 article asserted the benefit; the follow-up can show it.

## What the final modern installation will need

This is the publication checklist. Replace each pending item with the exact
tested command or file before drafting the article:

- [x] Separate Zero W instead of upgrading the original unit.
- [x] Raspberry Pi OS Lite 32-bit Trixie on ARMv6/armhf.
- [x] Onboard Wi-Fi connected without the USB dongle.
- [x] GCC 14, libevent, libmosquitto, libgpiod v2, and libatomic-capable build.
- [x] Project-owned GPIO API and single modern hardware build.
- [x] Deterministic fake protocol tests and Incus validation.
- [x] `raspi-utils-dt` identified as the installed provider of `dtoverlay`.
- [x] Disabled-by-default Office Clock Device Tree overlay added to the repo.
- [x] Offline target merge of that overlay accepted on the exact Zero W.
- [x] Checksum-pinned install, enable, inspect, disable, and SD-card rescue
  commands written.
- [x] Live overlay boot exercised; native ADC binding, GPIO ownership, and
  onboard-Wi-Fi return verified.
- [x] Runtime-only LPD8806 `spidev` binding and explicit unbind exercised
  without a transfer.
- [ ] Normal-disable procedure exercised.
- [ ] Final overlay uninstall procedure exercised after the live gates.
- [x] Project-owned SPI userspace transport plus deterministic fake.
- [x] LPD8806 conversion preserving 720 GRB bytes and eight latch bytes
  in one hardware-free verified transfer.
- [x] Native Zero W build of the strip profile, with dependency evidence and
  no WiringPi.
- [x] Standalone 728-byte all-off tool and guarded bind/transfer/unbind
  verifier implemented.
- [x] Corrected mode-0 all-off frame transferred on the Zero W with clean
  rollback and no visible flash.
- [x] LPD8806 Zero W timing acceptance at the existing 12 ms application tick:
  the 1 MHz profile failed 0/25, and the 2 MHz kernel free-run profile passed
  25/25 with a 3.001 ms median. Production speed promoted to 2 MHz.
- [x] LPD8806 colored-frame correctness at 2 MHz, confirming GRB byte order and
  signal integrity on the existing arbitrary-pin wiring.
- [x] MCP3002 native-IIO application conversion with dynamic Device Tree
  discovery and deterministic fixture tests.
- [x] MCP3002 exact-board raw channel verification.
- [x] Controlled uncovered/covered/restored samples captured with both channels
  separate.
- [x] Dimming thresholds retuned against measured room darkness and verified
  live: 460 low-water and 700 high-water, replacing an unreachable 360/500.
- [x] HT1632 per-edge path measured and rejected at 19.2 ms against the 12 ms
  tick, which is what justified building the bulk transport rather than
  assuming it.
- [x] HT1632 burst transport implemented and accepted at a 4.094 ms mean,
  20/20 renders in budget, with edge-order equivalence tests against the
  ordinary write path.
- [x] Whole-application trial on the exact Zero W: 13 of 13 checks, zero
  failures, timing rated better than production.
- [x] Backend/transport build knobs retired; `make` and `make hardware` select
  the modern profile.
- [x] **Strip `spidev` binding persistence across reboot.** Solved with
  `oclock-strip-spi.service` and verified by an actual reboot on 2026-08-03:
  the clock came back with the binding established and the service running,
  with nothing done by hand. Includes a negative test proving a failed binding
  refuses to start the clock rather than crash-looping, and 14 offline checks
  covering the refusal paths a single reboot cannot reach.
- [x] Reboot NetworkManager, HTTP, MQTT, and service-order validation. Note the
  caveat: this was a clean reboot, not a cold boot from power-off, and MQTT
  needed two connect attempts.
- [x] Overnight soak with CPU, latency, throttling, and Wi-Fi evidence:
  8 h 53 min, zero restarts, zero warnings, zero Wi-Fi drops. Carried ordinary
  load; the recorded stress recipe still applies to a future loaded soak, and
  the stressed numbers remain the Phase 5 run-1 measurements.
- [ ] Hard power-cut recovery, as distinct from a clean reboot.
- [ ] Sustained multi-day observation.
- [ ] Physical rollback exercise using the preserved Zero/Jessie unit.
- [ ] Service trimming (Phase 7) with before/after measurements. The before
  measurement now exists: a 2 min 24.8 s boot, `NetworkManager` 59.6 s,
  `cloud-init` 21.6 s.
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

### Phase 5 — exact-board experiments, complete

**Outcome: passed, 13 of 13 checks, zero failures, at commit `9677a0e`.** The
accepted architecture is libgpiod for motion, kernel `spi-gpio` plus `spidev`
at 2 MHz for the strip, native `mcp320x`/IIO for the ADC, and a narrow burst
value path for the matrix. Strip smoothness, timing, and dimming — the three
observations that rejected both earlier candidates — all passed, with timing
rated better than the original production clock.

The failed experiments below are kept deliberately: they are what produced the
final design.

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
  override. The project overlay compiled and merged against the exact active
  Zero W Device Tree, then passed a live reboot with ten checks and no
  failures or warnings. No device has been opened or transferred through.
- Runtime-only LPD8806 binding passed eight checks. The expected `root:spi`
  node appeared, the ADC binding was unchanged, no transfer occurred, and
  explicit rollback restored the unbound state.
- A project-owned SPI output, deterministic fake, dynamic Device Tree
  discovery, and LPD8806 path now preserve the exact 720 GRB plus eight latch
  bytes in one transfer. Incus tests and an x86 Trixie build pass. The native
  ARMv6 build also passed at commit `37b6797`; the later guarded first-transfer
  gate supplied the first live payload evidence.
- Current-tree build policy now supports only the selected modern profile.
  The old selector knobs and WiringPi compile check are gone; historical
  implementations remain only for comparison, and physical rollback remains
  the complete original unit.
- A standalone first-transfer tool and guarded verifier reuse the exact
  application frame assembly, send one all-off frame under an explicit prompt
  and timeout, and unbind before the operator answers. The first exact-board
  attempt safely stopped before payload because `spi-gpio` rejected the
  optional `SPI_NO_CS` mode bit. The overlay already has zero chip selects, so
  the corrected transport requests ordinary mode 0. Its retry passed all 17
  checks, transferred 728 bytes at 1 MHz, stayed visually dark, and restored
  the unbound state. The measured `show()` call was 20.956 ms, so this is
  functional acceptance rather than final cadence acceptance.
- The 1 MHz cadence run then failed outright: 0/25 frames met 12 ms. Raising
  the request to 2 MHz fixed it completely. The same helper, the same wiring,
  the same overlay, one constant changed: median `show()` fell from 20.473 ms
  to 3.001 ms and 25/25 frames met the budget. This is the strongest single
  narrative beat in the whole migration and it belongs in the article. The
  cause is a hard threshold in the kernel driver rather than anything
  electrical: `spi-gpio` inserts a rounded-up delay on both sides of every
  clock edge when the requested half-cycle is 500 ns or longer, which is
  exactly 1 MHz. Asking for *more* speed made the driver stop waiting.
- Caveat to keep honest in the write-up: every frame measured so far was
  all-off. The colored-frame gate, which is the first real test of signal
  integrity at 2 MHz on arbitrary GPIO pins, has not run yet.

### Phase 6 — blocking items complete 2026-08-03

Both blockers recorded here closed on 2026-08-03, and the modern stack now
starts itself on the Zero W.

- ~~**Blocker: the strip binding does not survive a reboot.**~~ **Closed.**
  Solved with `oclock-strip-spi.service`, a systemd oneshot ordered before
  `oclock.service`, with `Requires=` so a failed binding stops the clock from
  starting rather than feeding a crash loop. A udev rule and a Device Tree
  change were both considered and rejected with reasons worth retelling. Never
  solved by giving the application privilege to bind its own device. See
  [the binding persistence gate](wiringpi-phase6-binding-persistence.md).
- ~~**Blocker: no soak.**~~ **Closed.** 8 h 53 min unattended, zero restarts,
  zero warnings, zero Wi-Fi drops. See
  [the overnight soak result](wiringpi-phase6-overnight-soak-result.md).
- The clock now boots unattended, binds its own strip device, and starts
  without a human. Verified by an actual reboot, with the operator confirming
  display, strip, and motion afterwards.

Still open, and none of it can be hurried:

- No physical rollback exercise has been performed since the migration
  completed. The Zero/Jessie unit is preserved and powered off.
- A hard power cut has not been tested; only a clean reboot.
- Sustained multi-day observation has not happened.
- Service trimming is deferred to Phase 7; see the section above. The reboot
  produced a concrete before measurement to trim against: a 2 min 24.8 s boot
  of which `NetworkManager` is 59.6 s and `cloud-init` is 21.6 s, against
  4.96 s for the new binding unit.

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
pins do not match those interfaces, so hardware SPI would mean rewiring. It
stayed a fallback and was never needed.

**The best technical surprise in the project lives here.** At 1 MHz the strip
missed the 12 ms tick on 25 of 25 frames, with a 20.5 ms median. At 2 MHz the
same code on the same wires took 3.0 ms. The cause is a hard threshold in the
kernel driver, not anything electrical: `spi-gpio` calls a nanosecond delay
helper on both sides of every clock edge whenever the requested half-cycle is
at least 500 ns — which is exactly 1 MHz or slower — and that helper is often a
rounded-up microsecond delay. Requesting *more* speed made the driver stop
waiting and free-run.

Worth being precise for readers: kernel `spi-gpio` is still bit-banging. It is
faster and better scheduled than doing it from userspace, but it is CPU-bound
rather than offloaded to a hardware engine with DMA. Fixing the latency did not
make the work free, which is why CPU under load is worth reporting honestly
alongside the frame times.

### Why the HT1632 remains special

The matrix has an extra panel-select shift chain plus command/data fields of
3, 7, and 4 bits. That does not map cleanly to ordinary byte-oriented spidev
transactions. Keeping one narrowly bounded bulk implementation is clearer
than forcing unlike protocols through an artificial common abstraction.

What that implementation turned out to be: the four matrix pins are resolved to
register masks once, and the bit loops then store straight to the BCM value
registers, skipping the per-edge mutex, line lookup, validation, and virtual
dispatch. The memory barrier moves from every store to the burst boundary,
which is legitimate because the BCM2835 orders accesses within a single
peripheral. Measured effect: 19.2 ms down to 4.1 ms for a full rewrite.

Two details worth telling readers, because both are the kind of thing that
bites quietly:

- The HT1632 needs at least 50 ns between a data change and the WR rising edge.
  The old path satisfied that **by accident**, because every write cost
  microseconds. Going roughly a hundred times faster meant the guarantee had to
  become explicit and tunable. A speedup can invalidate an assumption that was
  only ever true incidentally.
- The burst path bypasses the ordinary write call, so no existing test would
  have noticed a mask assigned to the wrong pin. It would compile, pass the
  whole suite, and silently drive the wrong wire. The equivalence test that
  compares both paths edge for edge is the real deliverable of that change.

### Why the ADC threshold is a separate question — and what the answer was

The ADC can return changing values while the display still never dims. That
separation mattered: the transport was proven first, by reading both MCP3002
IIO raw channels and recording controlled covered and uncovered values, and
only then was the threshold questioned.

The answer was that **the old threshold simply did not fit**, and it never had.
With the real room light switched off, the reported value settles between 355
and 478 depending on conditions — always above the original 360 low-water mark.
Dimming could not have engaged on any candidate, including the Phase 0
baseline on the original clock. A decade-old constant had been quietly wrong,
and only a migration that forced someone to stand and watch the hardware
surfaced it.

This is the strongest argument in the whole project for separating transport
correctness from calibration. Had they been debugged together, the obvious and
wrong conclusion would have been that the new transport broke dimming.

## Proposed article outline

1. **A ten-year-old clock still ticking**
   - Link Part 1 and Part 2.
   - Show the original Zero/Jessie/WiringPi unit and explain why it remains
     valuable as rollback.
2. **What aged and what did not**
   - Same panels, strip, ADC, PIR, power, wiring, HTTP modes, and animations.
   - Obsolete OS/toolchain/GPIO library and unnecessary Wi-Fi dongle.
3. **The compatibility contract**
   - Separate Zero W, no wiring change, complete physical rollback retained,
     measurements before deployment.
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
9. **The bug that was never in the migration**
   - The dimming story: three runs, a wrong first theory about averaging, and
     the room light switch that proved a decade-old threshold had always been
     unreachable. Include the harness false pass, where the automated check
     disagreed with the human and the human was right.
10. **Measurements and final acceptance**
    - Strip frame time, matrix timing, CPU under animation stress versus at
      rest, HTTP latency, light values, Wi-Fi, soak, and rollback.
11. **Lessons from keeping old hardware alive**
    - Modernization is not library substitution; preserve working baselines;
      failed measurements are design input; measure before building the thing
      you assumed you would need; and a speedup can break a constraint that was
      only ever satisfied by accident.

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
- The light-sensor trace with the **room light** switched off and back on,
  annotated with the 460/700 thresholds. This is better evidence than a covered
  sensor, and it is the plot that shows why 360 was unreachable.
- Before/after matrix render timing: 19.2 ms per-edge versus 4.1 ms burst,
  against the 12 ms tick line.
- Before/after strip frame timing: 20.5 ms at 1 MHz versus 3.0 ms at 2 MHz,
  showing the kernel delay-threshold cliff.
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
- “Rollback has been tested.” — not since the migration completed. The
  Zero/Jessie unit is preserved and powered off, but swapping it back has not
  been rehearsed.
- “These are the final installation commands.”
- “It has run for a week without problems.”

Four claims previously on this list are now supported by evidence and may be
written carefully:

- **Dimming.** It is accurate to say the dimming behavior now works and that
  the root cause was an unreachable threshold rather than the migration. Be
  precise: the constant was wrong before the migration too.
- **Speed.** It is accurate to say the strip meets the 12 ms application tick
  with better than 2x margin at 2 MHz, and that the operator rated response
  times better than production. It is **not** accurate to say kernel software
  SPI is as fast as the original WiringPi implementation in general; the honest
  comparison is per-frame budget and observed behavior, not raw bit rate.
- **Unattended boot.** It is accurate to say the clock starts itself, binds its
  own strip device, and needs no human after a reboot. Proved on 2026-08-03;
  see [the binding persistence gate](wiringpi-phase6-binding-persistence.md).
  Be precise about scope: a **clean reboot** was tested, not a hard power cut.
  A power cut should be strictly easier, because boot always begins from an
  unbound child, but it has not been done. Write “it comes back on its own
  after a reboot,” not “it survives a power cut.”
- **A day of uptime.** It is accurate to say the clock ran unattended for
  8 h 53 min with zero restarts, zero warnings, and no Wi-Fi drop; see
  [the overnight soak result](wiringpi-phase6-overnight-soak-result.md). Nine
  hours is not a day and is certainly not a week — say the number.

Note also that “The service reliably starts after Wi-Fi and SPI are ready” is
now **half** true and worth splitting. SPI is guaranteed:
`Requires=oclock-strip-spi.service` means the clock cannot start before its
strip device exists. Wi-Fi is not: `oclock.service` orders itself
`After=network.target`, which does not wait for actual connectivity, and MQTT
took two connect attempts on the verified boot before succeeding. It recovers
on its own, so this is not a defect — but do not claim ordering that is not
there.

The safe phrasing before final deployment is: “The selected modern profile no
longer links WiringPi; the repository and original recovery unit retain it for
backward compatibility.”

## Source map for the future author

- Overall plan: [`wiringpi-migration.md`](wiringpi-migration.md)
- **Make target reference** (what every build and test target does, and which are opt-in): [`development.md`](development.md#make-targets)
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
- Live SPI boot result: [`wiringpi-phase5-spi-live-boot-result.md`](wiringpi-phase5-spi-live-boot-result.md)
- Guarded LPD8806 binding: [`wiringpi-phase5-lpd8806-binding.md`](wiringpi-phase5-lpd8806-binding.md)
- LPD8806 binding result: [`wiringpi-phase5-lpd8806-binding-result.md`](wiringpi-phase5-lpd8806-binding-result.md)
- LPD8806 SPI transport: [`wiringpi-phase5-lpd8806-transport.md`](wiringpi-phase5-lpd8806-transport.md)
- LPD8806 first-transfer result: [`wiringpi-phase5-lpd8806-first-transfer-result.md`](wiringpi-phase5-lpd8806-first-transfer-result.md)
- LPD8806 cadence result: [`wiringpi-phase5-lpd8806-cadence-result.md`](wiringpi-phase5-lpd8806-cadence-result.md)
- Guarded LPD8806 2 MHz experiment: [`wiringpi-phase5-lpd8806-2mhz-experiment.md`](wiringpi-phase5-lpd8806-2mhz-experiment.md)
- **LPD8806 2 MHz accepted result**: [`wiringpi-phase5-lpd8806-2mhz-result.md`](wiringpi-phase5-lpd8806-2mhz-result.md)
- Guarded LPD8806 colored gate: [`wiringpi-phase5-lpd8806-colors.md`](wiringpi-phase5-lpd8806-colors.md)
- **LPD8806 colored result**: [`wiringpi-phase5-lpd8806-colors-result.md`](wiringpi-phase5-lpd8806-colors-result.md)
- Guarded HT1632 render gate: [`wiringpi-phase5-ht1632-render.md`](wiringpi-phase5-ht1632-render.md)
- HT1632 per-edge rejection: [`wiringpi-phase5-ht1632-render-result.md`](wiringpi-phase5-ht1632-render-result.md)
- **HT1632 burst transport accepted**: [`wiringpi-phase5-ht1632-burst-result.md`](wiringpi-phase5-ht1632-burst-result.md)
- Whole-application trial gate: [`wiringpi-phase5-application-trial.md`](wiringpi-phase5-application-trial.md)
- **Whole-application trial result (Phase 5 complete)**: [`wiringpi-phase5-application-trial-result.md`](wiringpi-phase5-application-trial-result.md)
- **Overnight soak result**: [`wiringpi-phase6-overnight-soak-result.md`](wiringpi-phase6-overnight-soak-result.md)
- **Binding persistence gate and result (unattended boot)**: [`wiringpi-phase6-binding-persistence.md`](wiringpi-phase6-binding-persistence.md)
- Boot binder: [`misc/bindOclockStripSpi.sh`](../misc/bindOclockStripSpi.sh)
- Boot binder unit: [`misc/oclock-strip-spi.service`](../misc/oclock-strip-spi.service)
- Clock service unit: [`misc/oclock.service`](../misc/oclock.service)
- Binder offline tests: [`tests/strip-binding.sh`](../tests/strip-binding.sh)
- **Retired gate tooling and what each file proved**: [`misc/junk/wiringpi-migration/CATALOG.md`](../misc/junk/wiringpi-migration/CATALOG.md)
- Remote maintenance access: [`oclock-remote-access.md`](oclock-remote-access.md)
- Modern build policy: [`wiringpi-modern-build-policy.md`](wiringpi-modern-build-policy.md)
- Target selection rationale: [`wiringpi-phase3-target-selection.md`](wiringpi-phase3-target-selection.md)
- Mapped fast-value backend design: [`wiringpi-phase5-fast-backend.md`](wiringpi-phase5-fast-backend.md)
- Guarded hardware-trial handoff: [`wiringpi-phase5-hardware-trial.md`](wiringpi-phase5-hardware-trial.md)
- LPD8806 native build result: [`wiringpi-phase5-lpd8806-build-result.md`](wiringpi-phase5-lpd8806-build-result.md)
- LPD8806 first-transfer gate: [`wiringpi-phase5-lpd8806-first-transfer.md`](wiringpi-phase5-lpd8806-first-transfer.md)
- MCP3002 native IIO: [`wiringpi-phase5-mcp3002-iio.md`](wiringpi-phase5-mcp3002-iio.md)
- MCP3002 first-read result: [`wiringpi-phase5-mcp3002-iio-result.md`](wiringpi-phase5-mcp3002-iio-result.md)
- MCP3002 controlled light result: [`wiringpi-phase5-mcp3002-calibration-result.md`](wiringpi-phase5-mcp3002-calibration-result.md)
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
- **2026-08-02:** Accepted the checksum-pinned live overlay boot with ten
  checks, zero failures, and zero warnings. Recorded the dynamic SPI child
  identities, native MCP3002/IIO binding, exact GPIO consumers, Wi-Fi return,
  active managed boot entry, and retained backup. Added a runtime-only,
  Device-Tree-discovered LPD8806 `spidev` binding gate that performs no device
  open or transfer. Normal disablement and all SPI data operations remain
  pending.
- **2026-08-02:** Accepted the runtime-only LPD8806 binding gate with eight
  checks and zero failures. Recorded the transient `root:spi` mode-`0660`
  device, unchanged MCP3002 binding, metadata-only safety boundary, and
  successful explicit unbind. Hardware-free transport/frame implementation
  was the next gate; this binding-only result itself transferred no bytes.
- **2026-08-02:** Added the project SPI output interface, deterministic fake,
  Device-Tree-discovered Linux spidev implementation, explicit build selector,
  and opt-in LPD8806 integration. Tests prove one exact 728-byte frame and no
  strip GPIO operations while retaining the legacy constructor and default.
  This checkpoint preceded the native ARM build and live transfer gates.
- **2026-08-02:** Accepted the corrected mode-0 first transfer with 17 checks,
  zero failures, and zero warnings. The exact 728-byte all-off frame completed
  at 1 MHz in 20.956 ms, the strip did not flash, the transient binding was
  removed, the ADC stayed on `mcp320x`, and the service stayed inactive. This
  proves the live payload and rollback paths while leaving the 12 ms cadence
  target open.
- **2026-08-02:** Rejected the 1 MHz LPD8806 profile on repeated cadence. All
  25 application-path all-off frames were valid and visually stable, but 0/25
  met 12 ms: median was 20.473 ms and the 95th percentile was 24.722 ms. The
  exact Raspberry Pi kernel source places 1 MHz on the delayed side of a
  500-ns half-cycle threshold, so the next isolated test requests 2 MHz to
  exercise the undelayed path without rewiring or changing the live overlay.
- **2026-08-02:** Replaced the supported application's GPIO-bit-banged MCP3002
  path with native `mcp320x`/IIO reads. The implementation discovers the IIO
  device by Device Tree identity, validates both single-ended raw attributes,
  and rejects malformed or out-of-range values. Exact-board reads and light
  calibration remain separate pending gates.
- **2026-08-02:** Accepted the MCP3002 native-IIO first read with 13 checks,
  zero failures, and zero warnings. Channel 0 returned 1013, channel 1 returned
  1016, and the pair took 6.656 ms. The ADC stayed on `mcp320x`, the strip
  stayed unbound, and the service stayed inactive. These high ambient values
  explain why the existing thresholds did not engage in that condition, but
  they do not authorize recalibration without controlled covered samples.
- **Build-note discipline:** Record a shell wall-clock duration (the Zero W has
  no `/usr/bin/time` and installing one is not authorized), the exact make
  target, and clean-versus-incremental status for native Pi builds. Prefer
  focused helpers, overlap slow ARM compilation with local work, and do not
  rerun a successful full build just to reconstruct a missing duration.
- **2026-08-02:** Accepted the three-window MCP3002 light capture with all 30
  sample pairs valid. Pair means were 997.3 uncovered, 179.0 fully covered,
  and 995.0 restored. The sensor response and recovery are clear, but a hand
  covering the sensor is not representative room illumination; retain the
  360/500 thresholds until the guarded application trial can observe a real
  bright-to-dark transition.
- **2026-08-02:** Accepted the 2 MHz LPD8806 experiment on both gates with zero
  failures. One frame took 3.193 ms; the 25-frame benchmark returned 25/25
  valid frames all inside the 12 ms tick, with a 3.001 ms median, 3.400 ms
  95th percentile, and 5.026 ms first-frame warm-up maximum. The strip stayed
  visually dark, the binding was removed, the ADC stayed on `mcp320x`, and the
  service stayed inactive. This accepts the arbitrary-pin `spi-gpio` strip path
  on timing and removes hardware-SPI rewiring from the expected plan. It does
  not authorize deployment: the production speed is still 1 MHz and no colored
  frame has been latched at the higher rate.
- **2026-08-02:** Accepted the guarded colored sequence with all 23 checks,
  zero failures, and zero warnings. All 240 pixels showed uniform red, then
  green, then blue at half brightness (63/127), then went dark. Frame times
  were 3.036 ms red, 4.424 ms green, 4.334 ms blue, and 4.377 ms for the final
  all-off, so the worst case kept better than 2.7x margin on the 12 ms tick.
  This is the first gate to latch non-zero pixel data and it closes the
  signal-integrity question: red rendered as red confirms the GRB wire order
  survived kernel `spidev`, and no stray, dead, or flickering pixel appeared.
  Firmware still reported `throttled=0x0`. Production strip speed was then
  promoted from 1 MHz to 2 MHz.
- **2026-08-02:** Wrote and built, but did not yet run, the two remaining
  Phase 5 gates. The [HT1632 render gate](wiringpi-phase5-ht1632-render.md)
  deliberately measures the existing matrix path before any bulk transport is
  written: the plan reserved one, but that decision was made while the strip
  and matrix shared a per-edge GPIO path, and the strip has since moved to
  kernel SPI. If the current renders already fit the 12 ms tick, the bulk
  rewrite should be deleted from the plan rather than built. This is a good
  article beat about not building the thing you assumed you would need.
- **2026-08-02:** Preparing the
  [whole-application trial](wiringpi-phase5-application-trial.md) surfaced a
  Phase 6 blocker worth telling readers about. The application only *opens*
  `/dev/spidev4.0`; it never binds it. That node exists only while the runtime
  `driver_override` binding is present, and the binding does not survive a
  reboot. A deployed clock cannot need a human to run a bind command after a
  power cut, so a persistence mechanism (udev rule, ordered systemd unit, or a
  Device Tree change) must be designed and reviewed before deployment. The fix
  must not be to give the application privilege to bind its own device.
- **2026-08-02:** The HT1632 render gate **failed on timing**, and that failure
  is the useful part. Content was perfect (even green then red stripes across
  all 16 chips, ending dark), but 0 of 20 forced full rewrites met the 12 ms
  tick: 18.9 ms minimum, 19.2 ms mean, 21.5 ms maximum. Working backward from
  roughly 7,100 GPIO writes per full rewrite gives about 2.7 microseconds per
  write, which is the same per-edge abstraction cost that defeated the strip
  before it moved to kernel SPI.
  The obvious objection is that `render()` is dirty-tracked, so production
  might rarely pay this. It does pay it: the clock update path calls `clear()`
  before redrawing, and `clear()` sets the buffer's global rewrite flag. So an
  ordinary clock tick already costs a full rewrite. This is the gate doing its
  job — it was written to ask "does the matrix still need a bulk transport now
  that the strip left the GPIO path?" and it answered yes, with a number, before
  any code was written. Good article beat: measure before you build, and be
  willing to have the measurement say "build it after all."
- **2026-08-02:** The bulk HT1632 transport landed and the same gate **passed**:
  20/20 renders inside the 12 ms tick, mean 4.094 ms (down from 19.164 ms), worst
  case 5.120 ms, and stripes visually identical to the slow run. Roughly 0.58
  microseconds per GPIO write, down from 2.7. The savings come from removing a
  mutex, a configured-line lookup with validation, and two virtual dispatches per
  edge, plus moving the memory barrier from every store to the burst boundary.
  Two details worth telling readers. First, the old path met the HT1632's 50 ns
  data-setup requirement *by accident*, because every write cost microseconds;
  going 100x faster meant that guarantee had to become explicit and tunable.
  Speeding something up can break a constraint that nobody wrote down because
  nothing had ever threatened it. Second, the burst path bypasses the ordinary
  write call, so no existing test would have noticed a mask wired to the wrong
  pin — it would compile, pass everything, and silently drive the wrong wire.
  The equivalence test that compares 15,044 edges between both paths, and which
  fails at edge 0 when CS and WR are swapped, is the real deliverable of that
  change.
- **2026-08-02:** The whole application ran on the real clock for the first
  time: **12 checks passed, one failed.** The two observations that rejected
  both earlier candidates — strip smoothness and timing — passed, and the
  operator rated response times *better than the original production clock*.
  Display, motion, MQTT, onboard Wi-Fi, and clean HTTP shutdown all passed.
  The failure was dimming, and the arithmetic suggests test execution rather
  than a defect: the reported light value is a rolling mean of ten samples taken
  every 600 ms, so crossing 360 from a 1022 baseline needs eight dark samples,
  about 4.8 seconds of *fully covered* sensor. The observed minimum of 403
  corresponds to roughly seven. Retest before touching the thresholds.
  Two things worth telling readers. First, the harness reported that the
  dimming thresholds *were* crossed, because the candidate publishes
  `light_sensor: 0` until its first ADC read and the check counted that startup
  sentinel as darkness. The automated check and the human disagreed, and the
  human was right; the failing observation is what prompted the audit that found
  the bug. Second, CPU is now a real open question: mean 18.29% and peak 75.68%
  against a 3.55% Phase 0 baseline, rising across the window. Some of that is
  inherent — kernel `spi-gpio` is still bit-banging, so it is CPU-bound rather
  than offloaded, and a ~4 ms strip frame plus a ~4 ms matrix render against a
  12 ms tick is a high duty cycle by construction. Fixing the latency did not
  make the work free.
- **2026-08-02: Phase 5 is complete.** The whole-application trial passed all
  13 checks with zero failures at commit `9677a0e`. Strip smoothness, timing,
  and automatic dimming — the three observations that rejected both earlier
  candidates — all passed, and the operator rated response times *better than
  the original production clock*.
  The dimming story is the best beat in this whole section, and it is not the
  one anyone expected. It took three runs. Run 1 failed and I guessed the
  operator had not held the cover long enough for the six-second averaging
  window. Run 2 disproved that: with a sustained cover the value reached a clear
  steady state of 452-478 and sat there for 45 seconds. The averaging was never
  the problem. What settled it was the operator switching *the actual room light
  off* instead of covering the sensor — the real condition the clock dims in.
  That showed the original 360 threshold was simply **unreachable**: the darkest
  the room ever gets still reads above it. Dimming could never have engaged, on
  any candidate, including during the Phase 0 baseline comparison. A decade-old
  constant had been quietly wrong, and only a migration that forced someone to
  actually watch the thing revealed it.
  Room darkness also varies more than expected: 452-478 on one run, 355-366 on
  the next. That spread is why 460 is right and 360 was not merely low — 360
  would have engaged on the darker night and missed the other entirely.
  Second beat: the harness *reported that the thresholds were crossed* on the
  run where they were not, because the app publishes `light_sensor: 0` until its
  first ADC read and the check counted that startup sentinel as darkness. The
  automated check and the human disagreed, and the human was right. A fully
  automated trial would have banked a clean pass and shipped a clock that could
  never dim.
  Still open, and honest to say so: CPU. Run 1 averaged 18.29% and peaked
  75.68% against a 3.55% Phase 0 baseline, rising late in a 300-second window;
  run 3 averaged 4.72% over 180 seconds. Something time-dependent may get
  expensive after several minutes. Kernel `spi-gpio` is still bit-banging, so
  it is CPU-bound rather than offloaded — fixing the latency did not make the
  work free.
- **2026-08-02 (correction):** The CPU figures from the first trial run were not
  a warning sign, and the earlier note guessing at a time-dependent cost was
  wrong. The operator was deliberately stressing the clock during that window
  with LED-strip animations plus `stickManAnimation.sh` posting image and
  message updates over HTTP. So 18.29% mean and 75.68% peak describe a **stress
  test that the modern stack passed**: roughly 25% headroom left at peak, with
  the display clean, the strip smooth, HTTP answering in about 96 ms, and
  operator-rated timing still better than production while all that ran.
  Ordinary operation measured 4.72% mean, close to the 3.55% Phase 0 baseline.
  Good reminder for the write-up: a number without the context of what the
  machine was being asked to do is not evidence of anything. The honest version
  of this section is "comparable to the original at rest, and it holds up under
  deliberate pounding," not "CPU went up."
- **Stress reproduction recipe.** The load that produced the 75.68% peak was a
  continuous strip animation plus `stickManAnimation.sh` over HTTP. The strip
  side is started with:
  `curl -X POST -d 'ledStripMode=4&timeout=7200' http://127.0.0.1/ledStrip`.
  Worth keeping for the article and for any future soak, since it exercises the
  240-pixel strip continuously through the 2 MHz kernel SPI path while the
  matrix and HTTP server are also busy.

- **2026-08-02 (end of session):** Phase 5 closed. Reconciled this notebook with
  the accepted evidence: the strip at 2 MHz, the colored-frame gate, the HT1632
  per-edge rejection and its burst replacement, the whole-application trial, the
  retuned 460/700 dimming thresholds, the harness false pass, the corrected CPU
  reading, and the deferred service trimming. Updated the editorial snapshot,
  the Phase 5 and Phase 6/7 chronology, the publication checklist, the article
  outline, the claims-that-must-wait list, the visuals list, and the source map.
  Every phase document in `docs/` is now linked from the source map, and every
  link in this file resolves.

  Two items block Phase 6 and must not be written as done: the strip `spidev`
  binding does not survive a reboot, and no soak has run. Everything else in the
  hardware stack is measured and accepted.

- **2026-08-03:** Phase 6's blocking items closed. The clock now starts itself.

  **Binding persistence.** Solved with a systemd oneshot,
  `oclock-strip-spi.service`, ordered before `oclock.service` with `Requires=`.
  A udev rule and a Device Tree change were both considered and rejected, and
  the reasons are more interesting than the solution: the DT route would have
  required borrowing another product's `compatible` string to obtain a side
  effect, and the udev route fails *silently*, which would hand the clock a
  guaranteed crash loop. Verified by an actual reboot, plus a negative test
  proving that a failed binding leaves the clock inactive with `NRestarts=0`
  instead of crash-looping.

  **Soak.** The clock ran unattended for 8 h 53 min at zero restarts, zero
  warnings, and zero Wi-Fi drops. Not a scheduled gate — it came from leaving
  the clock running overnight for fun, which is worth a line in the article
  about how the least ceremonious evidence in the whole migration was also some
  of the most convincing.

  Three findings worth carrying into the draft:

  - **The tests found a bug the reboot could not.** The binder originally
    sampled the driver symlink and character device once, immediately after
    writing to the bind control, and neither is guaranteed visible in that
    instant on a single-core ARMv6 during boot. That defect would have produced
    an intermittent boot failure, and rebooting a healthy unit would never have
    found it reliably. It now polls for the end state.
  - **The wait loop is insurance, not decoration.** On the verified boot the
    strip child was already present, but the ADC child did not probe until five
    seconds *after* the binder finished. These children appear asynchronously
    across that window; a binder without the wait would work most boots.
  - **A correction to our own record.** The Phase 5 trial result reported memory
    as "about 101 MB RSS." That was the `vsz` column. Resident is 5.2 MB; the
    101 MB is virtual address space, mostly thread stacks. On a 426 MB Pi Zero
    the difference is the difference between an investigation and a non-event.

  Also updated: the editorial snapshot, the service-procedure section (SPI
  ordering is settled; Wi-Fi ordering is still open and now has a measurement
  behind it), the Phase 6 chronology, the publication checklist, the
  claims-that-must-wait list, and the source map.

  What must still not be written as done: rollback has not been rehearsed, a
  hard power cut has not been tested, and nothing has run for days or weeks.
  Say "it comes back on its own after a reboot," not "it survives a power cut."

- **2026-08-03 (cleanup):** Retired the spent migration tooling. Twenty-one
  files — eight evidence collectors, eight guarded gate verifiers, the
  superseded binding helper, and four standalone measurement programs — moved
  to [`misc/junk/wiringpi-migration/`](../misc/junk/wiringpi-migration/CATALOG.md)
  with a catalog recording what each one proved and which result document holds
  its numbers. The five `make phase5-*` targets were removed and the four gate
  procedure documents now carry a banner saying so, since their command
  sequences no longer run.

  What stayed in `misc/` is the useful distinction for the article: the boot
  binder and its unit (live), the overlay manager (any new SD card needs it),
  the Tailscale bootstrap (maintenance access), and the animation and test
  scripts that predate this whole project.

  `tests/compatibility.sh` was rewritten rather than repointed. It had grown a
  long tail of assertions about one-shot gate scripts — checking that a
  collector contained no GPIO command, that a verifier prompted for the right
  confirmation word. Those were valuable while the gates were live and are
  noise now. It asserts nothing about the retired directory, so that directory
  can be deleted with a plain `git rm` and nothing else. In its place it gained
  the assertions that now matter: that `oclock.service` keeps `Requires=` on
  the binding unit, that the binding unit cannot be silently skipped by a
  `Condition`, and that the boot binder never hard-codes a bus number.
