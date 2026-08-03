#!/bin/bash

# Collect read-only evidence for selecting the Phase 3 GPIO backend target.
# This script does not request GPIO lines, read line values, change direction
# or output state, install packages, or modify boot configuration.

set -u
set -o pipefail

expected_debian_version=13
expected_libgpiod_major=2
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
output_dir=

usage()
{
    cat <<EOF
Usage: sudo $0 [options]

Options:
  --output DIRECTORY          Output directory (default: secure /tmp directory)
  --debian-version VERSION    Expected Debian version (default: ${expected_debian_version})
  --libgpiod-major VERSION    Expected libgpiod major (default: ${expected_libgpiod_major})
  -h, --help                  Show this help

Run this only on the separately prepared target SD card. It collects operating
system, kernel, GPIO character-device, line metadata, package, compiler, and
GPIO ABI evidence without requesting or driving any GPIO line.
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
        --debian-version)
            (($# >= 2)) || die "--debian-version requires a value"
            expected_debian_version=$2
            shift 2
            ;;
        --libgpiod-major)
            (($# >= 2)) || die "--libgpiod-major requires a value"
            expected_libgpiod_major=$2
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

[[ ${expected_debian_version} =~ ^[0-9]+$ ]] ||
    die "--debian-version must be an integer"
[[ ${expected_libgpiod_major} =~ ^[0-9]+$ ]] ||
    die "--libgpiod-major must be an integer"
((EUID == 0)) ||
    die "run this collector as root (for example, with sudo)"

umask 077
if [[ -z ${output_dir} ]]; then
    output_dir=$(mktemp -d "/tmp/oclock-phase3-${timestamp}-XXXXXXXX") ||
        die "cannot create secure temporary output directory"
else
    [[ ${output_dir} != / ]] || die "unsafe output directory"
    mkdir -p "$(dirname "${output_dir}")" ||
        die "cannot create output parent"
    mkdir "${output_dir}" ||
        die "output directory already exists or cannot be created: ${output_dir}"
fi

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

{
    echo "Office Clock Phase 3 target-image evidence"
    echo
    echo "collection_utc: $(date -u --iso-8601=seconds 2>/dev/null || date -u)"
    echo "collector: $0"
    echo "collector_uid: $(id -u)"
    echo "expected_debian_version: ${expected_debian_version}"
    echo "expected_libgpiod_major: ${expected_libgpiod_major}"
    echo
    echo "Safety contract:"
    echo "  metadata-only GPIO inspection"
    echo "  no line requests or line-value reads"
    echo "  no direction or output changes"
    echo "  no package installation or boot modification"
} >"${output_dir}/collection-context.txt"

capture identity id
capture kernel uname -a
capture machine uname -m
capture architecture dpkg --print-architecture
capture compiler c++ --version
capture c-compiler cc --version
capture pkg-config-version pkg-config --version

{
    for release_file in /etc/os-release /etc/debian_version; do
        if [[ -r ${release_file} ]]; then
            echo "===== ${release_file} ====="
            cat "${release_file}"
            echo
        fi
    done
} >"${output_dir}/operating-system.txt" 2>&1

{
    if [[ -r /proc/device-tree/model ]]; then
        echo -n "model: "
        tr -d '\000' </proc/device-tree/model
        echo
    fi
    if [[ -r /proc/device-tree/compatible ]]; then
        echo "compatible:"
        tr '\000' '\n' </proc/device-tree/compatible
    fi
    if [[ -r /proc/cpuinfo ]]; then
        echo
        echo "cpuinfo (Serial omitted):"
        sed '/^Serial[[:space:]]*:/d' /proc/cpuinfo
    fi
} >"${output_dir}/raspberry-pi.txt" 2>&1

if command -v vcgencmd >/dev/null 2>&1; then
    capture firmware-version vcgencmd version
    capture throttling vcgencmd get_throttled
fi

capture gpiodetect-version gpiodetect --version
capture gpiodetect gpiodetect
capture libgpiod-pkg-version pkg-config --modversion libgpiod
capture libgpiod-pkg-flags pkg-config --cflags --libs libgpiod
capture libgpiodcxx-pkg-version pkg-config --modversion libgpiodcxx
capture gpio-packages dpkg-query -W \
    -f='${binary:Package}\t${Version}\t${Architecture}\t${Status}\n' \
    gpiod libgpiod-dev libgpiod3 libgpiod2 libgpiod2t64

{
    shopt -s nullglob
    gpio_nodes=(/dev/gpiochip*)
    if ((${#gpio_nodes[@]} == 0)); then
        echo "No GPIO character devices found"
    else
        ls -l "${gpio_nodes[@]}"
        echo
        stat -c 'path=%n type=%F major_minor=%t:%T mode=%a owner=%U group=%G' \
            "${gpio_nodes[@]}"
    fi
} >"${output_dir}/gpio-device-nodes.txt" 2>&1

{
    shopt -s nullglob
    chip_paths=(/sys/bus/gpio/devices/gpiochip*)
    if ((${#chip_paths[@]} == 0)); then
        echo "No GPIO sysfs metadata found"
    fi
    for chip_path in "${chip_paths[@]}"; do
        echo "===== ${chip_path} ====="
        readlink -f "${chip_path}"
        for property in label base ngpio; do
            if [[ -r ${chip_path}/${property} ]]; then
                printf '%s: ' "${property}"
                cat "${chip_path}/${property}"
            fi
        done
        echo
    done
} >"${output_dir}/gpio-sysfs.txt" 2>&1

{
    for config_path in \
        "/boot/config-$(uname -r)" \
        "/boot/firmware/config-$(uname -r)"; do
        if [[ -r ${config_path} ]]; then
            echo "===== ${config_path} ====="
            grep -E '^CONFIG_(GPIO_CDEV|GPIO_CDEV_V1|GPIOLIB)=' \
                "${config_path}" || true
        fi
    done
    if [[ -r /proc/config.gz ]] && command -v zcat >/dev/null 2>&1; then
        echo "===== /proc/config.gz ====="
        zcat /proc/config.gz |
            grep -E '^CONFIG_(GPIO_CDEV|GPIO_CDEV_V1|GPIOLIB)=' || true
    fi
} >"${output_dir}/kernel-gpio-config.txt" 2>&1

{
    for boot_config in /boot/config.txt /boot/firmware/config.txt; do
        if [[ -r ${boot_config} ]]; then
            echo "===== ${boot_config} (GPIO-related settings only) ====="
            grep -E '^[[:space:]]*(arm_64bit|dtparam|dtoverlay|enable_uart|force_turbo|gpio|kernel)=' \
                "${boot_config}" || true
        fi
    done
} >"${output_dir}/boot-gpio-settings.txt" 2>&1

gpiod_version=$(pkg-config --modversion libgpiod 2>/dev/null || true)
if [[ -z ${gpiod_version} ]]; then
    gpiod_version=$(gpiodetect --version 2>/dev/null |
        sed -nE 's/.*[^0-9]([0-9]+\.[0-9]+(\.[0-9]+)?).*/\1/p' |
        head -1)
fi
gpiod_major=${gpiod_version%%.*}

broadcom_record=$(grep -E \
    '^[[:space:]]*gpiochip[0-9]+[[:space:]]+\[pinctrl-bcm2835\][[:space:]]+\([0-9]+ lines\)' \
    "${output_dir}/gpiodetect.txt" 2>/dev/null | head -1 || true)
broadcom_chip=
broadcom_line_count=
if [[ -n ${broadcom_record} ]]; then
    broadcom_chip=$(awk '{ print $1 }' <<<"${broadcom_record}")
    broadcom_line_count=$(sed -nE \
        's/.*\(([0-9]+) lines\).*/\1/p' <<<"${broadcom_record}")
fi

{
    echo "libgpiod_version: ${gpiod_version:-not detected}"
    echo "libgpiod_major: ${gpiod_major:-not detected}"
    echo "broadcom_gpiochip: ${broadcom_chip:-not detected}"
    echo "broadcom_line_count: ${broadcom_line_count:-not detected}"
    echo "gpiodetect_record: ${broadcom_record:-not detected}"
} >"${output_dir}/selected-target.txt"

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
        line=$(grep -E \
            "line[[:space:]]+${offset}:" \
            "${output_dir}/gpioinfo-selected-chip.txt" 2>/dev/null |
            head -1 || true)
        if [[ -n ${line} ]]; then
            echo "offset ${offset}: ${line}"
        else
            echo "offset ${offset}: NOT FOUND"
        fi
    done
} >"${output_dir}/gpioinfo-oclock-offsets.txt"

probe_source="${output_dir}/gpio-v2-lineinfo-probe.c"
probe_binary="${output_dir}/gpio-v2-lineinfo-probe"
cat >"${probe_source}" <<'EOF'
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    struct gpiochip_info chip = {0};
    int fd;

    if (argc != 2) {
        fprintf(stderr, "usage: %s /dev/gpiochipN\n", argv[0]);
        return 2;
    }

    fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open: %s\n", strerror(errno));
        return 3;
    }
    if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &chip) < 0) {
        fprintf(stderr, "GPIO_GET_CHIPINFO_IOCTL: %s\n", strerror(errno));
        close(fd);
        return 4;
    }
    printf("chip_name: %s\nchip_label: %s\nchip_lines: %u\n",
           chip.name, chip.label, chip.lines);

#ifdef GPIO_V2_GET_LINEINFO_IOCTL
    {
        struct gpio_v2_line_info line = {0};
        line.offset = 0;
        if (ioctl(fd, GPIO_V2_GET_LINEINFO_IOCTL, &line) < 0) {
            fprintf(stderr, "GPIO_V2_GET_LINEINFO_IOCTL: %s\n",
                    strerror(errno));
            close(fd);
            return 5;
        }
        printf("v2_line_info: available\n");
        printf("line_zero_name: %s\n", line.name);
    }
#else
    fprintf(stderr, "userspace headers do not define GPIO v2 line info\n");
    close(fd);
    return 6;
#endif

    close(fd);
    return 0;
}
EOF

probe_compile_status=1
probe_status=1
if command -v cc >/dev/null 2>&1; then
    {
        printf 'command: cc -std=c11 -Wall -Wextra -Werror %q -o %q\n\n' \
            "${probe_source}" "${probe_binary}"
        cc -std=c11 -Wall -Wextra -Werror \
            "${probe_source}" -o "${probe_binary}"
        probe_compile_status=$?
        echo
        echo "exit_status: ${probe_compile_status}"
    } >"${output_dir}/gpio-v2-probe-compile.txt" 2>&1
fi

if ((probe_compile_status == 0)) &&
        [[ ${broadcom_chip} =~ ^gpiochip[0-9]+$ ]]; then
    capture gpio-v2-probe "${probe_binary}" "/dev/${broadcom_chip}"
    probe_status=$(awk '/^exit_status:/ { print $2 }' \
        "${output_dir}/gpio-v2-probe.txt" | tail -1)
fi

{
    echo "This collector performs metadata-only GPIO inspection."
    echo "It never invokes GPIO line-request, value-read, value-write, monitor,"
    echo "or notification tools. Its compiled probe opens the selected chip"
    echo "read-only and issues only chip-info and v2 line-info ioctls."
    echo
    echo "The production Jessie SD card must remain unchanged and recoverable."
} >"${output_dir}/safety-notes.txt"

result_file="${output_dir}/collection-result.txt"
failures=0
warnings=0

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

echo "Phase 3 target-image checks" >"${result_file}"

if grep -q 'model: Raspberry Pi Zero' "${output_dir}/raspberry-pi.txt"; then
    result_ok "target is a Raspberry Pi Zero model"
else
    result_fail "target model is not a Raspberry Pi Zero"
fi

if grep -q '^armv6l$' "${output_dir}/machine.txt" &&
        grep -q '^armhf$' "${output_dir}/architecture.txt"; then
    result_ok "runtime is ARMv6 with the armhf package architecture"
else
    result_fail "expected armv6l runtime and armhf package architecture"
fi

if grep -Eq \
        "^VERSION_ID=\"?${expected_debian_version}\"?$" \
        "${output_dir}/operating-system.txt"; then
    result_ok "Debian ${expected_debian_version} target was captured"
else
    result_fail "target is not Debian ${expected_debian_version}"
fi

if [[ ${gpiod_major} == "${expected_libgpiod_major}" ]]; then
    result_ok "libgpiod major ${expected_libgpiod_major} is available"
else
    result_fail "expected libgpiod major ${expected_libgpiod_major}; found ${gpiod_version:-none}"
fi

if grep -q '^exit_status: 0$' "${output_dir}/gpiodetect.txt"; then
    result_ok "gpiodetect completed successfully"
else
    result_fail "gpiodetect did not complete successfully"
fi

if [[ ${broadcom_chip} =~ ^gpiochip[0-9]+$ &&
      ${broadcom_line_count} =~ ^[0-9]+$ ]] &&
        ((broadcom_line_count > 27)); then
    result_ok "pinctrl-bcm2835 chip was identified without assuming its number"
else
    result_fail "pinctrl-bcm2835 chip with required offsets was not identified"
fi

if ((gpioinfo_status == 0)); then
    result_ok "gpioinfo captured the selected Broadcom chip"
else
    result_fail "gpioinfo did not capture the selected Broadcom chip"
fi

missing_offsets=$(grep -c 'NOT FOUND' \
    "${output_dir}/gpioinfo-oclock-offsets.txt" || true)
if ((missing_offsets == 0)); then
    result_ok "gpioinfo contains every office-clock BCM offset"
else
    result_fail "gpioinfo is missing ${missing_offsets} office-clock BCM offset(s)"
fi

if ((probe_compile_status == 0)); then
    result_ok "GPIO ABI probe compiled against the target headers"
else
    result_fail "GPIO ABI probe did not compile"
fi

if [[ ${probe_status} == 0 ]] &&
        grep -q '^v2_line_info: available$' \
            "${output_dir}/gpio-v2-probe.txt" 2>/dev/null; then
    result_ok "kernel and headers support GPIO character-device ABI v2 line info"
else
    result_fail "GPIO character-device ABI v2 line-info probe failed"
fi

if grep -Eq $'^libgpiod-dev(:armhf)?\t.*\tarmhf\tinstall ok installed$' \
        "${output_dir}/gpio-packages.txt"; then
    result_ok "armhf libgpiod development package is installed"
else
    result_fail "armhf libgpiod development package is not installed"
fi

if grep -q '^throttled=0x0$' "${output_dir}/throttling.txt" 2>/dev/null; then
    result_ok "firmware reports no current or historical throttling"
elif [[ -f ${output_dir}/throttling.txt ]]; then
    result_warn "review the firmware throttling result"
else
    result_warn "vcgencmd throttling evidence is unavailable"
fi

{
    echo
    echo "failures: ${failures}"
    echo "warnings: ${warnings}"
} >>"${result_file}"

archive_path="${output_dir%/}.tar.gz"
archive_parent=$(dirname "${output_dir}")
archive_name=$(basename "${output_dir}")
tar -czf "${archive_path}" -C "${archive_parent}" "${archive_name}" ||
    die "could not create archive"
sha256sum "${archive_path}" >"${archive_path}.sha256"

if [[ -n ${SUDO_UID:-} && -n ${SUDO_GID:-} ]]; then
    chown "${SUDO_UID}:${SUDO_GID}" "${archive_path}" \
        "${archive_path}.sha256" 2>/dev/null || true
fi

echo
cat "${result_file}"
echo
echo "Share this archive: ${archive_path}"
echo "Archive checksum:   ${archive_path}.sha256"
echo "Result directory:   ${output_dir}"

if ((failures > 0)); then
    echo "Target-image collection has ${failures} required failure(s)." >&2
    exit 1
fi
