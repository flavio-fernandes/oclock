# oclock
c++11 codebase to control led matrix display, running in a Raspberry Pi

For more info, check out [these links](https://www.flaviof.com/blog/category/hacks.html):

- [Office Clock Part 1: the hardware](https://www.flaviof.com/blog/hacks/office-clock-part1.html)
- [Office Clock Part 2: the software](https://www.flaviof.com/blog/hacks/office-clock-part2.html)
- [Office Clock Part 3: a new brain, ten years later](https://www.flaviof.com/blog2/post/hacks/office-clock-part3/)
- [Read-only Raspberry Pi gotchas](https://www.flaviof.com/blog2/post/hacks/readonly-rpi-gotchas/)

Part 3 covers the 2026 rebuild: Raspberry Pi OS Trixie, WiringPi replaced by
libgpiod and the kernel SPI subsystem, and a read-only root filesystem, without
moving a single wire. The gotchas post is its companion, collecting the things
that broke along the way and why none of them were where the error message
pointed.

#### Adafruit Show and Tell

Adafruit offers guides along with products that make it easy to build this project.
Limor and Phil hosted [Show and Tell](https://www.youtube.com/c/adafruit/videos) on April 6th, 2016 and
I had the honor of [talking about what I did](https://youtu.be/LXa7T5t3hmA?t=7m24s) to them.

[![office-clock talk](https://img.youtube.com/vi/LXa7T5t3hmA/0.jpg)](https://youtu.be/LXa7T5t3hmA?t=7m24s)

## Building

The supported hardware target is a Raspberry Pi Zero W running Raspberry Pi
OS Lite 32-bit (Debian 13/Trixie). Install the application and GPIO development
packages, then build the selected modern hardware stack:

```sh
sudo apt install -y build-essential pkg-config libevent-dev \
    libmosquitto-dev libgpiod-dev
make
```

`make` and `make hardware` are equivalent build-only targets. They select
libgpiod v2 for GPIO ownership, the restricted `/dev/gpiomem` value path for
the current high-rate GPIO operations, and Linux `spidev` for the LPD8806
strip. They do not change the binary's owner or setuid mode. The former
`GPIO_BACKEND` and `STRIP_TRANSPORT` build knobs have been removed; supplying
either is an error.

The hardware application now runs against the live overlay: the ADC path uses
the native `mcp320x`/IIO driver and the strip uses kernel `spidev`. The clock
starts itself at boot, with `oclock-strip-spi.service` binding the strip's SPI
child before `oclock.service` starts. See the
[migration plan](docs/wiringpi-migration.md) and
[modern build policy](docs/wiringpi-modern-build-policy.md).

On a development machine without GPIO hardware:

```sh
make sandbox
./oclock-sandbox -b 127.0.0.1 -p 8080 -M 127.0.0.1
make test
```

`make test` runs thirteen hardware-free targets and is what to run before a
commit. Two more sit outside it only because they need tools that are not in
the base build dependencies. Neither is slow:

```sh
make valgrind          # whole application under leak-check=full
make test-spi-overlay  # merge and verify the Device Tree overlay
```

Every target is described in
[docs/development.md](docs/development.md#make-targets).

The clock reports itself at `GET /status` (human-readable) and
`GET /status.json` (the same data as JSON), including CPU load, free memory,
and whether the matrix is currently dimmed. See
[docs/status-api.md](docs/status-api.md) for the schema and its compatibility
rules.

For compatibility with the deployed clock, the runtime defaults remain
`0.0.0.0:80` and MQTT broker `192.168.10.238:1883`. Use `-b`, `-p`, `-M`,
and `-P` to override them. The control API does not provide authentication,
so it should only be exposed on a trusted network.
See [docs/development.md](docs/development.md) for the reproducible Incus VM
workflow and the limits of fake-GPIO testing.

The original Pi Zero/Jessie/WiringPi hardware stack is preserved as the
physical rollback unit rather than as a supported build from the current
tree. See
[docs/wiringpi-migration.md](docs/wiringpi-migration.md) for the pin inventory,
compatibility contract, implementation sequence, and hardware acceptance gates.
