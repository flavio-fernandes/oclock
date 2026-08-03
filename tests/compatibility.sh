#!/usr/bin/env bash

set -euo pipefail

binary=${1:-./oclock-sandbox}
test_dir=$(mktemp -d)

cleanup() {
    rm -rf "${test_dir}"
}
trap cleanup EXIT

# The original CLI printed help and returned failure. More importantly, the
# help text exposes the deployed network defaults without binding port 80.
if "${binary}" -h >"${test_dir}/help.txt" 2>&1; then
    echo "legacy help exit status changed" >&2
    exit 1
fi
grep -q 'IPv4 address to bind (default: 0.0.0.0)' "${test_dir}/help.txt"
grep -q 'tcp port number to listen on (default: 80)' "${test_dir}/help.txt"
grep -q 'broker to connect to (default: 192.168.10.238)' "${test_dir}/help.txt"
grep -q 'port of that mqtt server (default: 1883)' "${test_dir}/help.txt"
grep -q 'keep alive interval in seconds (default: 182)' "${test_dir}/help.txt"

# Preserve current application/service paths. Hardware transport selection is
# tested in the boundary suite.
grep -qx 'After=network.target' misc/oclock.service
grep -qx 'ExecStart=/home/pi/oclock.git/oclock' misc/oclock.service
grep -qx 'StandardOutput=null' misc/oclock.service
grep -qx 'Alias=oclock.service' misc/oclock.service
make -B -n CC=modern-cxx hardware >"${test_dir}/compiler.txt"
grep -q 'modern-cxx -c' "${test_dir}/compiler.txt"

# Existing clients may abbreviate display POST keys; preserve that behavior.
grep -Fq 'strncasecmp(key, "msg", strlen(key)) == 0' src/displayInternal.cpp
grep -Fq 'strncasecmp(key, "animationStep", strlen(key)) == 0' src/displayInternal.cpp

# The strip has no character device until something binds its deliberately
# unclaimed Device Tree child. Requires= rather than Wants= is the whole point:
# without the binding the clock cannot open its SPI output, and Restart=
# on-failure would turn that into a crash loop. Refusing to start is better.
grep -qx 'Requires=oclock-strip-spi.service' misc/oclock.service
grep -qx 'After=oclock-strip-spi.service' misc/oclock.service
grep -qx 'Restart=on-failure' misc/oclock.service

# The binding is state, not a process, and stopping it must reverse it.
grep -qx 'Type=oneshot' misc/oclock-strip-spi.service
grep -qx 'RemainAfterExit=yes' misc/oclock-strip-spi.service
grep -qx 'Before=oclock.service' misc/oclock-strip-spi.service
grep -Fq 'ExecStop=' misc/oclock-strip-spi.service
# A missing overlay must fail loudly here rather than silently skipping the
# unit and handing a guaranteed crash loop to oclock.service.
if grep -Eq '^[[:space:]]*Condition' misc/oclock-strip-spi.service; then
    echo "the strip binding unit can be silently skipped by a Condition" >&2
    exit 1
fi
# The start timeout must exceed the helper's own wait or systemd kills it
# mid-binding.
grep -qx 'TimeoutStartSec=120' misc/oclock-strip-spi.service
grep -Fq 'bind --wait 90' misc/oclock-strip-spi.service

# spi_gpio is a module loaded during udev coldplug, so the SPI children appear
# asynchronously. The boot binder must wait for its child and must discover it
# by Device Tree path, never by a dynamic bus number.
bash -n misc/bindOclockStripSpi.sh
misc/bindOclockStripSpi.sh --help >"${test_dir}/strip-bind-help.txt"
grep -Fq '/oclock-strip-spi/lpd8806@0' misc/bindOclockStripSpi.sh
grep -Fq 'await_strip_device' misc/bindOclockStripSpi.sh
grep -Fq 'Incomplete strip binding was rolled back.' misc/bindOclockStripSpi.sh
if grep -Fq 'spi4.0' misc/bindOclockStripSpi.sh; then
    echo "boot strip binder hard-codes a dynamic SPI device" >&2
    exit 1
fi
# The binder must never open the device or drive the application.
if grep -Eq '(^|[[:space:]])(systemctl|dtoverlay|reboot|shutdown|gpioset|gpioget|gpiomon)[[:space:]]' \
        misc/bindOclockStripSpi.sh; then
    echo "boot strip binder contains an out-of-scope command" >&2
    exit 1
fi

# The overlay identifies the strip with a project-owned compatible precisely so
# no in-tree driver claims it by accident.
grep -Fq 'compatible = "flaviof,oclock-lpd8806"' \
    hardware/oclock-spi-overlay.dts
grep -Fq 'compatible = "microchip,mcp3002"' \
    hardware/oclock-spi-overlay.dts

# The overlay manager is deployment tooling and still in use. It must keep its
# checksum pin and its refusal to reboot.
bash -n misc/managePhase5SpiOverlay.sh
misc/managePhase5SpiOverlay.sh --help \
    >"${test_dir}/phase5-spi-manager-help.txt"
grep -Fq '53b593f4c30a78c8beb446cf506816af29a817d0daf93bb899a9054c7aa61959' \
    misc/managePhase5SpiOverlay.sh
