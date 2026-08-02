#!/bin/bash

# Inspect an opt-in native ARM LPD8806/spidev build without executing it or
# accessing any GPIO, SPI, IIO, service, or boot interface.

set -u
set -o pipefail

binary_path=
source_commit=
output_dir=
expected_model="Raspberry Pi Zero W Rev 1.1"
expected_revision=9000c1
strip_node_suffix=/oclock-strip-spi/lpd8806@0

usage()
{
    cat <<EOF
Usage: $0 --binary PATH --commit SHA [--output DIRECTORY]

Inspect the native opt-in strip binary, its architecture and dynamic
dependencies, the Linux SPI header provider, and the Device Tree discovery
marker. The binary is never executed and no hardware or service is accessed.
EOF
}

die()
{
    echo "error: $*" >&2
    exit 2
}

while (($# > 0)); do
    case "$1" in
        --binary)
            (($# >= 2)) || die "--binary requires a value"
            binary_path=$2
            shift 2
            ;;
        --commit)
            (($# >= 2)) || die "--commit requires a value"
            source_commit=$2
            shift 2
            ;;
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

[[ -n ${binary_path} ]] || die "--binary is required"
[[ -n ${source_commit} ]] || die "--commit is required"
[[ ${source_commit} =~ ^[0-9a-f]{40}$ ]] ||
    die "--commit must be a full lowercase SHA"

for command_name in awk basename date dirname dpkg dpkg-query file grep ldd \
        mkdir mktemp readlink sha256sum strings tail tar tr uname; do
    command -v "${command_name}" >/dev/null 2>&1 ||
        die "required command is missing: ${command_name}"
done

binary_path=$(readlink -f "${binary_path}") || die "cannot resolve binary"
[[ -f ${binary_path} && -x ${binary_path} ]] ||
    die "binary is not an executable regular file: ${binary_path}"

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
umask 077
if [[ -z ${output_dir} ]]; then
    output_dir=$(mktemp -d "/tmp/oclock-phase5-lpd-build-${timestamp}-XXXXXXXX") ||
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
    echo "Office Clock Phase 5 native LPD8806/spidev build inspection"
    echo
    echo "collected_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "source_commit: ${source_commit}"
    echo "binary_path: ${binary_path}"
    echo "binary_sha256: $(sha256sum "${binary_path}" | awk '{ print $1 }')"
    echo "model: ${model}"
    echo "revision: ${revision:-unknown}"
    echo "kernel: $(uname -srvm)"
    echo "machine: ${machine}"
    echo "architecture: ${architecture}"
    echo "os_codename: ${os_codename}"
} >"${output_dir}/context.txt"

{
    echo "This collector does not execute or copy the candidate binary."
    echo "It does not open SPI/IIO, request GPIO, bind a driver, change a"
    echo "service, package, boot file, or module, or perform a transfer."
} >"${output_dir}/safety.txt"

file "${binary_path}" >"${output_dir}/file.txt" 2>&1
ldd "${binary_path}" >"${output_dir}/ldd.txt" 2>&1
sha256sum "${binary_path}" >"${output_dir}/binary.sha256"
dpkg-query -S /usr/include/linux/spi/spidev.h \
    >"${output_dir}/spidev-header-provider.txt" 2>&1
header_provider_status=$?

{
    strings "${binary_path}" | awk -v marker="${strip_node_suffix}" '
        $0 == marker { print "strip_node_suffix=" $0; found = 1 }
        $0 ~ /^\/dev\/spidev[0-9]+\.[0-9]+$/ {
            print "hardcoded_device=" $0; hardcoded = 1
        }
        END {
            if (!found) print "strip_node_suffix=NOT FOUND"
            if (!hardcoded) print "hardcoded_device=none"
        }'
} >"${output_dir}/binary-markers.txt"

echo "Phase 5 native LPD8806/spidev build checks" >"${result_file}"

if [[ ${model} == "${expected_model}" &&
      ${revision:-unknown} == "${expected_revision}" &&
      ${machine} == armv6l && ${architecture} == armhf &&
      ${os_codename} == trixie ]]; then
    ok "target is the selected Zero W/Trixie ARMv6 image"
else
    fail "target identity does not match the selected Zero W/Trixie image"
fi

if grep -Eq 'ELF 32-bit LSB.*ARM.*EABI5' "${output_dir}/file.txt"; then
    ok "candidate is a 32-bit ARM EABI5 executable"
else
    fail "candidate architecture is unexpected"
fi

if grep -q 'libgpiod\.so\.3 =>' "${output_dir}/ldd.txt"; then
    ok "candidate resolves libgpiod major 3"
else
    fail "candidate does not resolve libgpiod major 3"
fi

if grep -q 'libatomic\.so\.1 =>' "${output_dir}/ldd.txt"; then
    ok "candidate resolves the ARM atomic runtime"
else
    fail "candidate does not resolve the ARM atomic runtime"
fi

if grep -q 'libwiringPi' "${output_dir}/ldd.txt"; then
    fail "candidate still resolves WiringPi"
else
    ok "candidate has no WiringPi dependency"
fi

if grep -Fqx "strip_node_suffix=${strip_node_suffix}" \
        "${output_dir}/binary-markers.txt" &&
        grep -Fqx 'hardcoded_device=none' \
        "${output_dir}/binary-markers.txt"; then
    ok "candidate discovers the strip without a hard-coded SPI bus number"
else
    fail "candidate SPI discovery markers are unexpected"
fi

if ((header_provider_status == 0)); then
    ok "installed package owns the Linux spidev userspace header"
else
    fail "Linux spidev userspace header provider was not identified"
fi

{
    echo
    echo "failures: ${failures}"
    echo
    echo "The candidate was inspected but not executed or copied into this archive."
    echo "No device was opened and no hardware or system state was changed."
} >>"${result_file}"

cat "${result_file}"
if ((failures > 0)); then
    die "native LPD8806 build inspection recorded ${failures} failure(s)"
fi
