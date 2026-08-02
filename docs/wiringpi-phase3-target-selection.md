# WiringPi migration Phase 3 target selection

## Selected candidate

As of 2026-07-31, the target candidate is the standard **Raspberry Pi OS Lite
(32-bit)** image:

- Raspberry Pi identifies the standard 32-bit image as compatible with all
  Raspberry Pi models.
- The current standard image is Debian 13 (Trixie), released 2026-06-18 with
  kernel 6.18.
- The current legacy 32-bit image is Debian 12 (Bookworm). It remains
  compatible with the original Pi Zero, but it is not preferred merely to
  reduce the GPIO API migration.
- Debian Trixie supplies `libgpiod-dev` and `gpiod` 2.2.1 for `armhf`.

Primary references:

- [Raspberry Pi OS downloads](https://www.raspberrypi.com/software/operating-systems/)
- [Raspberry Pi Zero hardware and BCM2835 identification](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html)
- [Debian Trixie libgpiod development package](https://packages.debian.org/trixie/libgpiod-dev)
- [libgpiod command-line tools](https://libgpiod.readthedocs.io/en/master/gpio_tools.html)

This selection is provisional until the exact original Pi Zero boots it and the
collector passes. The dated facts above must be rechecked if a later image is
used.

## Safety boundary

Use a separate microSD card. Do not upgrade, alter, or install packages on the
known-good Jessie card. Keep the Phase 0 rollback executable and Jessie card
together.

The production board is an original Raspberry Pi Zero Rev 1.2 without onboard
Wi-Fi. Preparing and updating the candidate therefore requires either:

- a local console plus a supported USB network adapter; or
- another safe way to provide network and terminal access to the spare image.

The hardware may remain wired as documented. Do not start the office-clock
application from the candidate card during target collection. The collector
does not request GPIO lines, sample their electrical values, or change their
direction.

## Prepare the spare card

In Raspberry Pi Imager:

1. Select the original Raspberry Pi Zero.
2. Select Raspberry Pi OS Lite (32-bit), using the standard image rather than
   Raspberry Pi OS (Legacy).
3. Select the spare microSD card after verifying its identity and capacity.
4. Configure a username and SSH only if the chosen network/console setup
   supports them.
5. Write and verify the image.

Label the old and new cards before removing the Jessie card. Shut the Pi down,
remove power, swap only the microSD card, then restore power.

On the candidate image:

```sh
sudo apt update
sudo apt full-upgrade -y
sudo apt install -y git build-essential pkg-config gpiod libgpiod-dev \
    libevent-dev libmosquitto-dev
sudo reboot
```

After reconnecting, confirm that no office-clock process or service is running:

```sh
systemctl is-active oclock || true
pgrep -a oclock || true
```

## Run the collector

Extract the collector from the PR branch without building or starting the
application:

```sh
git clone https://github.com/flavio-fernandes/oclock.git ~/oclock-phase3
cd ~/oclock-phase3
git fetch origin agent/plan-wiringpi-migration
phase3_collector=/tmp/collectGpioTarget.sh
git show FETCH_HEAD:misc/collectGpioTarget.sh >"${phase3_collector}"
chmod 0755 "${phase3_collector}"
sudo "${phase3_collector}"
```

Share the reported `.tar.gz` and `.sha256` files. A successful result must show:

- Raspberry Pi Zero hardware, `armv6l`, and the `armhf` package architecture;
- Debian 13;
- libgpiod major version 2 and the installed development package;
- a GPIO character device labelled `pinctrl-bcm2835`, discovered without
  assuming `gpiochip0`;
- metadata for every BCM offset used by the clock;
- successful compilation and execution of a read-only GPIO ABI v2 line-info
  probe.

The archive intentionally omits hostname, IP configuration, network sockets,
SSH configuration, and the device serial. Raw evidence remains outside Git.

The accepted capture and the distinction between its Zero W host and the
production non-W Zero are recorded in the
[target baseline](wiringpi-phase3-target-baseline.md).

## Decision after capture

Every required check passed. Phase 3 therefore selected and implemented a
libgpiod v2 backend against the captured 2.2 API and verified chip-label/offset
mapping. The backend remains opt-in, and WiringPi remains the default for
Jessie.

If the standard Trixie image does not boot reliably or the collector fails, do
not modify the Jessie card. Retain the archive, restore the old card, and
evaluate the failure before considering the legacy Bookworm image.
