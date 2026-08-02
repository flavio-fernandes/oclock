#!/usr/bin/env bash

# Read each MCP3002 single-ended IIO channel once through the exact application
# input implementation. This gate never starts oclock, binds a driver, requests
# a GPIO line, writes a boot file, or touches the LPD8806 character device.

set -euo pipefail

accepted_overlay_sha=53b593f4c30a78c8beb446cf506816af29a817d0daf93bb899a9054c7aa61959
adc_dt_suffix=/oclock-adc-spi/mcp3002@0
strip_dt_suffix=/oclock-strip-spi/lpd8806@0
tool=
commit=

usage() {
    cat <<'EOF'
Usage: sudo misc/verifyPhase5Mcp3002FirstRead.sh \
  --tool PATH --commit GIT_COMMIT

Requires the accepted Office Clock overlay, inactive oclock.service, native
MCP3002/mcp320x binding, and an unbound strip. After the explicit READ prompt,
the standalone application reader reads channel 0 and channel 1 once each.
No calibration decision or whole-application run is included.
EOF
}

die() {
    echo "error: $*" >&2
    exit 1
}

while (($# > 0)); do
    case "$1" in
        --tool)
            (($# >= 2)) || die "--tool requires a value"
            tool=$2
            shift 2
            ;;
        --commit)
            (($# >= 2)) || die "--commit requires a value"
            commit=$2
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
done

[[ ${EUID} -eq 0 ]] || die "run this verifier with sudo"
[[ -n ${tool} && -n ${commit} ]] || {
    usage >&2
    exit 2
}
[[ ${commit} =~ ^[0-9a-f]{40}$ ]] || die "commit must be a full Git SHA"
tool=$(realpath "${tool}")
[[ -x ${tool} ]] || die "reader is not executable: ${tool}"

model=$(tr -d '\0' </proc/device-tree/model 2>/dev/null || true)
[[ ${model} == "Raspberry Pi Zero W Rev 1.1" ]] ||
    die "target is not the accepted Raspberry Pi Zero W revision"
[[ $(uname -m) == armv6l ]] || die "target is not ARMv6"
[[ $(dpkg --print-architecture) == armhf ]] || die "target is not armhf"
# This fixed system file is present on the selected target.
# shellcheck disable=SC1091
. /etc/os-release
[[ ${VERSION_CODENAME:-} == trixie ]] || die "target is not Trixie"
systemctl is-active --quiet oclock && die "oclock.service must be inactive"

overlay=/boot/firmware/overlays/oclock-spi.dtbo
[[ -f ${overlay} ]] || die "accepted Office Clock overlay is not installed"
[[ $(sha256sum "${overlay}" | awk '{print $1}') == \
   "${accepted_overlay_sha}" ]] || die "installed overlay checksum differs"

find_spi_child() {
    local suffix=$1
    local -a matches=()
    local device node
    for device in /sys/bus/spi/devices/spi*; do
        [[ -e ${device} ]] || continue
        node=$(readlink -f "${device}/of_node" 2>/dev/null || true)
        [[ ${node} == *"${suffix}" ]] && matches+=("${device}")
    done
    ((${#matches[@]} == 1)) ||
        die "expected one SPI child ending in ${suffix}; found ${#matches[@]}"
    printf '%s\n' "${matches[0]}"
}

adc_spi=$(find_spi_child "${adc_dt_suffix}")
strip_spi=$(find_spi_child "${strip_dt_suffix}")
[[ $(basename "$(readlink -f "${adc_spi}/driver")") == mcp320x ]] ||
    die "MCP3002 is not bound to mcp320x"
[[ ! -e ${strip_spi}/driver ]] || die "strip must remain unbound"

iio_matches=()
for device in /sys/bus/iio/devices/iio:device*; do
    [[ -e ${device} ]] || continue
    node=$(readlink -f "${device}/of_node" 2>/dev/null || true)
    [[ ${node} == *"${adc_dt_suffix}" ]] && iio_matches+=("${device}")
done
((${#iio_matches[@]} == 1)) ||
    die "expected one MCP3002 IIO device; found ${#iio_matches[@]}"
iio_device=${iio_matches[0]}
[[ $(tr -d '\n' <"${iio_device}/name") == mcp3002 ]] ||
    die "IIO device name is not mcp3002"

operator=${SUDO_USER:-root}
for channel in 0 1; do
    attribute=${iio_device}/in_voltage${channel}_raw
    [[ -e ${attribute} ]] || die "IIO channel ${channel} attribute is missing"
    runuser -u "${operator}" -- test -r "${attribute}" ||
        die "IIO channel ${channel} is not readable by ${operator}"
done

file "${tool}" | grep -Fq 'ELF 32-bit LSB executable, ARM, EABI5' ||
    die "reader is not an ARM EABI5 executable"
if ldd "${tool}" | grep -q libwiringPi; then
    die "reader unexpectedly resolves WiringPi"
fi
strings "${tool}" | grep -Fq "${adc_dt_suffix}" ||
    die "reader lacks Device Tree identity discovery"

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
output_dir=$(mktemp -d "/tmp/oclock-phase5-mcp3002-read-${timestamp}-XXXXXXXX")
result_file=${output_dir}/result.txt

capture_state() {
    local destination=$1
    {
        printf 'adc_spi_child=%s\n' "$(basename "${adc_spi}")"
        printf 'adc_driver=%s\n' \
            "$(basename "$(readlink -f "${adc_spi}/driver")")"
        printf 'iio_device=%s\n' "$(basename "${iio_device}")"
        printf 'iio_name=%s\n' "$(tr -d '\n' <"${iio_device}/name")"
        if [[ -e ${strip_spi}/driver ]]; then
            printf 'strip_driver=%s\n' \
                "$(basename "$(readlink -f "${strip_spi}/driver")")"
        else
            echo 'strip_driver=none'
        fi
        printf 'service_active=%s\n' \
            "$(systemctl is-active oclock 2>/dev/null || true)"
    } >"${destination}"
}

{
    printf 'captured_at_utc=%s\n' "${timestamp}"
    printf 'source_commit=%s\n' "${commit}"
    printf 'model=%s\n' "${model}"
    printf 'architecture=%s/%s\n' "$(uname -m)" "$(dpkg --print-architecture)"
    printf 'os_codename=%s\n' "${VERSION_CODENAME}"
} >"${output_dir}/context.txt"
file "${tool}" >"${output_dir}/tool-file.txt"
ldd "${tool}" >"${output_dir}/tool-ldd.txt"
sha256sum "${tool}" >"${output_dir}/tool.sha256"
capture_state "${output_dir}/state-before.txt"
vcgencmd get_throttled >"${output_dir}/throttling-before.txt" 2>&1 || true

echo "Candidate reader:  ${tool}"
echo "Candidate commit:  ${commit}"
echo "Authorized access: one raw read from MCP3002 channel 0 and channel 1"
echo
echo "No GPIO line, SPI binding, strip device, service, or boot file will change."
printf 'Type READ to continue: '
read -r answer
[[ ${answer} == READ ]] || die "read was not authorized"

read_status=0
timeout --signal=TERM --kill-after=2s 10s \
    runuser -u "${operator}" -- "${tool}" \
    >"${output_dir}/read.txt" 2>"${output_dir}/read-stderr.txt" ||
    read_status=$?

capture_state "${output_dir}/state-after.txt"
vcgencmd get_throttled >"${output_dir}/throttling-after.txt" 2>&1 || true

failures=0
warnings=0
ok() {
    echo "OK: $*" | tee -a "${result_file}"
}
fail() {
    echo "FAIL: $*" | tee -a "${result_file}"
    failures=$((failures + 1))
}
warn() {
    echo "WARNING: $*" | tee -a "${result_file}"
    warnings=$((warnings + 1))
}

echo "Phase 5 MCP3002 native-IIO first-read checks" >"${result_file}"
ok "target is the selected Zero W/Trixie ARMv6 image"
ok "installed overlay matches the accepted artifact"
ok "oclock.service was inactive before the read"
ok "reader is ARM EABI5 and has no WiringPi dependency"
ok "MCP3002 was discovered by Device Tree identity"
ok "MCP3002 is bound to mcp320x"
ok "both single-ended raw attributes are readable by the invoking user"

if ((read_status == 0)); then
    ok "standalone IIO reader returned success"
else
    fail "standalone IIO reader returned status ${read_status}"
fi

for channel in 0 1; do
    value=$(sed -n "s/^channel${channel}_raw=//p" "${output_dir}/read.txt")
    if [[ ${value} =~ ^[0-9]+$ ]] && ((value <= 1023)); then
        ok "channel ${channel} returned a 10-bit raw value"
    else
        fail "channel ${channel} did not return a valid 10-bit raw value"
    fi
done
if grep -Eq '^read_pair_elapsed_microseconds=[1-9][0-9]*$' \
        "${output_dir}/read.txt"; then
    ok "two-channel read wall time was captured"
else
    fail "two-channel read wall time is missing or invalid"
fi
if cmp -s "${output_dir}/state-before.txt" "${output_dir}/state-after.txt"; then
    ok "ADC, strip, and service state did not change"
else
    fail "hardware or service state changed during the read"
fi
if grep -Fqx 'throttled=0x0' "${output_dir}/throttling-after.txt"; then
    ok "firmware reported no current or historical throttling"
else
    warn "review the captured firmware throttling result"
fi

{
    echo
    echo "failures: ${failures}"
    echo "warnings: ${warnings}"
    echo
    echo "Exactly one read of each single-ended MCP3002 channel was attempted."
    echo "No calibration decision or whole-application run was included."
} >>"${result_file}"

cat "${result_file}"

archive=${output_dir}.tar.gz
tar -czf "${archive}" -C /tmp "$(basename "${output_dir}")"
(
    cd /tmp
    sha256sum "$(basename "${archive}")" \
        >"$(basename "${archive}").sha256"
)
chmod 0644 "${archive}" "${archive}.sha256"
if [[ -n ${SUDO_UID:-} && -n ${SUDO_GID:-} ]]; then
    chown "${SUDO_UID}:${SUDO_GID}" "${archive}" "${archive}.sha256"
fi

echo
echo "Share this archive: ${archive}"
echo "Archive checksum:   ${archive}.sha256"
echo "Result directory:   ${output_dir}"

((failures == 0)) || die "MCP3002 first-read gate recorded ${failures} failure(s)"
