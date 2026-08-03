#!/bin/bash

# Guard the temporary Phase 5 LPD8806-to-spidev driver override. This helper
# loads/binds or unbinds a driver, but never opens the device or transfers data.

set -u
set -o pipefail

action=
confirmation=
binding_in_progress=0

expected_model="Raspberry Pi Zero W Rev 1.1"
expected_revision=9000c1
expected_overlay_sha256="53b593f4c30a78c8beb446cf506816af29a817d0daf93bb899a9054c7aa61959"
boot_config=/boot/firmware/config.txt
installed_overlay=/boot/firmware/overlays/oclock-spi.dtbo

usage()
{
    cat <<EOF
Usage:
  sudo $0 status
  sudo $0 bind --confirm BIND
  sudo $0 unbind --confirm UNBIND

The strip SPI child is discovered from its live Device Tree path; no spi bus
number is hard-coded. bind loads spidev, writes the child's driver_override,
binds only that child, and waits for its character device. unbind reverses the
binding and clears driver_override. This runtime binding does not survive a
reboot.

The helper does not open /dev/spidev*, send a transfer, read IIO, request GPIO,
start or stop oclock.service, edit /boot, or reboot.
EOF
}

die()
{
    echo "error: $*" >&2
    exit 2
}

(($# > 0)) || {
    usage >&2
    exit 2
}

action=$1
shift
case "${action}" in
    status|bind|unbind)
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        die "unknown action: ${action}"
        ;;
esac

