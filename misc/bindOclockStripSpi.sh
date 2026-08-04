#!/bin/bash

# Bind the Office Clock LPD8806 SPI child to spidev at boot.
#
# The overlay gives the strip child a project-owned compatible
# ("flaviof,oclock-lpd8806") that no in-tree driver claims, so the character
# device only exists after an explicit driver_override. This helper performs
# that binding as a system service, before oclock.service starts.
#
# This is the production counterpart to managePhase5Lpd8806Binding.sh. The
# differences are deliberate:
#
#   * No confirmation word. It runs unattended at boot.
#   * No board-model or overlay-checksum guard. Those exist in the gate helper
#     to prove a trial ran against the reviewed overlay. Here the substantive
#     precondition is the live Device Tree child, which is specific enough that
#     it cannot match unrelated hardware.
#   * No "oclock.service must be stopped" guard. Ordering is systemd's job.
#   * Idempotent. Re-running against an already-bound child succeeds.
#   * Waits for the child to appear. spi_gpio is a module loaded during udev
#     coldplug, so the SPI children are created asynchronously well after
#     userspace starts. On the reference unit the MCP3002 probes about 54
#     seconds after boot. A boot-time binder that does not wait is a race.
#
# The helper never opens /dev/spidev*, sends a transfer, or starts a service.

set -u
set -o pipefail

action=
wait_seconds=90
binding_in_progress=0
strip_device=
strip_name=
spidev_node=

strip_node_suffix=/oclock-strip-spi/lpd8806@0

# Test-only path prefix, empty in production. tests/strip-binding.sh sets it to
# a fake sysfs tree so the guard logic and the order of writes can be checked
# without hardware. It redirects only paths that already require root, so it
# confers no privilege the caller does not already hold, and it suppresses
# modprobe and udevadm, which have no meaning against a fake tree.
test_root=${OCLOCK_BIND_TEST_ROOT:-}

spi_devices_dir="${test_root}/sys/bus/spi/devices"
spidev_driver_dir="${test_root}/sys/bus/spi/drivers/spidev"
device_tree_child="${test_root}/proc/device-tree${strip_node_suffix}"
dev_dir="${test_root}/dev"

load_spidev_module()
{
    [[ -n ${test_root} ]] && return 0
    modprobe spidev
}

settle_udev()
{
    [[ -n ${test_root} ]] && return 0
    udevadm settle --timeout=10
}

