#!/bin/bash

# Inspect the first boot with the Phase 5 SPI overlay. This collector does not
# bind spidev, open SPI/IIO, read an ADC channel, request GPIO, or change state.

set -u
set -o pipefail

output_dir=
expected_model="Raspberry Pi Zero W Rev 1.1"
expected_revision=9000c1
expected_overlay_sha256="53b593f4c30a78c8beb446cf506816af29a817d0daf93bb899a9054c7aa61959"
boot_config=/boot/firmware/config.txt
installed_overlay=/boot/firmware/overlays/oclock-spi.dtbo

usage()
{
    cat <<EOF
Usage: sudo $0 [--output DIRECTORY]

Collect post-reboot Office Clock SPI overlay, child-driver, IIO metadata, GPIO
consumer, boot-directive, and service state. No spidev binding or hardware
transfer is performed. oclock.service must remain inactive.
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
        grep head id ls lsmod mkdir mktemp od readlink sed sha256sum sort stat \
        systemctl tail tar tr uname; do
    command -v "${command_name}" >/dev/null 2>&1 ||
        die "required command is missing: ${command_name}"
done

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
umask 077
if [[ -z ${output_dir} ]]; then
    output_dir=$(mktemp -d "/tmp/oclock-phase5-spi-boot-${timestamp}-XXXXXXXX") ||
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
warnings=0

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

warn()
{
    echo "WARNING: $*" >>"${result_file}"
    warnings=$((warnings + 1))
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
    echo "Office Clock Phase 5 live SPI-overlay boot inspection"
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
    echo "Only Office Clock overlay metadata is collected."
    echo "No full Device Tree, serial number, MAC address, or network setting is archived."
    echo "No module, binding, device, GPIO, service, package, or boot file is changed."
    echo "No /dev/spidev* or IIO raw attribute is opened or read."
} >"${output_dir}/safety.txt"

{
    stat -c 'path=%n type=%F mode=%a owner=%U group=%G size=%s' \
        "${installed_overlay}" 2>&1 || true
    sha256sum "${installed_overlay}" 2>&1 || true
    echo
    grep -nF 'Office Clock Phase 5 SPI overlay' "${boot_config}" 2>&1 || true
    grep -nF 'dtoverlay=oclock-spi' "${boot_config}" 2>&1 || true
} >"${output_dir}/installed-overlay.txt"

{
    for node in /proc/device-tree/oclock-strip-spi \
            /proc/device-tree/oclock-strip-spi/lpd8806@0 \
            /proc/device-tree/oclock-adc-spi \
            /proc/device-tree/oclock-adc-spi/mcp3002@0; do
        echo "===== ${node} ====="
        if [[ ! -d ${node} ]]; then
            echo "not present"
            echo
            continue
        fi
        for property in compatible num-chipselects sck-gpios miso-gpios \
                mosi-gpios cs-gpios reg spi-max-frequency status; do
            [[ -r ${node}/${property} ]] || continue
            printf '%s text: ' "${property}"
            tr '\0' ' ' <"${node}/${property}" | sed 's/[[:cntrl:]]/./g'
            echo
            printf '%s hex: ' "${property}"
            od -An -tx1 -v "${node}/${property}"
        done
        echo
    done
} >"${output_dir}/active-oclock-nodes.txt" 2>&1

strip_device=
adc_device=
{
    echo "Office Clock SPI children"
    shopt -s nullglob
    for device_path in /sys/bus/spi/devices/spi*; do
        [[ -e ${device_path} ]] || continue
        resolved_device=$(readlink -f "${device_path}")
        resolved_node=$(readlink -f "${device_path}/of_node" 2>/dev/null || true)
        case "${resolved_node}" in
            */oclock-strip-spi/lpd8806@0)
                strip_device=${device_path}
                ;;
            */oclock-adc-spi/mcp3002@0)
                adc_device=${device_path}
                ;;
        esac
        echo "device=${device_path}"
        echo "resolved_device=${resolved_device}"
        echo "of_node=${resolved_node:-none}"
        if [[ -L ${device_path}/driver ]]; then
            echo "driver=$(basename "$(readlink -f "${device_path}/driver")")"
        else
            echo "driver=none"
        fi
        [[ -r ${device_path}/modalias ]] &&
            echo "modalias=$(cat "${device_path}/modalias")"
        echo
    done
} >"${output_dir}/spi-children.txt" 2>&1

