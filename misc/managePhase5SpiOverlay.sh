#!/bin/bash

# Guard the first live installation and boot enablement of the Phase 5 Office
# Clock SPI overlay. This helper never reboots, starts the clock, binds a
# userspace SPI driver, opens a device, or transfers data.

set -u
set -o pipefail

action=
overlay_binary=
confirmation=

expected_model="Raspberry Pi Zero W Rev 1.1"
expected_revision=9000c1
expected_machine=armv6l
expected_architecture=armhf
expected_codename=trixie
expected_overlay_sha256="53b593f4c30a78c8beb446cf506816af29a817d0daf93bb899a9054c7aa61959"

boot_config=/boot/firmware/config.txt
installed_overlay=/boot/firmware/overlays/oclock-spi.dtbo
block_begin="# BEGIN Office Clock Phase 5 SPI overlay"
block_end="# END Office Clock Phase 5 SPI overlay"
overlay_directive="dtoverlay=oclock-spi"

usage()
{
    cat <<EOF
Usage:
  sudo $0 status
  sudo $0 enable --overlay FILE.dtbo --confirm ENABLE
  sudo $0 disable --confirm DISABLE

enable verifies the exact Zero W/Trixie target, stopped service, overlay hash,
and inactive live nodes. It then preserves a timestamped byte-for-byte boot
configuration backup, installs the overlay, and appends one managed boot block.

disable preserves another backup and comments the one managed overlay line.
It deliberately retains the installed .dtbo for recovery and inspection.

The helper does not reboot, start or stop oclock.service, load a module, bind
spidev, open SPI/IIO, request GPIO, or perform a hardware transfer.
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
    status|enable|disable)
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
        --overlay)
            (($# >= 2)) || die "--overlay requires a value"
            overlay_binary=$2
            shift 2
            ;;
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

for command_name in awk basename cat chown cmp cp date dirname dpkg grep mkdir \
        readlink sed sha256sum stat sync systemctl tail tr uname; do
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
    [[ ${machine} == "${expected_machine}" ]] ||
        die "expected ${expected_machine}; found ${machine}"
    [[ ${architecture} == "${expected_architecture}" ]] ||
        die "expected ${expected_architecture}; found ${architecture}"
    [[ ${os_codename} == "${expected_codename}" ]] ||
        die "expected ${expected_codename}; found ${os_codename}"
    [[ -f ${boot_config} ]] || die "missing ${boot_config}"
    [[ -d $(dirname "${installed_overlay}") ]] ||
        die "missing overlay directory: $(dirname "${installed_overlay}")"
}

require_inactive_service()
{
    if systemctl is-active --quiet oclock; then
        die "oclock.service is active; stop it deliberately before continuing"
    fi
}

managed_begin_count()
{
    grep -Fxc "${block_begin}" "${boot_config}" 2>/dev/null || true
}

managed_end_count()
{
    grep -Fxc "${block_end}" "${boot_config}" 2>/dev/null || true
}

active_directive_count()
{
    grep -Fxc "${overlay_directive}" "${boot_config}" 2>/dev/null || true
}

make_backup()
{
    local timestamp backup
    timestamp=$(date -u +%Y%m%dT%H%M%SZ)
    backup="${boot_config}.oclock-phase5-${timestamp}.bak"
    [[ ! -e ${backup} && ! -e ${backup}.sha256 ]] ||
        die "refusing to overwrite existing backup: ${backup}"
    cp "${boot_config}" "${backup}" ||
        die "could not preserve boot configuration backup"
    cmp --silent "${boot_config}" "${backup}" ||
        die "boot configuration backup is not byte-identical"
    (
        cd "$(dirname "${backup}")" || exit 1
        sha256sum "$(basename "${backup}")" \
            >"$(basename "${backup}").sha256"
    ) || die "could not checksum boot configuration backup"
    printf '%s\n' "${backup}"
}

show_status()
{
    echo "Target:             ${model} (${machine}/${architecture} ${os_codename})"
    echo "Boot configuration: ${boot_config}"
    stat -c '  mode=%a owner=%U group=%G size=%s' "${boot_config}"
    echo "Installed overlay:  ${installed_overlay}"
    if [[ -f ${installed_overlay} ]]; then
        sha256sum "${installed_overlay}"
    else
        echo "  not installed"
    fi
    echo "Managed markers:    begin=$(managed_begin_count) end=$(managed_end_count)"
    echo "Active directives:  $(active_directive_count)"
    echo "Live strip node:    $([[ -e /proc/device-tree/oclock-strip-spi ]] && echo yes || echo no)"
    echo "Live ADC node:      $([[ -e /proc/device-tree/oclock-adc-spi ]] && echo yes || echo no)"
    echo "oclock.service:     $(systemctl is-active oclock 2>/dev/null || true)"
}

case "${action}" in
    status)
        verify_target
        show_status
        ;;

    enable)
        [[ ${confirmation} == ENABLE ]] ||
            die "enable requires --confirm ENABLE"
        [[ -n ${overlay_binary} && -f ${overlay_binary} ]] ||
            die "enable requires --overlay with a readable compiled overlay"
        verify_target
        require_inactive_service
        [[ ! -e /proc/device-tree/oclock-strip-spi &&
           ! -e /proc/device-tree/oclock-adc-spi ]] ||
            die "Office Clock SPI nodes are already active"
        [[ ! -e ${installed_overlay} ]] ||
            die "refusing to overwrite existing ${installed_overlay}"
        [[ $(managed_begin_count) == 0 && $(managed_end_count) == 0 &&
           $(active_directive_count) == 0 ]] ||
            die "managed block or active Office Clock directive already exists"
        printf '%s  %s\n' "${expected_overlay_sha256}" "${overlay_binary}" |
            sha256sum --check --status ||
            die "compiled overlay does not match the accepted target artifact"

        backup=$(make_backup) || die "could not create boot configuration backup"
        cp "${overlay_binary}" "${installed_overlay}" ||
            die "could not install overlay"
        cmp --silent "${overlay_binary}" "${installed_overlay}" ||
            die "installed overlay is not byte-identical"
        {
            echo
            echo "${block_begin}"
            echo "[all]"
            echo "${overlay_directive}"
            echo "${block_end}"
        } >>"${boot_config}" || die "could not append managed boot block"
        sync "${installed_overlay}" "${boot_config}" "${backup}" \
            "${backup}.sha256" || die "could not sync boot files"

        [[ $(managed_begin_count) == 1 && $(managed_end_count) == 1 &&
           $(active_directive_count) == 1 ]] ||
            die "post-write managed-block verification failed; restore ${backup}"
        printf '%s  %s\n' "${expected_overlay_sha256}" \
            "${installed_overlay}" | sha256sum --check --status ||
            die "post-write overlay checksum failed; restore ${backup}"

        echo "Overlay installed and enabled for the next boot."
        echo "Backup: ${backup}"
        echo "No reboot was performed. Keep oclock.service inactive."
        echo "Rollback before reboot: sudo $0 disable --confirm DISABLE"
        ;;

    disable)
        [[ ${confirmation} == DISABLE ]] ||
            die "disable requires --confirm DISABLE"
        verify_target
        require_inactive_service
        [[ $(managed_begin_count) == 1 && $(managed_end_count) == 1 &&
           $(active_directive_count) == 1 ]] ||
            die "expected exactly one active managed Office Clock block"

        backup=$(make_backup) || die "could not create boot configuration backup"
        sed -i "s|^${overlay_directive}$|# ${overlay_directive} # disabled|" \
            "${boot_config}" || die "could not disable overlay directive"
        sync "${boot_config}" "${backup}" "${backup}.sha256" ||
            die "could not sync boot files"
        [[ $(active_directive_count) == 0 ]] ||
            die "overlay directive remains active; restore ${backup}"

        echo "Overlay disabled for the next boot; installed artifact retained."
        echo "Backup: ${backup}"
        echo "No reboot was performed."
        ;;
esac
