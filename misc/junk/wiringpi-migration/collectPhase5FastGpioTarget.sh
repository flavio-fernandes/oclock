#!/bin/bash

# Read-only follow-up to the failed Phase 5 libgpiod timing trial. This records
# whether the selected Zero W/Trixie image exposes the restricted Raspberry Pi
# GPIO memory device required for a bounded fast-value-path experiment. It
# never requests, configures, or drives a GPIO line and does not stop services.

set -u
set -o pipefail

output_dir=

usage()
{
    cat <<EOF
Usage: sudo $0 [--output DIRECTORY]

Options:
  --output DIRECTORY  New result directory (default: secure directory in /tmp)
  -h, --help          Show this help

This collector is read-only. It inspects the Zero W, /dev/gpiomem, GPIO device
metadata, permissions, and the current clock service. Its probe maps
/dev/gpiomem read-only and reads the GPIO level register; it does not write a
register or change pin state.
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
            output_dir=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
done

((EUID == 0)) || die "run this script with sudo"
for command_name in awk basename cc chown date dirname dpkg file find getent \
        id mkdir mktemp readlink sha256sum sort stat systemctl tail tar tr \
        uname; do
    command -v "${command_name}" >/dev/null 2>&1 ||
        die "required command is missing: ${command_name}"
done

[[ -r /proc/device-tree/model ]] || die "cannot read Raspberry Pi model"
model=$(tr -d '\0' </proc/device-tree/model) || die "cannot read model"
revision=$(awk -F: '/^Revision/ { gsub(/[[:space:]]/, "", $2); print tolower($2) }' \
    /proc/cpuinfo | tail -1)
[[ ${model} == "Raspberry Pi Zero W Rev 1.1" ]] ||
    die "wrong hardware: expected Raspberry Pi Zero W Rev 1.1, found ${model}"
[[ ${revision} == 9000c1 ]] ||
    die "wrong board revision: expected 9000c1, found ${revision:-unknown}"
[[ $(uname -m) == armv6l ]] || die "target is not running ARMv6"
[[ $(dpkg --print-architecture) == armhf ]] ||
    die "target package architecture is not armhf"

# shellcheck disable=SC1091
source /etc/os-release
[[ ${VERSION_CODENAME:-} == trixie ]] ||
    die "target card is not Debian/Raspbian Trixie"

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
umask 077
if [[ -z ${output_dir} ]]; then
    output_dir=$(mktemp -d "/tmp/oclock-phase5-fastgpio-${timestamp}-XXXXXXXX") ||
        die "cannot create result directory"
else
    [[ ${output_dir} != / ]] || die "unsafe output directory"
    mkdir -p "$(dirname "${output_dir}")" || die "cannot create output parent"
    mkdir "${output_dir}" || die "output directory already exists"
fi

archive_path="${output_dir}.tar.gz"
result_file="${output_dir}/result.txt"
failures=0

finish()
{
    local rc=$?
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

{
    echo "Phase 5 fast-GPIO target follow-up"
    echo
    echo "collected_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "model: ${model}"
    echo "revision: ${revision}"
    echo "os: ${PRETTY_NAME:-unknown}"
    echo "kernel: $(uname -srvm)"
    echo "machine: $(uname -m)"
    echo "architecture: $(dpkg --print-architecture)"
    echo "collector_is_read_only: yes"
} >"${output_dir}/context.txt"

{
    echo "command: stat /dev/gpiomem"
    echo
    stat /dev/gpiomem
} >"${output_dir}/gpiomem-stat.txt" 2>&1 || true

{
    echo "resolved_path: $(readlink -f /dev/gpiomem 2>/dev/null || true)"
    echo "file: $(file /dev/gpiomem 2>&1 || true)"
    echo "root_readable: $([[ -r /dev/gpiomem ]] && echo yes || echo no)"
    echo "root_writable: $([[ -w /dev/gpiomem ]] && echo yes || echo no)"
    echo
    echo "gpio_group:"
    getent group gpio || true
    echo
    echo "invoking_user:"
    if [[ -n ${SUDO_USER:-} ]]; then
        id "${SUDO_USER}" || true
    else
        id || true
    fi
} >"${output_dir}/gpiomem-access.txt" 2>&1

{
    echo "device_nodes:"
    find /dev -maxdepth 1 \( -name 'gpiomem*' -o -name 'gpiochip*' \) \
        -exec stat -c '%A %U %G %t:%T %n' {} + 2>&1 | sort
    echo
    echo "kernel_modules:"
    awk 'tolower($1) ~ /gpiomem/' /proc/modules 2>/dev/null || true
    echo
    echo "platform_devices:"
    find /sys/devices/platform -maxdepth 4 -iname '*gpiomem*' -print 2>/dev/null |
        sort
} >"${output_dir}/gpio-devices.txt"

systemctl status oclock --no-pager >"${output_dir}/service-status.txt" 2>&1 || true

cat >"${output_dir}/gpiomem-read-probe.c" <<'EOF'
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void) {
  const char *const path = "/dev/gpiomem";
  const size_t mapLength = 4096;
  const size_t levelRegisterIndex = 13; /* GPLEV0 at byte offset 0x34. */
  int fd = open(path, O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    fprintf(stderr, "open %s: %s\n", path, strerror(errno));
    return 1;
  }

  void *mapping = mmap(NULL, mapLength, PROT_READ, MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) {
    fprintf(stderr, "mmap %s: %s\n", path, strerror(errno));
    close(fd);
    return 1;
  }

  volatile const uint32_t *registers = mapping;
  const uint32_t levels = registers[levelRegisterIndex];
  printf("read_only_mapping: ok\n");
  printf("gplev0_sample: 0x%08x\n", levels);

  munmap(mapping, mapLength);
  close(fd);
  return 0;
}
EOF

if cc -std=gnu11 -Wall -Wextra -Werror -x c \
        "${output_dir}/gpiomem-read-probe.c" \
        -o "${output_dir}/gpiomem-read-probe" \
        >"${output_dir}/probe-compile.txt" 2>&1; then
    "${output_dir}/gpiomem-read-probe" \
        >"${output_dir}/probe-run.txt" 2>&1
    probe_status=$?
else
    probe_status=1
fi

result_check()
{
    local description=$1
    shift
    if "$@"; then
        echo "OK: ${description}" >>"${result_file}"
    else
        echo "FAIL: ${description}" >>"${result_file}"
        failures=$((failures + 1))
    fi
}

echo "Phase 5 fast-GPIO target checks" >"${result_file}"
result_check "target is the selected Zero W/Trixie ARMv6 image" test \
    "${model}" = "Raspberry Pi Zero W Rev 1.1"
result_check "/dev/gpiomem is a character device" test -c /dev/gpiomem
result_check "/dev/gpiomem is readable by the collector" test -r /dev/gpiomem
result_check "/dev/gpiomem is writable by the collector" test -w /dev/gpiomem
result_check "read-only GPIO register mapping succeeded" test \
    "${probe_status}" -eq 0

{
    echo
    echo "failures: ${failures}"
    echo
    echo "No GPIO line was requested, configured, or driven."
} >>"${result_file}"

cat "${result_file}"
((failures == 0)) || die "fast-GPIO target capture recorded ${failures} failure(s)"
