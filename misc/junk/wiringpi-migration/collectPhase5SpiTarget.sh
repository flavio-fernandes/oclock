#!/bin/bash

# Collect read-only evidence needed to design the Office Clock's two spi-gpio
# controllers and the native MCP3002 IIO binding. This script never loads a
# module, applies an overlay, opens a device node, requests a GPIO line, stops
# a service, or changes boot files.

set -u
set -o pipefail

output_dir=
expected_model="Raspberry Pi Zero W Rev 1.1"
expected_revision=9000c1
reviewed_kernel="6.18.39+rpt-rpi-v6"
reviewed_spidev_sha256="635863f9af5f50d6412791c3bb5195c9dab465bcf821e2d51252aee74f4c927e"

usage()
{
    cat <<EOF
Usage: sudo $0 [--output DIRECTORY]

Options:
  --output DIRECTORY  New result directory (default: secure directory in /tmp)
  -h, --help          Show this help

Run this on the selected Zero W/Trixie SD card. The collector inspects kernel
SPI and MCP3002 IIO support, available modules, existing SPI/IIO devices, GPIO
consumers, boot configuration locations, Device Tree overlay tools, and
spidev binding evidence. It does not load or bind a driver, apply an overlay,
open a device node, request or drive GPIO, stop the clock, or modify the
system.
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

for command_name in awk basename cat chown date dirname dpkg dpkg-query find \
        grep head id ls mkdir mktemp od readlink sed sha256sum sort \
        stat systemctl tail tar tr uname; do
    command -v "${command_name}" >/dev/null 2>&1 ||
        die "required command is missing: ${command_name}"
done

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
umask 077
if [[ -z ${output_dir} ]]; then
    output_dir=$(mktemp -d "/tmp/oclock-phase5-spi-${timestamp}-XXXXXXXX") ||
        die "cannot create result directory"
else
    [[ ${output_dir} != / ]] || die "unsafe output directory"
    mkdir -p "$(dirname "${output_dir}")" ||
        die "cannot create output parent"
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
    local archive_status
    set +e
    if [[ -d ${output_dir} ]]; then
        tar -czf "${archive_path}" -C "$(dirname "${output_dir}")" \
            "$(basename "${output_dir}")"
        archive_status=$?
        if ((archive_status == 0)); then
            if (
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
        else
            rc=1
        fi
        echo "Result directory:   ${output_dir}" >&2
    fi
    exit "${rc}"
}
trap finish EXIT

capture()
{
    local name=$1
    shift
    local destination="${output_dir}/${name}.txt"
    {
        printf 'command:'
        printf ' %q' "$@"
        printf '\n\n'
        "$@"
        local rc=$?
        printf '\nexit_status: %d\n' "${rc}"
    } >"${destination}" 2>&1
    return 0
}

result_ok()
{
    echo "OK: $*" >>"${result_file}"
}

result_warn()
{
    echo "WARNING: $*" >>"${result_file}"
    warnings=$((warnings + 1))
}

result_fail()
{
    echo "FAIL: $*" >>"${result_file}"
    failures=$((failures + 1))
}

kernel_setting_enabled()
{
    local setting=$1
    grep -Eq "^${setting}=[ym]$" "${output_dir}/kernel-spi-config.txt"
}

module_is_available()
{
    local setting=$1
    local module_name=$2
    if grep -q "^${setting}=y$" "${output_dir}/kernel-spi-config.txt"; then
        return 0
    fi
    grep -q "^${setting}=m$" "${output_dir}/kernel-spi-config.txt" &&
        modinfo "${module_name}" >/dev/null 2>&1
}

model=unknown
if [[ -r /proc/device-tree/model ]]; then
    model=$(tr -d '\0' </proc/device-tree/model)
fi
revision=unknown
if [[ -r /proc/cpuinfo ]]; then
    revision=$(awk -F: '/^Revision/ {
        gsub(/[[:space:]]/, "", $2); print tolower($2)
    }' /proc/cpuinfo | tail -1)
    [[ -n ${revision} ]] || revision=unknown
fi
machine=$(uname -m)
architecture=$(dpkg --print-architecture)

os_codename=unknown
os_pretty_name=unknown
if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    source /etc/os-release
    os_codename=${VERSION_CODENAME:-unknown}
    os_pretty_name=${PRETTY_NAME:-unknown}
fi

{
    echo "Office Clock Phase 5 kernel-SPI discovery"
    echo
    echo "collected_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "collector: $0"
    echo "collector_uid: $(id -u)"
    echo "model: ${model}"
    echo "revision: ${revision}"
    echo "os: ${os_pretty_name}"
    echo "os_codename: ${os_codename}"
    echo "kernel: $(uname -srvm)"
    echo "machine: ${machine}"
    echo "architecture: ${architecture}"
    echo
    echo "Selected unchanged BCM wiring:"
    echo "  LPD8806: clock=20 data=21 (no chip select)"
    echo "  MCP3002: clock=17 miso=27 mosi=22 chip-select=4"
    echo "  HT1632 remains outside kernel SPI: 6 13 19 26"
    echo "  motion remains libgpiod input: 10"
} >"${output_dir}/context.txt"

{
    echo "This collector is observational only."
    echo
    echo "It does not:"
    echo "  load or unload a kernel module"
    echo "  apply, remove, compile, or install a Device Tree overlay"
    echo "  bind or unbind an SPI driver"
    echo "  open or transfer data through /dev/spidev*"
    echo "  request, read, configure, or drive a GPIO line"
    echo "  stop, start, or restart oclock.service"
    echo "  install a package or modify a boot file"
    echo
    echo "gpioinfo is used only for line metadata and consumer names."
    echo "Serial numbers, MAC addresses, and network configuration are omitted."
} >"${output_dir}/safety.txt"

capture kernel uname -a
capture architecture dpkg --print-architecture
# The dpkg-query format placeholders must remain literal for dpkg to expand.
# shellcheck disable=SC2016
capture installed-kernel-packages dpkg-query -W \
    '-f=${binary:Package}\t${Version}\t${Architecture}\t${Status}\n' \
    'linux-image*' 'linux-headers*' 'raspberrypi-kernel*' 'raspi-utils*' \
    'device-tree-compiler*'

{
    found_config=0
    for config_path in \
            "/boot/config-$(uname -r)" \
            "/boot/firmware/config-$(uname -r)" \
            /proc/config.gz; do
        [[ -r ${config_path} ]] || continue
        found_config=1
        echo "===== ${config_path} ====="
        if [[ ${config_path} == /proc/config.gz ]]; then
            if command -v zcat >/dev/null 2>&1; then
                zcat "${config_path}" |
                    grep -E '^(CONFIG_(SPI|SPI_MASTER|SPI_GPIO|SPI_SPIDEV|IIO|MCP320X)=|# CONFIG_(SPI|SPI_MASTER|SPI_GPIO|SPI_SPIDEV|IIO|MCP320X) is not set)' || true
            else
                echo "zcat is unavailable"
            fi
        else
            grep -E '^(CONFIG_(SPI|SPI_MASTER|SPI_GPIO|SPI_SPIDEV|IIO|MCP320X)=|# CONFIG_(SPI|SPI_MASTER|SPI_GPIO|SPI_SPIDEV|IIO|MCP320X) is not set)' \
                "${config_path}" || true
        fi
        echo
    done
    ((found_config > 0)) || echo "No readable running-kernel config found"
} >"${output_dir}/kernel-spi-config.txt" 2>&1

capture loaded-modules lsmod
capture spi-gpio-modinfo modinfo spi-gpio
capture spidev-modinfo modinfo spidev
capture mcp320x-modinfo modinfo mcp320x
if command -v modprobe >/dev/null 2>&1; then
    capture spi-gpio-module-dependencies modprobe --show-depends spi-gpio
    capture spidev-module-dependencies modprobe --show-depends spidev
    capture mcp320x-module-dependencies modprobe --show-depends mcp320x
fi

{
    module_root="/lib/modules/$(uname -r)"
    echo "module_root: ${module_root}"
    for metadata_file in modules.builtin modules.alias modules.dep; do
        module_metadata="${module_root}/${metadata_file}"
        [[ -r ${module_metadata} ]] || continue
        echo
        echo "===== ${module_metadata} ====="
        grep -Ei '(^|[/ :_-])(spi[-_]gpio|spidev|mcp320x)([ .:_-]|$)' \
            "${module_metadata}" || true
    done
    echo
    echo "module_files:"
    find "${module_root}" -type f \
        \( -name 'spi-gpio.ko*' -o -name 'spidev.ko*' -o \
            -name 'mcp320x.ko*' \) \
        -print 2>/dev/null | sort
} >"${output_dir}/spi-module-availability.txt" 2>&1

{
    shopt -s nullglob
    spi_nodes=(/dev/spidev*)
    if ((${#spi_nodes[@]} == 0)); then
        echo "No /dev/spidev* nodes found"
    else
        stat -c 'path=%n type=%F major_minor=%t:%T mode=%a owner=%U group=%G' \
            "${spi_nodes[@]}"
    fi
    echo
    echo "No node above was opened by this collector."
} >"${output_dir}/spidev-device-nodes.txt" 2>&1

{
    for bus_path in /sys/class/spi_master /sys/bus/spi/devices \
            /sys/bus/spi/drivers; do
        echo "===== ${bus_path} ====="
        if [[ -d ${bus_path} ]]; then
            find "${bus_path}" -maxdepth 2 -mindepth 1 -printf '%y %p -> %l\n' \
                2>/dev/null | sort
        else
            echo "not present"
        fi
        echo
    done
    shopt -s nullglob
    spi_devices=(/sys/bus/spi/devices/spi*)
    for device_path in "${spi_devices[@]}"; do
        [[ -d ${device_path} ]] || continue
        echo "===== ${device_path} ====="
        readlink -f "${device_path}"
        for property in modalias driver_override; do
            if [[ -r ${device_path}/${property} ]]; then
                printf '%s: ' "${property}"
                cat "${device_path}/${property}"
            fi
        done
        if [[ -L ${device_path}/driver ]]; then
            echo "driver: $(readlink -f "${device_path}/driver")"
        fi
        echo
    done
} >"${output_dir}/spi-sysfs.txt" 2>&1

{
    for iio_path in /sys/bus/iio/devices /sys/bus/spi/drivers/mcp320x; do
        echo "===== ${iio_path} ====="
        if [[ -d ${iio_path} ]]; then
            find "${iio_path}" -maxdepth 2 -mindepth 1 \
                -printf '%y %p -> %l\n' 2>/dev/null | sort
        else
            echo "not present"
        fi
        echo
    done
    echo "No IIO value was opened or read by this collector."
} >"${output_dir}/iio-sysfs.txt" 2>&1

{
    echo "spidev module aliases:"
    modinfo -F alias spidev 2>&1 || true
    echo
    echo "spidev module path:"
    spidev_module=$(modinfo -n spidev 2>/dev/null || true)
    echo "${spidev_module:-not found}"
    if [[ -n ${spidev_module} && -f ${spidev_module} ]]; then
        sha256sum "${spidev_module}"
    fi
    echo
    echo "spidev driver sysfs controls:"
    if [[ -d /sys/bus/spi/drivers/spidev ]]; then
        find /sys/bus/spi/drivers/spidev -maxdepth 1 \
            -printf '%y %f mode=%m\n' 2>/dev/null | sort
    else
        echo "spidev driver is not currently registered"
    fi
    echo
    echo "installed spidev source candidates:"
    find /usr/src -path '*/drivers/spi/spidev.c' -print 2>/dev/null | sort
} >"${output_dir}/spidev-binding-evidence.txt" 2>&1

{
    while IFS= read -r source_path; do
        echo "===== ${source_path} ====="
        grep -nE 'spidev_of_check|spidev_dt_ids|spidev_spi_ids|compatible.*spidev|spidev listed directly' \
            "${source_path}" || true
        echo
    done < <(find /usr/src -path '*/drivers/spi/spidev.c' \
        -print 2>/dev/null | sort)
} >"${output_dir}/installed-spidev-source.txt" 2>&1

{
    echo "Boot configuration candidates:"
    for boot_config in /boot/firmware/config.txt /boot/config.txt; do
        if [[ -e ${boot_config} ]]; then
            stat -c 'path=%n type=%F mode=%a owner=%U group=%G' "${boot_config}"
            echo "resolved: $(readlink -f "${boot_config}")"
        else
            echo "missing: ${boot_config}"
        fi
    done
    echo
    for boot_config in /boot/firmware/config.txt /boot/config.txt; do
        [[ -r ${boot_config} ]] || continue
        echo "===== ${boot_config} (Device Tree and GPIO directives only) ====="
        grep -nE '^[[:space:]]*(\[[^]]+\]|device_tree=|dtoverlay=|dtparam=.*spi|gpio=)' \
            "${boot_config}" || true
        echo
    done
} >"${output_dir}/boot-spi-settings.txt" 2>&1

capture dtoverlay-help dtoverlay -h
capture dtoverlay-list-active dtoverlay -l
capture dtoverlay-list-available dtoverlay -a
capture dtoverlay-spi-gpio-help dtoverlay -h spi-gpio
if command -v dtc >/dev/null 2>&1; then
    capture device-tree-compiler-version dtc --version
fi
if command -v fdtoverlay >/dev/null 2>&1; then
    capture fdtoverlay-help fdtoverlay --help
fi

{
    for overlay_dir in /boot/firmware/overlays /boot/overlays; do
        echo "===== ${overlay_dir} ====="
        if [[ -d ${overlay_dir} ]]; then
            find "${overlay_dir}" -maxdepth 1 -type f \
                \( -iname '*spi*.dtbo' -o -name README \) \
                -printf '%f\n' 2>/dev/null | sort
        else
            echo "not present"
        fi
        echo
    done
    for overlay_readme in /boot/firmware/overlays/README \
            /boot/overlays/README; do
        [[ -r ${overlay_readme} ]] || continue
        echo "===== ${overlay_readme} (SPI-related entries) ====="
        grep -nEi 'spi-gpio|spidev|^Name:[[:space:]]+spi|^Load:[[:space:]]+.*spi' \
            "${overlay_readme}" || true
        echo
    done
} >"${output_dir}/overlay-inventory.txt" 2>&1

{
    echo "Existing Device Tree SPI-related nodes and selected properties"
    echo
    if [[ ! -d /proc/device-tree ]]; then
        echo "/proc/device-tree is not present"
    else
        while IFS= read -r node_path; do
            echo "===== ${node_path} ====="
            for property in compatible status reg spi-max-frequency \
                    num-chipselects cs-gpios sck-gpios mosi-gpios miso-gpios; do
                property_path="${node_path}/${property}"
                [[ -r ${property_path} ]] || continue
                printf '%s text: ' "${property}"
                tr '\0' ' ' <"${property_path}" | sed 's/[[:cntrl:]]/./g'
                echo
                printf '%s hex: ' "${property}"
                od -An -tx1 -v "${property_path}" 2>/dev/null || true
            done
            echo
        done < <(find /proc/device-tree -type d \
            \( -name 'spi@*' -o -name 'spi-gpio*' -o -name 'spidev@*' \) \
            -print 2>/dev/null | sort)
    fi
} >"${output_dir}/active-device-tree-spi.txt" 2>&1

capture gpiodetect gpiodetect
gpiod_version=$(gpiodetect --version 2>/dev/null |
    sed -nE 's/.*[^0-9]([0-9]+\.[0-9]+(\.[0-9]+)?).*/\1/p' |
    head -1)
gpiod_major=${gpiod_version%%.*}
broadcom_record=$(grep -E \
    '^[[:space:]]*gpiochip[0-9]+[[:space:]]+\[pinctrl-bcm2835\][[:space:]]+\([0-9]+ lines\)' \
    "${output_dir}/gpiodetect.txt" 2>/dev/null | head -1 || true)
broadcom_chip=$(awk '{ print $1 }' <<<"${broadcom_record}")
gpioinfo_status=1
if [[ ${broadcom_chip} =~ ^gpiochip[0-9]+$ ]] &&
        command -v gpioinfo >/dev/null 2>&1; then
    {
        if [[ ${gpiod_major} == 2 ]]; then
            echo "command: gpioinfo -c ${broadcom_chip}"
            echo
            gpioinfo -c "${broadcom_chip}"
            gpioinfo_status=$?
        else
            echo "command: gpioinfo ${broadcom_chip}"
            echo
            gpioinfo "${broadcom_chip}"
            gpioinfo_status=$?
        fi
        echo
        echo "exit_status: ${gpioinfo_status}"
    } >"${output_dir}/gpioinfo-selected-chip.txt" 2>&1
else
    echo "Broadcom GPIO chip or gpioinfo command not available" \
        >"${output_dir}/gpioinfo-selected-chip.txt"
fi

relevant_offsets=(4 6 10 13 17 19 20 21 22 26 27)
{
    echo "Expected BCM offsets: ${relevant_offsets[*]}"
    echo
    for offset in "${relevant_offsets[@]}"; do
        line=$(grep -E "line[[:space:]]+${offset}:" \
            "${output_dir}/gpioinfo-selected-chip.txt" 2>/dev/null |
            head -1 || true)
        echo "offset ${offset}: ${line:-NOT FOUND}"
    done
} >"${output_dir}/gpioinfo-oclock-consumers.txt"

capture service-status systemctl status oclock --no-pager
capture service-properties systemctl show oclock \
    -p ActiveState -p SubState -p MainPID -p ExecMainStartTimestamp \
    -p FragmentPath

echo "Phase 5 kernel-SPI target checks" >"${result_file}"

if [[ ${model} == "${expected_model}" &&
      ${revision} == "${expected_revision}" &&
      ${machine} == armv6l &&
      ${architecture} == armhf &&
      ${os_codename} == trixie ]]; then
    result_ok "target is the selected Zero W/Trixie ARMv6 image"
else
    result_fail "expected ${expected_model}, revision ${expected_revision}, ARMv6/armhf Trixie"
fi

if kernel_setting_enabled CONFIG_SPI; then
    result_ok "running kernel enables the SPI core"
else
    result_fail "running kernel does not expose CONFIG_SPI=y/m"
fi

if module_is_available CONFIG_SPI_GPIO spi-gpio; then
    result_ok "spi-gpio is built in or available for the running kernel"
else
    result_fail "spi-gpio is unavailable for the running kernel"
fi

if module_is_available CONFIG_SPI_SPIDEV spidev; then
    result_ok "spidev is built in or available for the running kernel"
else
    result_fail "spidev is unavailable for the running kernel"
fi

if module_is_available CONFIG_MCP320X mcp320x; then
    result_ok "native MCP3002 IIO driver is built in or available"
else
    result_fail "native MCP3002 IIO driver is unavailable for the running kernel"
fi

if grep -q '^alias:.*spi:spidev' "${output_dir}/spidev-modinfo.txt" ||
        grep -q '^spi:spidev$' "${output_dir}/spidev-binding-evidence.txt"; then
    result_ok "spidev module alias evidence was captured"
else
    result_warn "spidev module aliases need manual review"
fi

if grep -q '^exit_status: 0$' "${output_dir}/gpiodetect.txt" &&
        ((gpioinfo_status == 0)); then
    result_ok "GPIO line-consumer metadata was captured"
else
    result_fail "GPIO line-consumer metadata could not be captured"
fi

missing_offsets=$(grep -c 'NOT FOUND' \
    "${output_dir}/gpioinfo-oclock-consumers.txt" || true)
if ((missing_offsets == 0)); then
    result_ok "GPIO metadata contains every office-clock BCM offset"
else
    result_fail "GPIO metadata is missing ${missing_offsets} office-clock offset(s)"
fi

if grep -q '^exit_status: 0$' "${output_dir}/dtoverlay-list-active.txt" &&
        grep -q '^exit_status: 0$' \
            "${output_dir}/dtoverlay-list-available.txt"; then
    result_ok "Raspberry Pi Device Tree overlay tooling is available"
else
    result_fail "dtoverlay tooling is unavailable"
fi

if [[ -r /boot/firmware/overlays/README || -r /boot/overlays/README ]]; then
    result_ok "installed overlay documentation was captured"
else
    result_warn "installed overlay README is unavailable"
fi

if command -v dtc >/dev/null 2>&1; then
    result_ok "Device Tree compiler is available"
else
    result_warn "target-side dtc is unavailable; compile the overlay off-device"
fi

if [[ -r /boot/firmware/config.txt || -r /boot/config.txt ]]; then
    result_ok "boot configuration location and SPI directives were captured"
else
    result_fail "no readable Raspberry Pi boot configuration was found"
fi

spidev_module=$(modinfo -n spidev 2>/dev/null || true)
if [[ $(uname -r) == "${reviewed_kernel}" && -f ${spidev_module} ]] &&
        printf '%s  %s\n' "${reviewed_spidev_sha256}" \
            "${spidev_module}" | sha256sum --check --status; then
    result_ok "spidev module matches the exact downstream source review"
elif grep -q '/drivers/spi/spidev.c$' \
        "${output_dir}/spidev-binding-evidence.txt"; then
    result_ok "installed spidev source is available for exact binding review"
else
    result_warn "correlate the captured kernel package and module with its downstream source"
fi

{
    echo
    echo "failures: ${failures}"
    echo "warnings: ${warnings}"
    echo
    echo "No module, overlay, driver binding, SPI transfer, GPIO state, service,"
    echo "package, or boot configuration was changed."
} >>"${result_file}"

cat "${result_file}"
if ((failures > 0)); then
    die "kernel-SPI target capture recorded ${failures} failure(s)"
fi
