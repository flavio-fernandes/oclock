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

# Preserve current application/service paths while the final Trixie deployment
# unit is still being designed. Hardware transport selection is tested in the
# boundary suite.
grep -qx 'After=network.target' misc/oclock.service
grep -qx 'ExecStart=/home/pi/oclock.git/oclock' misc/oclock.service
grep -qx 'StandardOutput=null' misc/oclock.service
grep -qx 'Alias=oclock.service' misc/oclock.service
make -n CC=modern-cxx hardware >"${test_dir}/compiler.txt"
grep -q 'modern-cxx -c' "${test_dir}/compiler.txt"

# Existing clients may abbreviate display POST keys; preserve that behavior.
grep -Fq 'strncasecmp(key, "msg", strlen(key)) == 0' src/displayInternal.cpp
grep -Fq 'strncasecmp(key, "animationStep", strlen(key)) == 0' src/displayInternal.cpp

# The SPI collector is also metadata-only. Its tool check must use commands
# whose successful exit status is stable on the selected raspi-utils release;
# bare `dtoverlay -h` prints help but deliberately returns 1 there.
bash -n misc/collectPhase5SpiTarget.sh
misc/collectPhase5SpiTarget.sh --help >"${test_dir}/phase5-spi-help.txt"
grep -Fq 'dtoverlay-list-active.txt' misc/collectPhase5SpiTarget.sh
grep -Fq 'dtoverlay-list-available.txt' misc/collectPhase5SpiTarget.sh
grep -Fq 'CONFIG_MCP320X' misc/collectPhase5SpiTarget.sh
grep -Fq 'mcp320x-modinfo' misc/collectPhase5SpiTarget.sh
if grep -Eq '(^|[[:space:]])(gpioget|gpioset|gpiomon|gpionotify)([[:space:]]|$)' \
        misc/collectPhase5SpiTarget.sh; then
    echo "SPI collector contains a GPIO line-access command" >&2
    exit 1
fi

bash -n misc/verifyPhase5SpiOverlayDryRun.sh
misc/verifyPhase5SpiOverlayDryRun.sh --help \
    >"${test_dir}/phase5-spi-dry-run-help.txt"
grep -Fq 'Nothing is applied to the live tree' \
    misc/verifyPhase5SpiOverlayDryRun.sh
grep -Fq 'compatible = "flaviof,oclock-lpd8806"' \
    hardware/oclock-spi-overlay.dts
grep -Fq 'compatible = "microchip,mcp3002"' \
    hardware/oclock-spi-overlay.dts

bash -n misc/managePhase5SpiOverlay.sh
misc/managePhase5SpiOverlay.sh --help \
    >"${test_dir}/phase5-spi-manager-help.txt"
grep -Fq '53b593f4c30a78c8beb446cf506816af29a817d0daf93bb899a9054c7aa61959' \
    misc/managePhase5SpiOverlay.sh
grep -Fq 'backup is not byte-identical' misc/managePhase5SpiOverlay.sh
grep -Fq 'does not reboot' "${test_dir}/phase5-spi-manager-help.txt"

bash -n misc/collectPhase5SpiOverlayBoot.sh
misc/collectPhase5SpiOverlayBoot.sh --help \
    >"${test_dir}/phase5-spi-boot-help.txt"
grep -Fq 'No spidev binding or hardware' \
    "${test_dir}/phase5-spi-boot-help.txt"
if grep -Eq '^[[:space:]]*(modprobe|dtoverlay|reboot|shutdown|gpioset|gpioget|gpiomon)[[:space:]]' \
        misc/collectPhase5SpiOverlayBoot.sh; then
    echo "live SPI collector contains a state-changing or GPIO-access command" >&2
    exit 1
fi

# The binding manager may change only the deliberately unbound strip child's
# runtime driver. It must discover dynamic SPI names from Device Tree, retain
# the native ADC binding, and offer explicit rollback. The paired collector is
# metadata-only and must never perform the binding or open the device.
bash -n misc/managePhase5Lpd8806Binding.sh
misc/managePhase5Lpd8806Binding.sh --help \
    >"${test_dir}/phase5-lpd-bind-manager-help.txt"
