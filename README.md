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

On a Raspberry Pi with WiringPi installed:

```sh
make
```

The default target preserves the original deployment behavior: it builds
`oclock`, changes it to `root:root`, and enables the owner setuid bit. Use
`make hardware` when only a hardware binary is wanted without changing its
owner or mode.

On the modern Raspberry Pi Zero W target running Raspberry Pi OS 32-bit
(Debian 13/Trixie), install the application and GPIO development packages and
select the modern backend explicitly:

```sh
sudo apt install -y build-essential pkg-config libevent-dev \
    libmosquitto-dev libgpiod-dev
make GPIO_BACKEND=gpiod hardware
```

This produces the same `oclock` filename but links libgpiod v2 instead of
WiringPi. Plain `make` and `make hardware` continue to select WiringPi for the
existing Jessie deployment.

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

The proposed removal of direct WiringPi dependencies is deliberately phased so
the deployed Pi Zero remains recoverable. See
[docs/wiringpi-migration.md](docs/wiringpi-migration.md) for the pin inventory,
compatibility contract, implementation sequence, and hardware acceptance gates.
