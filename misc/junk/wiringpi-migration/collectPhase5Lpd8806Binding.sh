#!/bin/bash

# Inspect the temporary LPD8806 spidev binding without opening the device or
# performing a transfer.

set -u
set -o pipefail

output_dir=
expected_model="Raspberry Pi Zero W Rev 1.1"
expected_revision=9000c1
expected_overlay_sha256="53b593f4c30a78c8beb446cf506816af29a817d0daf93bb899a9054c7aa61959"
installed_overlay=/boot/firmware/overlays/oclock-spi.dtbo

usage()
{
    cat <<EOF
Usage: sudo $0 [--output DIRECTORY]

Collect the exact LPD8806 SPI child, spidev driver override, character-device
metadata, GPIO ownership, native MCP3002 binding, and inactive service state.
No device is opened and no SPI transfer or IIO read is performed.
EOF
}

die()
{
    echo "error: $*" >&2
    exit 2
}

while (($# > 0)); do
    case "$1" in
        --output)
            (($# >= 2)) || die "--output requires a value"
            output_dir=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

((EUID == 0)) || die "run this collector with sudo"

for command_name in awk basename cat chown date dirname dpkg find gpiodetect \
        gpioinfo grep head id ls lsmod mkdir mktemp od readlink sed sha256sum \
        sort stat systemctl tail tar tr uname; do
    command -v "${command_name}" >/dev/null 2>&1 ||
        die "required command is missing: ${command_name}"
done

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
umask 077
if [[ -z ${output_dir} ]]; then
    output_dir=$(mktemp -d "/tmp/oclock-phase5-lpd-bind-${timestamp}-XXXXXXXX") ||
        die "cannot create result directory"
else
    [[ ${output_dir} != / ]] || die "unsafe output directory"
    mkdir -p "$(dirname "${output_dir}")" || die "cannot create output parent"
    mkdir "${output_dir}" ||
        die "output directory already exists or cannot be created"
fi

archive_path="${output_dir%/}.tar.gz"
result_file="${output_dir}/result.txt"
failures=0

finish()
{
    local rc=$?
    set +e
    if [[ -d ${output_dir} ]]; then
        if tar -czf "${archive_path}" -C "$(dirname "${output_dir}")" \
                "$(basename "${output_dir}")" && (
            cd "$(dirname "${archive_path}")" || exit 1
            sha256sum "$(basename "${archive_path}")" \
                >"$(basename "${archive_path}").sha256"
        ); then
            if [[ -n ${SUDO_UID:-} && -n ${SUDO_GID:-} ]]; then
                chown "${SUDO_UID}:${SUDO_GID}" "${archive_path}" \
                    "${archive_path}.sha256" 2>/dev/null || true
            fi
            echo "Share this archive: ${archive_path}" >&2
            echo "Archive checksum:   ${archive_path}.sha256" >&2
        else
            rc=1
        fi
        echo "Result directory:   ${output_dir}" >&2
    fi
    exit "${rc}"
}
trap finish EXIT

ok()
{
    echo "OK: $*" >>"${result_file}"
}

fail()
{
    echo "FAIL: $*" >>"${result_file}"
    failures=$((failures + 1))
}

model=unknown
if [[ -r /proc/device-tree/model ]]; then
    model=$(tr -d '\0' </proc/device-tree/model)
fi
revision=$(awk -F: '/^Revision/ {
    gsub(/[[:space:]]/, "", $2); print tolower($2)
}' /proc/cpuinfo | tail -1)
machine=$(uname -m)
architecture=$(dpkg --print-architecture)
os_codename=unknown
if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    source /etc/os-release
    os_codename=${VERSION_CODENAME:-unknown}
fi

{
    echo "Office Clock Phase 5 LPD8806 spidev binding inspection"
    echo
    echo "collected_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "model: ${model}"
    echo "revision: ${revision:-unknown}"
    echo "kernel: $(uname -srvm)"
    echo "machine: ${machine}"
    echo "architecture: ${architecture}"
    echo "os_codename: ${os_codename}"
} >"${output_dir}/context.txt"

{
    echo "This collector is metadata-only."
    echo "It does not open /dev/spidev*, issue an ioctl, transfer a byte,"
    echo "read IIO, request GPIO, bind/unbind a driver, or change service/boot state."
} >"${output_dir}/safety.txt"

strip_devices=()
adc_devices=()
shopt -s nullglob
for device_path in /sys/bus/spi/devices/spi*.*; do
    [[ -e ${device_path} ]] || continue
    resolved_node=$(readlink -f "${device_path}/of_node" 2>/dev/null || true)
    case "${resolved_node}" in
        */oclock-strip-spi/lpd8806@0)
            strip_devices+=("${device_path}")
            ;;
        */oclock-adc-spi/mcp3002@0)
            adc_devices+=("${device_path}")
            ;;
    esac
done

strip_device=
strip_name=
spidev_node=
if ((${#strip_devices[@]} == 1)); then
    strip_device=${strip_devices[0]}
    strip_name=$(basename "${strip_device}")
    spidev_node="/dev/spidev${strip_name#spi}"
fi
adc_device=
if ((${#adc_devices[@]} == 1)); then
    adc_device=${adc_devices[0]}
fi

{
    echo "strip_matches=${#strip_devices[@]}"
    if [[ -n ${strip_device} ]]; then
        echo "strip_device=${strip_device}"
        echo "strip_resolved=$(readlink -f "${strip_device}")"
        echo "strip_of_node=$(readlink -f "${strip_device}/of_node")"
        echo "driver_override=$(cat "${strip_device}/driver_override" 2>/dev/null || true)"
        if [[ -L ${strip_device}/driver ]]; then
            echo "driver=$(basename "$(readlink -f "${strip_device}/driver")")"
        else
            echo "driver=none"
        fi
        echo "character_device=${spidev_node}"
        stat -c 'type=%F mode=%a owner=%U group=%G major_minor=%t:%T' \
            "${spidev_node}" 2>&1 || true
    fi
    echo
    echo "adc_matches=${#adc_devices[@]}"
    if [[ -n ${adc_device} ]]; then
        echo "adc_device=${adc_device}"
        echo "adc_of_node=$(readlink -f "${adc_device}/of_node")"
        if [[ -L ${adc_device}/driver ]]; then
            echo "adc_driver=$(basename "$(readlink -f "${adc_device}/driver")")"
        else
            echo "adc_driver=none"
        fi
    fi
    echo
    echo "No character device or IIO attribute was opened."
} >"${output_dir}/binding.txt" 2>&1

{
    shopt -s nullglob
    nodes=(/dev/spidev*)
    if ((${#nodes[@]} == 0)); then
        echo "No /dev/spidev* nodes found"
    else
        stat -c 'path=%n type=%F mode=%a owner=%U group=%G major_minor=%t:%T' \
            "${nodes[@]}"
    fi
    echo "No node above was opened."
} >"${output_dir}/spidev-nodes.txt" 2>&1

{
    lsmod | grep -E '^(spi_gpio|spi_bitbang|spidev|mcp320x|industrialio)[[:space:]]' || true
} >"${output_dir}/loaded-relevant-modules.txt" 2>&1

gpiodetect >"${output_dir}/gpiodetect.txt" 2>&1
broadcom_chip=$(awk \
    '/^[[:space:]]*gpiochip[0-9]+[[:space:]]+\[pinctrl-bcm2835\]/ { print $1; exit }' \
    "${output_dir}/gpiodetect.txt")
if [[ ${broadcom_chip} =~ ^gpiochip[0-9]+$ ]]; then
    gpiod_version=$(gpiodetect --version 2>/dev/null |
        sed -nE 's/.*[^0-9]([0-9]+\.[0-9]+(\.[0-9]+)?).*/\1/p' |
        head -1)
    if [[ ${gpiod_version%%.*} == 2 ]]; then
        gpioinfo -c "${broadcom_chip}" \
            >"${output_dir}/gpioinfo-selected-chip.txt" 2>&1 || true
    else
        gpioinfo "${broadcom_chip}" \
            >"${output_dir}/gpioinfo-selected-chip.txt" 2>&1 || true
    fi
else
    echo "Broadcom GPIO chip is unavailable" \
        >"${output_dir}/gpioinfo-selected-chip.txt"
fi

{
    for offset in 20 21; do
        line=$(grep -E "line[[:space:]]+${offset}:" \
            "${output_dir}/gpioinfo-selected-chip.txt" | head -1 || true)
        echo "offset ${offset}: ${line:-NOT FOUND}"
    done
} >"${output_dir}/gpioinfo-strip-lines.txt"

systemctl show oclock -p LoadState -p ActiveState -p SubState -p MainPID \
    -p FragmentPath >"${output_dir}/service-properties.txt" 2>&1

echo "Phase 5 LPD8806 spidev binding checks" >"${result_file}"

if [[ ${model} == "${expected_model}" &&
      ${revision:-unknown} == "${expected_revision}" &&
      ${machine} == armv6l && ${architecture} == armhf &&
      ${os_codename} == trixie ]]; then
    ok "target is the selected Zero W/Trixie ARMv6 image"
else
    fail "target identity does not match the selected Zero W/Trixie image"
fi

if systemctl is-active --quiet oclock; then
    fail "oclock.service is active"
else
    ok "oclock.service is inactive"
fi

if [[ -f ${installed_overlay} ]] &&
        printf '%s  %s\n' "${expected_overlay_sha256}" \
            "${installed_overlay}" | sha256sum --check --status; then
    ok "installed overlay matches the accepted artifact"
else
    fail "installed overlay is missing or unexpected"
fi

if ((${#strip_devices[@]} == 1)); then
    ok "exactly one LPD8806 SPI child was discovered by Device Tree path"
else
    fail "expected one LPD8806 SPI child; found ${#strip_devices[@]}"
fi

if [[ -n ${strip_device} && -L ${strip_device}/driver &&
      $(basename "$(readlink -f "${strip_device}/driver")") == spidev &&
      $(cat "${strip_device}/driver_override" 2>/dev/null || true) == spidev ]]; then
    ok "LPD8806 child is explicitly bound to spidev"
else
    fail "LPD8806 child is not explicitly bound to spidev"
fi

if [[ -n ${spidev_node} && -c ${spidev_node} ]]; then
    ok "LPD8806 spidev character device exists"
else
    fail "LPD8806 spidev character device is missing"
fi

if ((${#adc_devices[@]} == 1)) && [[ -L ${adc_device}/driver ]] &&
      [[ $(basename "$(readlink -f "${adc_device}/driver")") == mcp320x ]]; then
    ok "MCP3002 remains bound to the native driver"
else
    fail "MCP3002 native binding changed unexpectedly"
fi

if grep -q 'consumer="sck"' "${output_dir}/gpioinfo-strip-lines.txt" &&
        grep -q 'consumer="mosi"' "${output_dir}/gpioinfo-strip-lines.txt"; then
    ok "kernel SPI controller retains strip GPIO ownership"
else
    fail "strip GPIO ownership metadata is unexpected"
fi

{
    echo
    echo "failures: ${failures}"
    echo
    echo "No device was opened. No SPI transfer, ioctl, IIO read, GPIO request,"
    echo "driver bind/unbind, service action, package change, or boot change was"
    echo "performed by this collector."
} >>"${result_file}"

cat "${result_file}"
if ((failures > 0)); then
    die "LPD8806 binding inspection recorded ${failures} failure(s)"
fi