grep -Fq 'runtime binding does not survive a' \
    "${test_dir}/phase5-lpd-bind-manager-help.txt"
grep -Fq '*/oclock-strip-spi/lpd8806@0' \
    misc/managePhase5Lpd8806Binding.sh
grep -Fq '*/oclock-adc-spi/mcp3002@0' \
    misc/managePhase5Lpd8806Binding.sh
grep -Fq 'MCP3002 SPI child is not bound to mcp320x' \
    misc/managePhase5Lpd8806Binding.sh
grep -Fq 'Incomplete strip binding was rolled back.' \
    misc/managePhase5Lpd8806Binding.sh
if grep -Fq 'spi4.0' misc/managePhase5Lpd8806Binding.sh; then
    echo "LPD8806 binding manager hard-codes a dynamic SPI device" >&2
    exit 1
fi

bash -n misc/collectPhase5Lpd8806Binding.sh
misc/collectPhase5Lpd8806Binding.sh --help \
    >"${test_dir}/phase5-lpd-bind-collector-help.txt"
grep -Fq 'No device is opened' \
    "${test_dir}/phase5-lpd-bind-collector-help.txt"
if grep -Eq '^[[:space:]]*(modprobe|dtoverlay|reboot|shutdown|gpioset|gpioget|gpiomon)[[:space:]]' \
        misc/collectPhase5Lpd8806Binding.sh; then
    echo "LPD8806 binding collector contains a state-changing command" >&2
    exit 1
fi
if grep -Eq '(^|[[:space:]])(driver_override|/sys/bus/spi/drivers/[^[:space:]]+/(bind|unbind))[[:space:]]*>' \
        misc/collectPhase5Lpd8806Binding.sh; then
    echo "LPD8806 binding collector writes a driver control" >&2
    exit 1
fi

bash -n misc/collectPhase5Lpd8806Build.sh
misc/collectPhase5Lpd8806Build.sh --help \
    >"${test_dir}/phase5-lpd-build-help.txt"
grep -Fq 'binary is never executed' \
    "${test_dir}/phase5-lpd-build-help.txt"
grep -Fq '/oclock-strip-spi/lpd8806@0' \
    misc/collectPhase5Lpd8806Build.sh
if grep -Eq '^[[:space:]]*(modprobe|dtoverlay|reboot|shutdown|gpioset|gpioget|gpiomon)[[:space:]]' \
        misc/collectPhase5Lpd8806Build.sh; then
    echo "native LPD8806 build collector contains a state-changing command" >&2
    exit 1
fi

# The first-transfer gate must use the standalone all-off tool, retain an
# explicit human confirmation, and make unbind part of its exit path. It must
# never use the partial application as a transfer vehicle.
bash -n misc/verifyPhase5Lpd8806FirstTransfer.sh
misc/verifyPhase5Lpd8806FirstTransfer.sh --help \
    >"${test_dir}/phase5-lpd-transfer-help.txt"
grep -Fq 'Type TRANSFER' misc/verifyPhase5Lpd8806FirstTransfer.sh
grep -Fq 'Emergency rollback: unbinding the strip' \
    misc/verifyPhase5Lpd8806FirstTransfer.sh
grep -Fq 'timeout --signal=TERM --kill-after=5s 15s "${transfer_tool}"' \
    misc/verifyPhase5Lpd8806FirstTransfer.sh
grep -Fq 'runtime strip binding was removed before the operator prompt' \
    misc/verifyPhase5Lpd8806FirstTransfer.sh
grep -Fq 'const Int16U ledCount = 240;' \
    misc/phase5Lpd8806AllOff.cpp
grep -Fq 'strip.show();' misc/phase5Lpd8806AllOff.cpp
grep -Fq 'OCLOCK_STRIP_SPEED_HZ 2000000U' src/spi/StripSpeed.h
make -n phase5-lpd8806-all-off-2mhz \
    >"${test_dir}/phase5-lpd-2mhz-build.txt"
