# WiringPi migration resume handoff — 2026-08-02

## Resume anchor

- Draft PR: [#3 — Migrate GPIO to Zero W/Trixie without breaking legacy Pi
  Zero](https://github.com/flavio-fernandes/oclock/pull/3)
- Branch: `agent/plan-wiringpi-migration`
- Last completed evidence commit before this handoff: `fa28cbd`
  (`Record failed mapped GPIO trial`)
- PR base at the stopping point: `master` commit `2b696b7`
- Repository worktree was clean before this documentation-only checkpoint.

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
| LPD8806 strip | `spi-gpio` controller exposed through `spidev` | clock 20, data 21 |
| MCP3002 light ADC | second `spi-gpio`/`spidev` bus | clock 17, MISO 27, MOSI 22, CS 4 |
| HT1632 matrix | retain a narrow bulk mmap transport initially | CS 6, WR 13, data 19, select clock 26 |

This plan preserves the physical wiring. `spi-gpio` is selected specifically
because it can create kernel-managed software SPI buses on the existing
arbitrary GPIOs; this is not a move to the Zero W's fixed hardware-SPI pins.

The final HT1632 transport remains deliberately separate. Its extra select
chain and mixed 3-bit, 7-bit, and 4-bit fields do not map cleanly to ordinary
byte-oriented `spidev` transfers. Do not force all devices through one
transport abstraction merely to make the design look uniform.

## Compatibility and safety boundaries

- Plain `make` and `make hardware` must continue to select WiringPi.
- The original Zero/Jessie card, WiringPi installation, service configuration,
  and Phase 0 binary remain the complete rollback unit.
- No wiring change is authorized.
- No modern candidate is deployment-approved; Phase 6 remains blocked.
- Do not change the light thresholds until raw MCP3002 channels are compared
  under controlled covered/uncovered conditions.
- Do not enable a Device Tree overlay until its GPIO claims, unload/disable
  rollback, service permissions, and boot-failure behavior are documented and
  reviewed. Once enabled, the SPI controller owns those lines and the current
  GPIO drivers must not request them.
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
- Phase 4 passed the isolated Trixie Incus compatibility, boundary,
  sanitizer-backed core/protocol, legacy compile, ARM warning, smoke, and
  repeated graceful-shutdown coverage.
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
per logical transaction while `spi-gpio` drives the existing pins. The matrix
keeps a narrow custom path only because its select topology is not ordinary
SPI.

## Tomorrow's first work

Start with read-only target discovery; do not ask the operator to enable an
overlay yet.

1. Reconfirm the PR branch against the latest `master` and rebase only if
   necessary, preserving the existing PR 3 history.
2. Run the read-only
   [`collectPhase5SpiTarget.sh`](../misc/collectPhase5SpiTarget.sh) collector on
   the Zero W/Trixie image. It records:
   `CONFIG_SPI`, `CONFIG_SPI_GPIO`, `CONFIG_SPI_SPIDEV`, loaded/available
   modules, current SPI controllers and `/dev/spidev*`, GPIO consumers, boot
   configuration locations, overlay tooling, and the downstream kernel's
   spidev binding evidence. See the
   [kernel-SPI discovery handoff](wiringpi-phase5-kernel-spi-discovery.md).
3. Design a reversible Device Tree overlay containing two independent
   `spi-gpio` controllers on the exact LPD8806 and MCP3002 pins. Keep it
   disabled by default and include explicit uninstall/disable instructions.
4. Add a project-owned SPI transport boundary plus a deterministic fake. Keep
   the legacy WiringPi device implementations and build defaults intact.
5. Convert the LPD8806 first. Preserve its 720-byte GRB frame and eight-byte
   zero latch exactly, then run Incus tests, a native ARM build, and an
   exact-board timing trial before converting another device.
6. Convert the MCP3002 only after the strip path is proven. Use a full-duplex
   transaction and add raw per-channel evidence so light calibration can be
   separated from protocol correctness.
7. Revisit the HT1632 only after the two standard SPI devices are settled.

If the kernel `spi-gpio` strip still cannot meet the 12 ms animation cadence,
record that result before considering rewiring to hardware SPI. Do not reduce
the refresh rate to make a failing transport appear acceptable.
