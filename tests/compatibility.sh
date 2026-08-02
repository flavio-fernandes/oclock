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

# Preserve the service and plain `make` deployment contracts used on Jessie.
grep -qx 'After=network.target' misc/oclock.service
grep -qx 'ExecStart=/home/pi/oclock.git/oclock' misc/oclock.service
grep -qx 'StandardOutput=null' misc/oclock.service
grep -qx 'Alias=oclock.service' misc/oclock.service
make -n sudo_oclock >"${test_dir}/make.txt"
grep -q 'sudo chown root:root oclock' "${test_dir}/make.txt"
grep -q 'sudo chmod u+s oclock' "${test_dir}/make.txt"
make -n CC=legacy-cxx hardware >"${test_dir}/compiler.txt"
grep -q 'legacy-cxx -c' "${test_dir}/compiler.txt"

# Existing clients may abbreviate display POST keys; preserve that behavior.
grep -Fq 'strncasecmp(key, "msg", strlen(key)) == 0' src/displayInternal.cpp
grep -Fq 'strncasecmp(key, "animationStep", strlen(key)) == 0' src/displayInternal.cpp

# Keep the Phase 0 hardware collector runnable on the legacy Bash environment
# without executing its production-only collection path in the sandbox.
bash -n misc/collectHardwareBaseline.sh
misc/collectHardwareBaseline.sh --help >"${test_dir}/collector-help.txt"
grep -q -- '--duration SECONDS' "${test_dir}/collector-help.txt"
grep -q -- '--binary PATH' "${test_dir}/collector-help.txt"
if misc/collectHardwareBaseline.sh --duration 0 \
        >"${test_dir}/collector-invalid.txt" 2>&1; then
    echo "hardware collector accepted an invalid duration" >&2
    exit 1
fi

# Keep the guarded Phase 1 verifier parseable on Jessie without allowing its
# help and argument checks to touch systemd or GPIO.
bash -n misc/verifyPhase1Hardware.sh
misc/verifyPhase1Hardware.sh --help >"${test_dir}/phase1-help.txt"
grep -q -- '--binary PATH' "${test_dir}/phase1-help.txt"
grep -q -- '--commit SHA' "${test_dir}/phase1-help.txt"
# Dependency inspection must consume complete command output before matching.
# A `grep -q` pipeline can make the producer receive SIGPIPE under pipefail.
grep -Fq 'candidate_dependencies=$(ldd "${binary_path}" 2>&1)' \
    misc/verifyPhase1Hardware.sh
# Prompts run inside command substitutions, so they must not contaminate the
# answer returned on stdout.
grep -Fq "printf '%s [yes/no]: ' \"\${prompt}\" >&2" \
    misc/verifyPhase1Hardware.sh
# The compatibility run must expose the same HTTP endpoint as production so
# the existing external controller can reach it.
grep -q '^bind_address=0\.0\.0\.0$' misc/verifyPhase1Hardware.sh
grep -q '^port=80$' misc/verifyPhase1Hardware.sh
grep -Fq 'candidate connected to the configured MQTT broker' \
    misc/verifyPhase1Hardware.sh
if misc/verifyPhase1Hardware.sh --binary /missing --commit invalid \
        >"${test_dir}/phase1-invalid.txt" 2>&1; then
    echo "Phase 1 verifier accepted invalid arguments" >&2
    exit 1
fi

# The Phase 3 target collector must remain parseable on both the legacy host
# and the candidate image. Its inspection path must not request or drive GPIO.
bash -n misc/collectGpioTarget.sh
misc/collectGpioTarget.sh --help >"${test_dir}/phase3-help.txt"
grep -q -- '--debian-version VERSION' "${test_dir}/phase3-help.txt"
grep -q -- '--libgpiod-major VERSION' "${test_dir}/phase3-help.txt"
if misc/collectGpioTarget.sh --libgpiod-major invalid \
        >"${test_dir}/phase3-invalid.txt" 2>&1; then
    echo "Phase 3 collector accepted an invalid libgpiod major" >&2
    exit 1
fi
grep -q 'O_RDONLY | O_CLOEXEC' misc/collectGpioTarget.sh
grep -q 'GPIO_V2_GET_LINEINFO_IOCTL' misc/collectGpioTarget.sh
if grep -Eq '(^|[[:space:]])(gpioget|gpioset|gpiomon|gpionotify)([[:space:]]|$)' \
        misc/collectGpioTarget.sh; then
    echo "Phase 3 collector contains a GPIO line-access command" >&2
    exit 1
fi
# Raspbian qualifies Multi-Arch package names even when armhf is the native
# architecture. Accept both Debian's unqualified and Raspbian's qualified form.
grep -Fq "libgpiod-dev(:armhf)?" misc/collectGpioTarget.sh
printf 'libgpiod-dev:armhf\t2.2.1-2+rpi1+deb13u1\tarmhf\tinstall ok installed\n' \
    >"${test_dir}/phase3-packages.txt"
grep -Eq $'^libgpiod-dev(:armhf)?\t.*\tarmhf\tinstall ok installed$' \
    "${test_dir}/phase3-packages.txt"
grep -Fq "grep -q '^throttled=0x0$'" misc/collectGpioTarget.sh

# Keep the Phase 5 verifier's safety boundary executable without entering its
# hardware path during host tests.
bash -n misc/verifyPhase5GpiodHardware.sh
misc/verifyPhase5GpiodHardware.sh --help >"${test_dir}/phase5-help.txt"
grep -q -- '--sha256 SHA256' "${test_dir}/phase5-help.txt"
if misc/verifyPhase5GpiodHardware.sh --binary \
        >"${test_dir}/phase5-invalid.txt" 2>&1; then
    echo "Phase 5 verifier accepted a missing option value" >&2
    exit 1
fi
grep -Fq 'Raspberry Pi Zero W Rev 1.1' misc/verifyPhase5GpiodHardware.sh
grep -Fq '[[ ${revision} == 9000c1 ]]' misc/verifyPhase5GpiodHardware.sh
grep -Fq "wifi:connected" misc/verifyPhase5GpiodHardware.sh
grep -Fq "grep -q 'libgpiod\\.so\\.3 =>'" \
    misc/verifyPhase5GpiodHardware.sh
grep -Fq "! grep -q 'libwiringPi'" misc/verifyPhase5GpiodHardware.sh
grep -Fq 'trap cleanup EXIT' misc/verifyPhase5GpiodHardware.sh
if grep -Eq '(^|[[:space:]])(gpioget|gpioset|gpiomon|gpionotify)([[:space:]]|$)' \
        misc/verifyPhase5GpiodHardware.sh; then
    echo "Phase 5 verifier invokes an unreviewed GPIO line command" >&2
    exit 1
fi

# The fast-GPIO target collector is metadata-only and must remain safe to
# extract and run before any experimental backend drives the harness.
bash -n misc/collectPhase5FastGpioTarget.sh
misc/collectPhase5FastGpioTarget.sh --help >"${test_dir}/fastgpio-help.txt"
grep -q 'collector is read-only' "${test_dir}/fastgpio-help.txt"
grep -q 'PROT_READ, MAP_SHARED' misc/collectPhase5FastGpioTarget.sh
if grep -Eq '(^|[[:space:]])(gpioget|gpioset|gpiomon|gpionotify)([[:space:]]|$)' \
        misc/collectPhase5FastGpioTarget.sh; then
    echo "fast-GPIO collector contains a GPIO line-access command" >&2
    exit 1
fi

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

echo "legacy compatibility tests passed"
