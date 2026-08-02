# WiringPi migration resume handoff — 2026-08-02

## Resume anchor

- Draft PR: [#3 — migrate the Office Clock hardware stack to Zero W/Trixie](https://github.com/flavio-fernandes/oclock/pull/3)
- Branch: `agent/plan-wiringpi-migration`
- Last completed evidence commit before this handoff: `fa28cbd`
  (`Record failed mapped GPIO trial`); the most recent accepted hardware
  evidence is the [2 MHz strip result](wiringpi-phase5-lpd8806-2mhz-result.md)
- PR base at the stopping point: `master` commit `2b696b7`
- Repository worktree was clean before this documentation-only checkpoint.
- Living public-write-up memory:
  [`office-clock-part3-blog-notes.md`](office-clock-part3-blog-notes.md). Keep
  it current whenever the migration changes installation requirements,
  obsolete steps, selected architecture, hardware evidence, or deployment
  status.

Fetch the named branch explicitly when resuming. Continue to push with an
explicit refspec, because this repository previously exposed a dangerous
upstream/default-push interaction:

```sh
git push origin HEAD:refs/heads/agent/plan-wiringpi-migration
```

Do not use a branch push command that could resolve its destination from an
unexpected upstream.

## User decision

The user approved a move away from the single-backend experiment toward a
mixed modern transport on the Raspberry Pi Zero W/Trixie unit:

| Device | Selected modern transport | Existing BCM GPIOs |
| --- | --- | --- |
| Motion sensor | libgpiod v2 input | 10 |
| LPD8806 strip | `spi-gpio` controller with explicit `spidev` binding | clock 20, data 21 |
| MCP3002 light ADC | second `spi-gpio` bus with native `mcp320x`/IIO | clock 17, MISO 27, MOSI 22, CS 4 |
| HT1632 matrix | retain a narrow bulk mmap transport initially | CS 6, WR 13, data 19, select clock 26 |

This plan preserves the physical wiring. `spi-gpio` is selected specifically
because it can create kernel-managed software SPI buses on the existing
arbitrary GPIOs; this is not a move to the Zero W's fixed hardware-SPI pins.

The final HT1632 transport remains deliberately separate. Its extra select
chain and mixed 3-bit, 7-bit, and 4-bit fields do not map cleanly to ordinary
byte-oriented `spidev` transfers. Do not force all devices through one
transport abstraction merely to make the design look uniform.

## Compatibility and safety boundaries

- The original Zero/Jessie card, WiringPi installation, service configuration,
  and Phase 0 binary remain the complete rollback unit.
- The current tree has one supported hardware build. Plain `make` and
  `make hardware` select the modern gpiod/mmap plus spidev profile; the former
  `GPIO_BACKEND` and `STRIP_TRANSPORT` variables are rejected.
- Historical WiringPi, pure-libgpiod, and bit-banged transport sources may
  remain for comparison and debugging, but they are not supported or tested
  build profiles. See the
  [modern build policy](wiringpi-modern-build-policy.md).
- No wiring change is authorized.
- No modern candidate is deployment-approved; Phase 6 remains blocked.
- Do not change the light thresholds until raw MCP3002 channels are compared
  under controlled covered/uncovered conditions.
- Enable the Device Tree overlay only through the documented guarded live-boot
  gate. Its GPIO claims, normal disablement, SD-card rescue, service boundary,
  and boot behavior are now documented. Once enabled, the SPI controller owns
  those lines and the current GPIO drivers must not request them.
- Keep the mmap backend experimental and narrowly limited to the HT1632
  follow-up. Do not turn it into a general replacement for kernel SPI.
- At the end of the last captured trial, the candidate exited cleanly and the
  Zero W `oclock.service` was inactive. Confirm live state rather than assuming
  it remains so tomorrow.

## Completed phases and evidence

- Phase 0 captured the healthy original Zero/Jessie/WiringPi system and
  preserved its exact rollback executable.
- Phase 1 introduced the project-owned GPIO boundary without changing legacy
  runtime behavior and passed the guarded legacy hardware trial.
- Phase 2 added deterministic fake-GPIO protocol and serialization coverage.
- Phase 3 selected the Zero W Rev 1.1, ARMv6/armhf Trixie, libgpiod 2.2 stack
  and produced a native modern build.
- Phase 4 passed the isolated Trixie Incus application/boundary,
  sanitizer-backed core/protocol, ARM warning, smoke, and repeated
  graceful-shutdown coverage.
- Phase 5 pure libgpiod passed functional I/O but failed dimming and timing.
- Phase 5 `gpiod-mmap` was definitely faster and still passed functional I/O,
  but it also failed dimming and timing, especially on the 240-pixel strip.

The most recent evidence is:

| Item | Value |
| --- | --- |
| Fast candidate source | `1f5605dbc6e8b43165190a150d47c9c1fee86a9d` |
| Fast candidate SHA-256 | `af7a168af98c7e4765a91d70fd2926281e47fe647e804ccc24d0db92e3c2338e` |
| Preserved Zero W binary | `/home/pi/oclock-phase5/oclock-gpiod-mmap-1f5605d` |
| Trial archive | `oclock-phase5-20260802T050318Z-GF94nKjP.tar.gz` |
| Trial archive SHA-256 | `a1aa2a71c89c0342f95b280a09c19919d5c9cce20d392e4221cf66240a339379` |

That run recorded clean shutdown, correct matrix content and strip colors,
motion transitions, MQTT/external-data behavior, and stable onboard Wi-Fi. It
failed three gates: dark-threshold crossing, observed dimming, and acceptable
timing. Light samples ranged from `478` to `1023`, never below the `360`
low-water mark. Post-startup host CPU averaged `9.14%`; the Phase 0 WiringPi
short-interval average was `3.55%`.

See the detailed [`gpiod-mmap` result](wiringpi-phase5-fast-result.md),
[initial pure-backend result](wiringpi-phase5-initial-result.md), and
[migration plan](wiringpi-migration.md).

## Why the architecture changed

The mapped backend removed one libgpiod ioctl per edge but retained per-edge
userspace locking, configured-line lookup, virtual dispatch, validation, and
memory-barrier costs. A 240-pixel LPD8806 refresh emits 5,760 data bits and at
least 11,648 clock-edge writes before data transitions are counted. The result
proved that the current per-edge GPIO abstraction cannot meet the preserved
strip timing.

The approved plan sends complete strip and ADC transfers into the kernel SPI
subsystem instead. The application will make one buffered `spidev` operation
per strip frame, while the kernel's native `mcp320x` driver exposes the ADC
through IIO. `spi-gpio` drives the existing pins for both. The matrix keeps a
narrow custom path only because its select topology is not ordinary SPI.

## Current next work

The read-only target discovery completed on 2026-08-02. Archive
`oclock-phase5-spi-20260802T135213Z-RCuWzN1Y.tar.gz` has SHA-256
`a650a19ba134e59d81ac78b56263f07c60c08641df71c6cb4e887c0d4e420eb0`.
Its `dtoverlay` failure was a false negative caused by bare-help exit status;
both list operations passed. Exact source correlation selected native IIO for
the MCP3002 and an explicit `spidev` override for the LPD8806. See the
[accepted result](wiringpi-phase5-kernel-spi-result.md).

The exact-board [offline overlay result](wiringpi-phase5-spi-overlay-result.md)
also passed with zero failures. Its archive is
`oclock-phase5-spi-dry-run-20260802T143057Z-NbEl7pZe.tar.gz`, SHA-256
`963c6d2cf03bfbad056736306a8561ec13be3756035f27ac775cb4c5408e50d1`.
It reconfirmed all modules, compiled and merged the overlay against the active
tree, and preserved every reviewed pin and child binding without changing the
target.

The guarded [live overlay boot](wiringpi-phase5-spi-live-boot.md) subsequently
passed with 10 checks, zero failures, and zero warnings. Archive
`oclock-phase5-spi-boot-20260802T151201Z-YEXB983Q.tar.gz` has SHA-256
`c6919e9f4f2b824ed5a066467582a6da9de1825c1258278943272601a247ad4c`.
The accepted [result](wiringpi-phase5-spi-live-boot-result.md) records dynamic
children `spi3.0`/`spi4.0`, native MCP3002/IIO binding, kernel GPIO ownership,
onboard-Wi-Fi recovery, and the retained boot backup. The overlay is active;
`oclock.service` remains stopped and no device transfer has occurred.

The runtime-only [LPD8806 binding](wiringpi-phase5-lpd8806-binding.md) also
passed all eight checks. Archive
`oclock-phase5-lpd-bind-20260802T152824Z-7zjktxAZ.tar.gz` has SHA-256
`2bfdaeb9f0e0ae6a119e12925671b57da296f084e7d67c2eb3e95e5fbfef0f5a`.
The accepted [result](wiringpi-phase5-lpd8806-binding-result.md) records the
transient `root:spi` device, preserved ADC binding, no-transfer boundary, and
successful explicit unbind. The strip is again unbound and the service is
inactive.

The hardware-free [LPD8806 transport](wiringpi-phase5-lpd8806-transport.md)
now provides dynamic Device Tree discovery, verified spidev configuration, a
deterministic fake, and a single 720-byte GRB plus eight-byte latch transfer.
Its exact-board [native build](wiringpi-phase5-lpd8806-build-result.md) passed
at commit `37b6797`. The preserved binary SHA-256 is
`834191828a27ac801e0e959435e0397068b4b9b2335455b4128d29256dbd8807`.
It links libgpiod and libatomic without WiringPi and discovers the strip by
Device Tree suffix. The old build selectors were then retired in favor of one
modern hardware build; historical sources remain only as diagnostic evidence.

1. The corrected
   [all-off first-transfer gate](wiringpi-phase5-lpd8806-first-transfer.md)
   passed all 17 checks. One 728-byte mode-0 frame completed, the strip stayed
   dark, and rollback succeeded. Its 20,956-microsecond `show()` time is above
   the 12 ms tick. The subsequent
   [25-frame cadence gate](wiringpi-phase5-lpd8806-cadence-result.md) returned
   valid, visually safe frames but rejected the 1 MHz profile: 0/25 met the
   budget, with a 20,473-microsecond median and 24,722-microsecond 95th
   percentile.

   The [2 MHz experiment](wiringpi-phase5-lpd8806-2mhz-result.md) then **passed
   both gates on 2026-08-02** with zero failures: one frame at 3,193
   microseconds, then 25/25 frames within budget at a 3,001-microsecond median
   and 3,400-microsecond 95th percentile, strip visually dark throughout. The
   arbitrary-pin `spi-gpio` strip path is accepted on timing and rewiring is
   not required.

   The [colored sequence gate](wiringpi-phase5-lpd8806-colors-result.md) then
   **passed all 23 checks on 2026-08-02**, latching uniform red, green, and
   blue across all 240 pixels at half brightness with correct GRB byte order,
   ending dark, slowest frame 4,424 microseconds. Signal integrity at 2 MHz on
   the existing arbitrary-pin wiring is no longer an open question, and the
   production speed has been promoted to 2 MHz.

   Still do not run the full application. Remaining Phase 5 work is the narrow
   HT1632 bulk transport, then a guarded whole-application run.
2. The MCP3002 application path now uses native IIO and its guarded first read
   passed all 13 checks. The controlled ten-sample windows then averaged 997.3
   uncovered, 179.0 fully covered, and 995.0 restored. Preserve the 360/500
   thresholds until representative room-light behavior can be observed during
   a later guarded application run.
3. Revisit the HT1632 only after the two standard SPI devices are settled.

The 1 MHz kernel `spi-gpio` strip cannot meet the 12 ms animation cadence, but
the 2 MHz undelayed path meets it with better than 2x margin. Do not reduce the
refresh rate to make a transport appear acceptable.

The Zero W has no `/usr/bin/time`, and installing a package is not an
authorized target change for these gates. Time native Pi builds with a shell
wall-clock measurement instead and record that it is wall clock, plus whether
the build was clean or incremental. Prefer
narrow helper targets and overlap unavoidable ARM compilation with local
tests, documentation, or evidence review. Do not repeat a successful full
build solely to recover a missing timing measurement.
