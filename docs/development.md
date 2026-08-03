# Development sandbox

The production clock ran on a Raspberry Pi Zero with Raspbian 8 (Jessie). The
modern clock runs Raspberry Pi OS Lite 32-bit, which is Debian 13 (Trixie), so
the development sandbox is a **Debian 13 Trixie** Incus instance:

```sh
incus launch images:debian/13 oclock-gpiod-v2 \
  --config limits.cpu=2 \
  --config limits.memory=2GiB

incus exec oclock-gpiod-v2 -- bash -lc \
  'apt-get update && apt-get install -y build-essential git pkg-config \
   libevent-dev libmosquitto-dev libgpiod-dev \
   device-tree-compiler valgrind shellcheck gdb curl ca-certificates'
```

## Why Trixie specifically, and not Bookworm

This is not a preference. **Debian 12 Bookworm cannot build the hardware
profile at all.** Its newest `libgpiod-dev` is 1.6.3, which is the v1 API, and
`make hardware-preflight` correctly rejects anything that is not `2.*`:

```
error: libgpiod v2 development files are required
```

Trixie supplies libgpiod **2.2.1**, the exact version on the clock, and GCC
**14.2.0**, also the version on the clock. Matching those two is most of the
value of this sandbox.

## What only this environment catches

`make test` passes without libgpiod at all, because every test target builds
against `fakeGpio`. That means **nothing in the test suite ever compiles
`src/gpio/gpiodV2Gpio.cpp`** — the real GPIO backend. Neither does
`check-arm-warnings`, which also uses the fake.

So `make hardware` here is the only check of that file short of building on the
Pi. Run it before pushing anything that touches `src/gpio/`:

```sh
incus exec oclock-gpiod-v2 -- bash -lc 'cd /root/oclock.git && make hardware'
```

## Syncing a checkout in

`incus file push --recursive` has been unreliable for whole trees; piping a tar
of the tracked files is faster and avoids pushing `build/` and `.git/`:

```sh
git ls-files -z | tar --null -T - -cf - \
  | incus exec oclock-gpiod-v2 -- bash -c \
      'mkdir -p /root/oclock.git && tar -C /root/oclock.git -xf -'

incus exec oclock-gpiod-v2 -- bash -lc \
  'cd /root/oclock.git && make sandbox && make test'
```

`tar` may warn that timestamps are in the future; that is clock skew between
host and instance and is harmless.

## Container or VM

A container is enough and is what the commands above create. One limitation is
worth knowing: an unprivileged container cannot `mknod`, so a test needing a
real character device must symlink `/dev/null` instead —
[`tests/strip-binding.sh`](../tests/strip-binding.sh) does exactly that. Add
`--vm` if you want that restriction lifted, at the cost of a heavier instance.

## What the sandbox cannot tell you

The sandbox binary uses `src/gpio/fakeGpio.cpp` through the project-owned GPIO
interface; it exercises the application, threading, HTTP, MQTT client,
rendering logic, and clean shutdown without touching GPIO or linking WiringPi.
The instance is x86-64, while the clock is ARMv6, and its GPIO devices and
bit-bang timing are not virtualized. Every timing conclusion in this project
came from the real board for that reason. A final hardware build and
electrical/timing test must still run on the Pi:

```sh
make hardware
sudo ./oclock
```

The production defaults are intentionally unchanged: HTTP on `0.0.0.0:80`
and MQTT at `192.168.10.238:1883`. The sandbox and tests pass explicit
loopback addresses and high ports. The production HTTP default exposes an
unauthenticated device-control API and should only be used on a trusted
network or behind an authenticated reverse proxy.

## Make targets

Every target in the `Makefile`, what it actually does, and whether `make test`
runs it for you.

### Building

| Target | What it does |
| --- | --- |
| `all` | Alias for `hardware`. What a bare `make` runs. |
| `hardware` | Builds `./oclock`, the sole supported modern profile: libgpiod v2 for line ownership, the restricted `/dev/gpiomem` value path for high-rate GPIO, Linux `spidev` for the LPD8806, and native `mcp320x`/IIO for the MCP3002. Does not change owner or setuid mode. |
| `hardware-preflight` | An order-only prerequisite of every hardware object. Fails the build early with a readable message if `pkg-config` cannot find libgpiod, or finds a version that is not `2.*`. You rarely invoke it directly; its value is that a missing dependency fails at the first compile instead of at link time. |
| `sandbox` | Builds `./oclock-sandbox` against `fakeGpio`. Links neither libgpiod nor WiringPi, so it builds and runs on any x86-64 dev box. |
| `spi-overlay` | Compiles `hardware/oclock-spi-overlay.dts` to `build/oclock-spi-overlay.dtbo` with `dtc -@`. Needs `device-tree-compiler`. |
| `clean` | Removes `build/`, both binaries, `log/pulsar.log`, editor backups, and tag files. |