grep -Fq -- '-DOCLOCK_STRIP_SPEED_HZ=2000000U' \
    "${test_dir}/phase5-lpd-2mhz-build.txt"
# The historical 1 MHz helper must stay pinned so the rejected profile remains
# reproducible now that production no longer defaults to it.
make -n phase5-lpd8806-all-off \
    >"${test_dir}/phase5-lpd-1mhz-build.txt"
grep -Fq -- '-DOCLOCK_STRIP_SPEED_HZ=1000000U' \
    "${test_dir}/phase5-lpd-1mhz-build.txt"
grep -Fq -- '--speed-hz must be 1000000 or the reviewed 2000000 experiment' \
    misc/verifyPhase5Lpd8806FirstTransfer.sh
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
bash -n misc/verifyPhase5Mcp3002FirstRead.sh
misc/verifyPhase5Mcp3002FirstRead.sh --help \
    >"${test_dir}/phase5-mcp3002-read-help.txt"
grep -Fq 'Type READ' misc/verifyPhase5Mcp3002FirstRead.sh
grep -Fq 'runuser -u "${operator}" -- "${tool}"' \
    misc/verifyPhase5Mcp3002FirstRead.sh
grep -Fq 'No calibration decision or whole-application run was included.' \
    misc/verifyPhase5Mcp3002FirstRead.sh
if grep -Eq '(^|[[:space:]])(gpioget|gpioset|gpiomon|gpionotify)[[:space:]]' \
        misc/verifyPhase5Mcp3002FirstRead.sh; then
    echo "MCP3002 verifier contains a GPIO line-access command" >&2
    exit 1
fi
bash -n misc/collectPhase5Mcp3002Calibration.sh
misc/collectPhase5Mcp3002Calibration.sh --help \
    >"${test_dir}/phase5-mcp3002-calibration-help.txt"
grep -Fq 'samples_per_window=10' misc/collectPhase5Mcp3002Calibration.sh
grep -Fq 'sample_interval=0.6' misc/collectPhase5Mcp3002Calibration.sh
grep -Fq 'Type BASELINE' misc/collectPhase5Mcp3002Calibration.sh
grep -Fq 'Type COVERED' misc/collectPhase5Mcp3002Calibration.sh
grep -Fq 'Type RESTORED' misc/collectPhase5Mcp3002Calibration.sh
grep -Fq 'It does not change or approve dimming thresholds.' \
    misc/collectPhase5Mcp3002Calibration.sh
if grep -Eq '(^|[[:space:]])(gpioget|gpioset|gpiomon|gpionotify)[[:space:]]' \
        misc/collectPhase5Mcp3002Calibration.sh; then
    echo "MCP3002 calibration collector contains a GPIO line-access command" >&2
    exit 1
fi
if grep -Fq 'strip.begin();' misc/phase5Lpd8806AllOff.cpp; then
    echo "all-off tool contains an extra initial latch transfer" >&2
    exit 1
fi
bash -n misc/verifyPhase5Lpd8806Cadence.sh
misc/verifyPhase5Lpd8806Cadence.sh --help \
    >"${test_dir}/phase5-lpd-cadence-help.txt"
grep -Fq 'frame_count=25' misc/verifyPhase5Lpd8806Cadence.sh
grep -Fq 'tick_budget_microseconds=12000' \
    misc/verifyPhase5Lpd8806Cadence.sh
grep -Fq 'Type BENCHMARK' misc/verifyPhase5Lpd8806Cadence.sh
grep -Fq -- '--speed-hz must be 1000000 or the reviewed 2000000 experiment' \
    misc/verifyPhase5Lpd8806Cadence.sh
grep -Fq 'Emergency rollback: unbinding the strip' \
    misc/verifyPhase5Lpd8806Cadence.sh
grep -Fq 'Did the entire LED strip remain off and stable?' \
    misc/verifyPhase5Lpd8806Cadence.sh
grep -Fq 'The whole application and ADC were not run.' \
    misc/verifyPhase5Lpd8806Cadence.sh

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

echo "application compatibility tests passed"
