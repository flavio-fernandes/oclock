[![Build Status](https://travis-ci.org/flavio-fernandes/oclock.svg?branch=master)](https://travis-ci.org/flavio-fernandes/oclock)

# oclock
c++11 codebase to control led matrix display, running in a Raspberry Pi

For more info, check out [these links](http://www.flaviof.com/blog/category/hacks.html):

    http://www.flaviof.com/blog/hacks/office-clock-part1.html
    http://www.flaviof.com/blog/hacks/office-clock-part2.html

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

This is still an in-progress migration profile. Do not run the whole hardware
application while the Office Clock Device Tree overlay owns the MCP3002 pins;
the application ADC path has not yet moved to IIO. See the
[migration plan](docs/wiringpi-migration.md) and
[modern build policy](docs/wiringpi-modern-build-policy.md).

On a development machine without GPIO hardware:

```sh
make sandbox
./oclock-sandbox -b 127.0.0.1 -p 8080 -M 127.0.0.1
make test
make check-arm-warnings
make valgrind
```

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