### Testing

| Target | What it does |
| --- | --- |
| `test` | The aggregate, and what to run before a commit. Runs the twelve targets below in order, all hardware-free. Stops at the first failure. |

Those twelve, in the order `test` runs them:

| Target | What it protects |
| --- | --- |
| `compatibility` | The behavior the deployed clock depends on: CLI defaults and exit status, the HTTP abbreviation quirk, both systemd units, the boot binder's guards, the overlay's compatibles, the measured dimming thresholds, and containment of the burst test hook. Also refuses to let the Tailscale bootstrap quietly become an SSH server or exit node. |
| `gpio-boundary` | That no production source outside the single legacy backend calls WiringPi directly. A pure `grep` over `src/`, `ht1632/`, `lpd8806/`, and `mcp300x/` — no build required. This is the guard that keeps the migration's whole premise honest. |
| `test-core` | Application core logic. |
| `test-gpio-protocols` | Device protocol traces against `FakeGpio`: initial levels, directions, bit ordering, GRB data, latch clocks, MCP3002 commands, motion mapping, cleanup, serialization. |
| `test-gpio-burst` | That the HT1632 bulk transport emits the identical edge sequence to the ordinary per-write path — 15,044 edges compared across all four matrix pins. Built with `-DOCLOCK_GPIO_BURST_TEST_HOOK`. |
| `test-gpio-registers` | BCM2835 register offset and mask arithmetic. |
| `test-spi-output` | The SPI transport against a deterministic fake, and separately compiles `linuxSpidevOutput.cpp` with `-Werror` so the real transport cannot rot. |
| `test-iio-analog` | The IIO analog input against fixture sysfs trees, including Device Tree identity discovery. |
| `test-strip-binding` | The boot-time strip binder against a fake sysfs tree: every refusal path, the idempotent re-run, and the exact writes performed. See [`tests/strip-binding.sh`](../tests/strip-binding.sh). |
| `check-arm-warnings` | Compiles every source with `-funsigned-char` and warnings as errors, links it, and runs the smoke test on the result. Catches ARM signedness bugs from an x86-64 dev box, where plain `char` is signed and ARM's is not. |
| `smoke` | Starts the sandbox, exercises it over HTTP, and shuts it down. |
| `test-shutdown` | Twenty start/stop cycles, checking for a clean exit each time. Threading and shutdown-ordering bugs are intermittent; twenty iterations is what makes them show up. |

The six C++ test binaries are built with **AddressSanitizer and
UndefinedBehaviorSanitizer** and run with `ASAN_OPTIONS=detect_leaks=1`, so
memory errors in the tested code fail the run rather than being reported as a
passing test.

### Targets `make test` does not run

Both are opt-in only because they need tools that are not in the base build
dependencies. Neither is slow. Install `valgrind` and `device-tree-compiler`
and there is no reason not to run them.

| Target | Why you would run it |
| --- | --- |
| `valgrind` | The one worth knowing about. It runs the **whole sandbox application** under `valgrind --leak-check=full` with `--errors-for-leak-kinds=definite,possible` and `--error-exitcode=99`, drives it over HTTP, and shuts it down. The sanitizers above cover the *test* binaries in isolation; this covers the real application with all its threads, the event loop, the MQTT client, and the shutdown path running together, which is where the interesting leaks live. **It takes about 3.5 seconds** on an x86-64 dev box — it is separate because it needs `valgrind` installed, not because it is expensive. Override the port with `OCLOCK_VALGRIND_PORT`. |
| `test-spi-overlay` | Merges the compiled overlay into a base Device Tree fixture with `fdtoverlay`, then asserts the merged result: both controllers are `spi-gpio`, both children carry the intended compatibles, and **all six BCM GPIO numbers are exactly the deployed wiring** — clock 20 and data 21 for the strip, 17/27/22/4 for the ADC. That last part is the no-rewiring compatibility contract encoded as a test: if someone "tidies" a pin number, this fails on a laptop instead of on a clock that no longer lights up. Requires `dtc`, `fdtoverlay`, and `fdtget` from `device-tree-compiler`. Run it whenever `hardware/oclock-spi-overlay.dts` changes, because a malformed overlay otherwise fails for the first time on a Pi at boot. |

### A note on running the suite twice

`compatibility` uses `make -B -n` for its build-inspection assertions. Without
`-B`, an already-built artifact makes `make -n` print nothing, the `grep` finds
nothing to match, and the assertion passes vacuously. It only ever passed
because it ran first in a clean tree. If you add a build-inspection assertion,
keep the `-B`.