iio_device=
{
    echo "IIO devices (metadata only)"
    shopt -s nullglob
    for device_path in /sys/bus/iio/devices/iio:device*; do
        [[ -e ${device_path} ]] || continue
        resolved_device=$(readlink -f "${device_path}")
        echo "device=${device_path}"
        echo "resolved=${resolved_device}"
        if [[ -r ${device_path}/name ]]; then
            device_name=$(cat "${device_path}/name")
            echo "name=${device_name}"
        else
            device_name=unknown
        fi
        find "${device_path}" -maxdepth 1 -type f \
            \( -name 'in_voltage*_raw' -o -name 'in_voltage*_scale' \) \
            -printf 'attribute=%f\n' 2>/dev/null | sort
        if [[ -n ${adc_device} && ${device_name} == mcp3002 &&
              ${resolved_device} == "$(readlink -f "${adc_device}")"/* ]]; then
            iio_device=${device_path}
        fi
        echo
    done
    echo "No attribute above was opened or read."
} >"${output_dir}/iio-devices.txt" 2>&1

{
    lsmod | grep -E '^(spi_gpio|spi_bitbang|spidev|mcp320x|industrialio)[[:space:]]' || true
} >"${output_dir}/loaded-relevant-modules.txt" 2>&1

gpiodetect >"${output_dir}/gpiodetect.txt" 2>&1
broadcom_chip=$(awk \
    '/^[[:space:]]*gpiochip[0-9]+[[:space:]]+\[pinctrl-bcm2835\]/ { print $1; exit }' \
    "${output_dir}/gpiodetect.txt")
gpioinfo_status=1
if [[ ${broadcom_chip} =~ ^gpiochip[0-9]+$ ]] &&
        command -v gpioinfo >/dev/null 2>&1; then
    gpiod_major=$(gpiodetect --version 2>/dev/null |
        sed -nE 's/.*[^0-9]([0-9]+)\..*/\1/p' | head -1)
    if [[ ${gpiod_major} == 2 ]]; then
        gpioinfo -c "${broadcom_chip}" \
            >"${output_dir}/gpioinfo-selected-chip.txt" 2>&1
    else
        gpioinfo "${broadcom_chip}" \
            >"${output_dir}/gpioinfo-selected-chip.txt" 2>&1
    fi
    gpioinfo_status=$?
else
    echo "Broadcom GPIO chip or gpioinfo is unavailable" \
        >"${output_dir}/gpioinfo-selected-chip.txt"
fi

{
    for offset in 4 17 20 21 22 27; do
        line=$(grep -E "line[[:space:]]+${offset}:" \
            "${output_dir}/gpioinfo-selected-chip.txt" | head -1 || true)
        echo "offset ${offset}: ${line:-NOT FOUND}"
    done
} >"${output_dir}/gpioinfo-spi-lines.txt"

systemctl show oclock -p LoadState -p ActiveState -p SubState -p MainPID \
    -p FragmentPath >"${output_dir}/service-properties.txt" 2>&1

echo "Phase 5 live SPI-overlay boot checks" >"${result_file}"

if [[ ${model} == "${expected_model}" &&
      ${revision:-unknown} == "${expected_revision}" &&
      ${machine} == armv6l && ${architecture} == armhf &&
      ${os_codename} == trixie ]]; then
    ok "target is the selected Zero W/Trixie ARMv6 image"
else
    fail "target identity does not match the selected Zero W/Trixie image"
fi

if systemctl is-active --quiet oclock; then
    fail "oclock.service is active; it must remain stopped"
else
    ok "oclock.service is inactive"
fi

if [[ -f ${installed_overlay} ]] &&
        printf '%s  %s\n' "${expected_overlay_sha256}" \
            "${installed_overlay}" | sha256sum --check --status; then
    ok "installed overlay matches the accepted target artifact"
else
    fail "installed overlay is missing or has an unexpected checksum"
fi

if [[ $(grep -Fxc 'dtoverlay=oclock-spi' "${boot_config}" || true) == 1 ]]; then
    ok "boot configuration contains one active Office Clock overlay directive"
else
    fail "boot configuration does not contain exactly one active directive"
fi

if [[ -d /proc/device-tree/oclock-strip-spi &&
      -d /proc/device-tree/oclock-adc-spi ]]; then
    ok "both Office Clock SPI controller nodes are active"
else
    fail "one or both Office Clock SPI controller nodes are absent"
fi

if [[ -n ${strip_device} && ! -L ${strip_device}/driver ]]; then
    ok "LPD8806 SPI child exists and remains deliberately unbound"
else
    fail "LPD8806 SPI child is missing or unexpectedly bound"
fi

if [[ -n ${adc_device} && -L ${adc_device}/driver &&
      $(basename "$(readlink -f "${adc_device}/driver")") == mcp320x ]]; then
    ok "MCP3002 SPI child is bound to the native mcp320x driver"
else
    fail "MCP3002 SPI child is missing or not bound to mcp320x"
fi

if [[ -n ${iio_device} && -e ${iio_device}/in_voltage0_raw &&
      -e ${iio_device}/in_voltage1_raw ]]; then
    ok "MCP3002 IIO metadata exposes both raw channels"
else
    fail "MCP3002 IIO metadata does not expose both raw channels"
fi

if ((gpioinfo_status == 0)) &&
        ! grep -q 'NOT FOUND' "${output_dir}/gpioinfo-spi-lines.txt"; then
    ok "GPIO metadata contains every overlay-owned offset"
else
    fail "GPIO metadata is incomplete for the overlay-owned offsets"
fi

if grep -Eq 'consumer=' "${output_dir}/gpioinfo-spi-lines.txt"; then
    ok "GPIO metadata includes kernel consumer labels"
else
    warn "review GPIO ownership labels manually for this libgpiod output format"
fi

{
    echo
    echo "failures: ${failures}"
    echo "warnings: ${warnings}"
    echo
    echo "No spidev binding, device open, transfer, IIO value read, GPIO request,"
    echo "service action, package change, or boot-file change was performed."
} >>"${result_file}"

cat "${result_file}"
if ((failures > 0)); then
    die "live SPI-overlay boot inspection recorded ${failures} failure(s)"
fi