while (($# > 0)); do
    case "$1" in
        --confirm)
            (($# >= 2)) || die "--confirm requires a value"
            confirmation=$2
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

((EUID == 0)) || die "run this helper with sudo"

for command_name in awk basename cat dpkg grep modprobe readlink sha256sum \
        stat systemctl tail tr udevadm uname; do
    command -v "${command_name}" >/dev/null 2>&1 ||
        die "required command is missing: ${command_name}"
done

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

verify_target()
{
    [[ ${model} == "${expected_model}" ]] ||
        die "expected ${expected_model}; found ${model}"
    [[ ${revision:-unknown} == "${expected_revision}" ]] ||
        die "expected revision ${expected_revision}; found ${revision:-unknown}"
    [[ ${machine} == armv6l && ${architecture} == armhf &&
       ${os_codename} == trixie ]] ||
        die "expected ARMv6/armhf Trixie target"
    [[ -f ${installed_overlay} ]] || die "installed overlay is missing"
    printf '%s  %s\n' "${expected_overlay_sha256}" \
        "${installed_overlay}" | sha256sum --check --status ||
        die "installed overlay checksum is not accepted"
    [[ $(grep -Fxc 'dtoverlay=oclock-spi' "${boot_config}" || true) == 1 ]] ||
        die "expected one active Office Clock overlay directive"
    [[ -d /proc/device-tree/oclock-strip-spi/lpd8806@0 ]] ||
        die "live LPD8806 Device Tree child is missing"
    if systemctl is-active --quiet oclock; then
        die "oclock.service is active; keep it stopped during this gate"
    fi
}

find_spi_devices()
{
    local device_path resolved_node
    strip_devices=()
    adc_devices=()
    shopt -s nullglob
    for device_path in /sys/bus/spi/devices/spi*.*; do
        [[ -e ${device_path} ]] || continue
        resolved_node=$(readlink -f "${device_path}/of_node" 2>/dev/null || true)
        if [[ ${resolved_node} == \
              */oclock-strip-spi/lpd8806@0 ]]; then
            strip_devices+=("${device_path}")
        elif [[ ${resolved_node} == \
                 */oclock-adc-spi/mcp3002@0 ]]; then
            adc_devices+=("${device_path}")
        fi
    done
    ((${#strip_devices[@]} == 1)) ||
        die "expected one LPD8806 SPI child; found ${#strip_devices[@]}"
    strip_device=${strip_devices[0]}
    strip_name=$(basename "${strip_device}")
    spidev_name="spidev${strip_name#spi}"
    spidev_node="/dev/${spidev_name}"

    ((${#adc_devices[@]} == 1)) ||
        die "expected one MCP3002 SPI child; found ${#adc_devices[@]}"
    adc_device=${adc_devices[0]}
    [[ -L ${adc_device}/driver ]] ||
        die "MCP3002 SPI child has no driver"
    [[ $(basename "$(readlink -f "${adc_device}/driver")") == mcp320x ]] ||
        die "MCP3002 SPI child is not bound to mcp320x"
}

driver_name()
{
    if [[ -L ${strip_device}/driver ]]; then
        basename "$(readlink -f "${strip_device}/driver")"
    else
        echo none
    fi
}

show_status()
{
    local override
    override=$(cat "${strip_device}/driver_override" 2>/dev/null || true)
    echo "Strip SPI child:    ${strip_name}"
    echo "Device Tree node:   $(readlink -f "${strip_device}/of_node")"
    echo "Driver:             $(driver_name)"
    echo "driver_override:    ${override:-empty}"
    echo "Character device:   ${spidev_node}"
    if [[ -e ${spidev_node} ]]; then
        stat -c '  type=%F mode=%a owner=%U group=%G major_minor=%t:%T' \
            "${spidev_node}"
    else
        echo "  not present"
    fi
    echo "ADC SPI child:      $(basename "${adc_device}") (mcp320x)"
    echo "oclock.service:     $(systemctl is-active oclock 2>/dev/null || true)"
}

rollback_failed_bind()
{
    if [[ $(driver_name) == spidev &&
          -w /sys/bus/spi/drivers/spidev/unbind ]]; then
        printf '%s\n' "${strip_name}" \
            >/sys/bus/spi/drivers/spidev/unbind 2>/dev/null || true
    fi
    printf '\n' >"${strip_device}/driver_override" 2>/dev/null || true
    udevadm settle --timeout=10 >/dev/null 2>&1 || true
}

finish()
{
    local rc=$?
    trap - EXIT
    if ((binding_in_progress)); then
        rollback_failed_bind
        echo "Incomplete strip binding was rolled back." >&2
    fi
    exit "${rc}"
}
trap finish EXIT

verify_target
find_spi_devices

case "${action}" in
    status)
        show_status
        ;;

    bind)
        [[ ${confirmation} == BIND ]] || die "bind requires --confirm BIND"
        [[ $(driver_name) == none ]] || die "strip SPI child is already bound"
        [[ -w ${strip_device}/driver_override ]] ||
            die "strip driver_override is not writable"
        current_override=$(cat "${strip_device}/driver_override" 2>/dev/null || true)
        [[ -z ${current_override} || ${current_override} == '(null)' ]] ||
            die "strip driver_override is already set to ${current_override}"

        modprobe spidev || die "could not load spidev"
        [[ -w /sys/bus/spi/drivers/spidev/bind ]] ||
            die "spidev bind control is unavailable"
        binding_in_progress=1
        printf 'spidev\n' >"${strip_device}/driver_override" ||
            die "could not set strip driver_override"
        if ! printf '%s\n' "${strip_name}" \
                >/sys/bus/spi/drivers/spidev/bind; then
            die "could not bind ${strip_name} to spidev"
        fi
        if ! udevadm settle --timeout=10; then
            die "udev did not settle after binding"
        fi

        if [[ $(driver_name) != spidev || ! -c ${spidev_node} ]]; then
            die "spidev binding validation failed"
        fi
        binding_in_progress=0
        echo "LPD8806 child bound to spidev without opening the device."
        show_status
        ;;

    unbind)
        [[ ${confirmation} == UNBIND ]] ||
            die "unbind requires --confirm UNBIND"
        [[ $(driver_name) == spidev ]] ||
            die "strip SPI child is not bound to spidev"
        [[ -w /sys/bus/spi/drivers/spidev/unbind ]] ||
            die "spidev unbind control is unavailable"

        printf '%s\n' "${strip_name}" \
            >/sys/bus/spi/drivers/spidev/unbind ||
            die "could not unbind ${strip_name}"
        printf '\n' >"${strip_device}/driver_override" ||
            die "could not clear strip driver_override"
        udevadm settle --timeout=10 || die "udev did not settle after unbinding"

        [[ $(driver_name) == none ]] || die "strip child remains bound"
        [[ ! -e ${spidev_node} ]] ||
            die "strip character device remains present"
        echo "LPD8806 spidev binding removed; spidev module left loaded."
        show_status
        ;;
esac