grep -Fq 'backup is not byte-identical' misc/managePhase5SpiOverlay.sh
grep -Fq 'does not reboot' "${test_dir}/phase5-spi-manager-help.txt"

# 1 MHz missed the 12 ms tick 25 of 25 times; 2 MHz takes the kernel's
# undelayed free-running path. This constant is the whole difference.
grep -Fq 'OCLOCK_STRIP_SPEED_HZ 2000000U' src/spi/StripSpeed.h
grep -Fq 'std::uint32_t mode = SPI_MODE_0;' \
    src/spi/linuxSpidevOutput.cpp
if grep -Fq 'SPI_MODE_0 | SPI_NO_CS' src/spi/linuxSpidevOutput.cpp; then
    echo "spidev transport requests unsupported SPI_NO_CS mode" >&2
    exit 1
fi

# The supported ADC path must use the native IIO device by Device Tree
# identity, never a copied dynamic IIO or SPI number.
grep -Fq '/oclock-adc-spi/mcp3002@0' \
    src/adc/linuxIioAnalogInput.cpp
grep -Fq 'in_voltage0_raw' docs/wiringpi-phase5-mcp3002-iio.md
grep -Fq 'in_voltage1_raw' docs/wiringpi-phase5-mcp3002-iio.md
if grep -Eq '/sys/bus/iio/devices/iio:device[0-9]+' \
        src/adc/linuxIioAnalogInput.cpp; then
    echo "IIO ADC path hard-codes a dynamic device number" >&2
    exit 1
fi

# The dimming thresholds are measured values, not arbitrary constants. The
# original 360 was unreachable on this unit, so dimming could never engage.
grep -Fq 'darkRoomThresholdLowWaterMark = 460' src/lightSensor.cpp
grep -Fq 'darkRoomThresholdHighWaterMark = 700' src/lightSensor.cpp

# The HT1632 burst path bypasses Gpio::write(). Its test hook replaces the real
# register store, so it must never reach a hardware or sandbox build.
make -B -n hardware >"${test_dir}/burst-hardware-build.txt"
if grep -Fq 'OCLOCK_GPIO_BURST_TEST_HOOK' "${test_dir}/burst-hardware-build.txt"; then
    echo "the burst test hook leaked into the hardware build" >&2
    exit 1
fi
make -B -n sandbox >"${test_dir}/burst-sandbox-build.txt"
if grep -Fq 'OCLOCK_GPIO_BURST_TEST_HOOK' "${test_dir}/burst-sandbox-build.txt"; then
    echo "the burst test hook leaked into the sandbox build" >&2
    exit 1
fi
make -B -n test-gpio-burst >"${test_dir}/burst-test-build.txt"
grep -Fq -- '-DOCLOCK_GPIO_BURST_TEST_HOOK' "${test_dir}/burst-test-build.txt"

# Declining a burst must remain valid: the default returns false and the
# matrix must keep a working gpio.write() fallback.
grep -Fq 'return false;' src/gpio/Gpio.h
grep -Fq 'gpio.write(bcmGpio, value);' ht1632/HT1632.h
# The datasheet setup guarantee must survive; the old per-write cost provided
# it incidentally and a direct register store does not.
grep -Fq 'OCLOCK_GPIO_BURST_SETUP_NOPS' src/gpio/GpioBurst.h
grep -Fq 'gpioBurstSetupDelay();' ht1632/HT1632.h
# Only lines already configured as outputs may be handed out.
grep -Fq 'GPIOD_LINE_DIRECTION_OUTPUT' src/gpio/gpiodV2Gpio.cpp

# Remote maintenance uses existing OpenSSH over a non-routing Tailscale node.
# Keep the dedicated key source-restricted and prevent this bootstrap from
# quietly turning the clock into a Tailscale SSH server, router, or exit node.
bash -n misc/bootstrapOclockTailscale.sh
misc/bootstrapOclockTailscale.sh --help \
    >"${test_dir}/tailscale-bootstrap-help.txt"
grep -Fq 'from=\"${source_ip}\"' misc/bootstrapOclockTailscale.sh
grep -Fq 'codex-office-clock-2026-08-02' misc/bootstrapOclockTailscale.sh
grep -Fq 'tailscale up --hostname=oclock --accept-dns=false' \
    misc/bootstrapOclockTailscale.sh
grep -Fq 'systemctl is-active --quiet oclock' \
    misc/bootstrapOclockTailscale.sh
if grep -Eq -- '--ssh|--advertise-routes|--advertise-exit-node|--exit-node' \
        misc/bootstrapOclockTailscale.sh; then
    echo "remote bootstrap enables an unauthorized Tailscale role" >&2
    exit 1
fi
if grep -Eq 'meteor-copperhead|100\.108\.157\.127' \
        misc/bootstrapOclockTailscale.sh docs/oclock-remote-access.md; then
    echo "remote-access files expose private tailnet topology" >&2
    exit 1
fi

# Retired Phase 5 gate tooling lives in misc/junk/wiringpi-migration/ and is
# deliberately not asserted here, so that directory can be deleted with a plain
# git rm and nothing else. Its catalog records what each file proved.
if [[ -d misc/junk/wiringpi-migration ]]; then
    grep -Fq 'wiringpi-migration' misc/junk/wiringpi-migration/CATALOG.md
fi

echo "application compatibility tests passed"