usage()
{
    cat <<EOF
Usage:
  sudo $0 status
  sudo $0 bind [--wait SECONDS]
  sudo $0 unbind

bind waits for the LPD8806 Device Tree child to appear, sets its
driver_override to spidev, binds it, and verifies the character device. It
succeeds without acting if the child is already bound. unbind reverses it.

Default wait is ${wait_seconds} seconds; use --wait 0 to fail immediately when
the child is absent.
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
        --wait)
            (($# >= 2)) || die "--wait requires a value"
            [[ $2 =~ ^[0-9]+$ ]] || die "--wait requires a whole number"
            wait_seconds=$2
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

if [[ -z ${test_root} ]]; then
    ((EUID == 0)) || die "run this helper as root"

    for command_name in basename cat modprobe readlink sleep stat udevadm; do
        command -v "${command_name}" >/dev/null 2>&1 ||
            die "required command is missing: ${command_name}"
    done
fi

# Locate the single SPI child whose live Device Tree node is the strip. The
# bus number is never assumed: spi-gpio controllers are numbered dynamically,
# so the Device Tree path is the only stable identity.
find_strip_device()
{
    local device_path resolved_node matches=()

    shopt -s nullglob
    for device_path in "${spi_devices_dir}"/spi*.*; do
        [[ -e ${device_path} ]] || continue
        resolved_node=$(readlink -f "${device_path}/of_node" 2>/dev/null || true)
        if [[ ${resolved_node} == *"${strip_node_suffix}" ]]; then
            matches+=("${device_path}")
        fi
    done
    shopt -u nullglob

    ((${#matches[@]} <= 1)) ||
        die "expected one LPD8806 SPI child; found ${#matches[@]}"
    ((${#matches[@]} == 1)) || return 1

    strip_device=${matches[0]}
    strip_name=$(basename "${strip_device}")
    spidev_node="${dev_dir}/spidev${strip_name#spi}"
    return 0
}

await_strip_device()
{
    local waited=0

    while ! find_strip_device; do
        ((waited < wait_seconds)) || {
            [[ -d ${device_tree_child} ]] ||
                die "the Office Clock strip Device Tree child is absent;" \
                    "check that dtoverlay=oclock-spi is active in config.txt"
            die "the LPD8806 SPI child did not appear within" \
                "${wait_seconds}s; spi_gpio may have failed to probe"
        }
        sleep 1
        waited=$((waited + 1))
    done

    ((waited == 0)) ||
        echo "Waited ${waited}s for the LPD8806 SPI child to appear."
}

# Wait for the kernel and udev to finish reacting to a bind or unbind write.
# udevadm settle covers the udev queue, but the driver symlink and the
# character device do not necessarily appear in the same instant the write
# returns, and this runs on a single-core ARMv6 during boot. Poll briefly for
# the expected end state rather than sampling it once.
#
# This is also why a settle timeout is only a warning. settle waits on the
# *global* udev queue, so an unrelated slow or failing probe holds it open and
# has nothing to do with whether our bind succeeded. That is not hypothetical:
# binding spi4.0 makes udev run raspberrypi-sys-mods/i2cprobe against it, which
# fails and keeps running well past the settle timeout, and treating that as
# fatal took the whole clock down at every boot. This function measures the end
# state we actually require, so it — not settle — is the authority.
await_bind_result()
{
    local want=$1
    local waited=0

    while ((waited < 100)); do
        if [[ ${want} == bound ]]; then
            [[ $(driver_name) == spidev && -c ${spidev_node} ]] && return 0
        else
            [[ $(driver_name) == none && ! -e ${spidev_node} ]] && return 0
        fi
        sleep 0.1
        waited=$((waited + 1))
    done
    return 1
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
        stat -c '  type=%F mode=%a owner=%U group=%G' "${spidev_node}"
    else
        echo "  not present"
    fi
}

rollback_failed_bind()
{
    if [[ $(driver_name) == spidev &&
          -w ${spidev_driver_dir}/unbind ]]; then
        printf '%s\n' "${strip_name}" \
            >"${spidev_driver_dir}/unbind" 2>/dev/null || true
    fi
    printf '\n' >"${strip_device}/driver_override" 2>/dev/null || true
    settle_udev >/dev/null 2>&1 || true
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

case "${action}" in
    status)
        find_strip_device || die "no LPD8806 SPI child is present"
        show_status
        ;;

    bind)
        await_strip_device

        # Idempotent: a already-bound child with a live character device is
        # the goal state, so report it and stop. systemd may re-run this unit.
        if [[ $(driver_name) == spidev && -c ${spidev_node} ]]; then
            echo "LPD8806 child is already bound to spidev; nothing to do."
            show_status
            exit 0
        fi

        [[ $(driver_name) == none ]] ||
            die "strip SPI child is bound to $(driver_name), not spidev"
        [[ -w ${strip_device}/driver_override ]] ||
            die "strip driver_override is not writable"

        load_spidev_module || die "could not load spidev"
        [[ -w ${spidev_driver_dir}/bind ]] ||
            die "spidev bind control is unavailable"

        binding_in_progress=1
        printf 'spidev\n' >"${strip_device}/driver_override" ||
            die "could not set strip driver_override"
        printf '%s\n' "${strip_name}" >"${spidev_driver_dir}/bind" ||
            die "could not bind ${strip_name} to spidev"
        settle_udev ||
            echo "warning: udev queue did not drain; verifying the end state" >&2

        await_bind_result bound || {
            [[ $(driver_name) == spidev ]] ||
                die "strip child did not bind to spidev"
            die "strip character device ${spidev_node} was not created"
        }
        binding_in_progress=0

        echo "LPD8806 child bound to spidev without opening the device."
        show_status
        ;;

    unbind)
        find_strip_device || {
            echo "No LPD8806 SPI child is present; nothing to unbind."
            exit 0
        }
        if [[ $(driver_name) == none ]]; then
            echo "LPD8806 child is not bound; nothing to unbind."
            exit 0
        fi
        [[ $(driver_name) == spidev ]] ||
            die "strip SPI child is bound to $(driver_name), not spidev"
        [[ -w ${spidev_driver_dir}/unbind ]] ||
            die "spidev unbind control is unavailable"

        printf '%s\n' "${strip_name}" \
            >"${spidev_driver_dir}/unbind" ||
            die "could not unbind ${strip_name}"
        printf '\n' >"${strip_device}/driver_override" ||
            die "could not clear strip driver_override"
        settle_udev ||
            echo "warning: udev queue did not drain; verifying the end state" >&2

        await_bind_result unbound || {
            [[ $(driver_name) == none ]] || die "strip child remains bound"
            die "strip character device remains present"
        }
        echo "LPD8806 spidev binding removed; spidev module left loaded."
        ;;
esac
