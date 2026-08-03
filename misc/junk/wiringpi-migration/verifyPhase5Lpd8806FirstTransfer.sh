#!/bin/bash

# Perform exactly one guarded LPD8806 all-off transfer through the reviewed
# spidev binding. The partial Office Clock application is never executed.

set -u
set -o pipefail

transfer_tool=
source_commit=
output_dir=
binding_active=0
manager=
result_file=
failures=0
warnings=0
expected_speed_hz=2000000

expected_model="Raspberry Pi Zero W Rev 1.1"
expected_revision=9000c1
expected_overlay_sha256="53b593f4c30a78c8beb446cf506816af29a817d0daf93bb899a9054c7aa61959"
installed_overlay=/boot/firmware/overlays/oclock-spi.dtbo

usage()
{
    cat <<EOF
Usage: sudo $0 --tool PATH --commit SHA [--speed-hz HZ] [--output DIRECTORY]

After an explicit TRANSFER confirmation, bind only the dynamically discovered
Office Clock strip child, run the standalone all-off tool once, immediately
unbind it, and collect timing and rollback evidence. The tool submits one
728-byte frame: 720 bytes of 0x80 (240 off pixels) and eight zero latch bytes.

The verifier refuses an active oclock.service or unexpected target/overlay.
It never starts the full application, reads the ADC, changes boot files,
rewires GPIO, changes packages, or leaves a deliberate persistent binding.
EOF
}

die()
{
    echo "error: $*" >&2
    exit 2
}

while (($# > 0)); do
    case "$1" in
        --tool)
            (($# >= 2)) || die "--tool requires a value"
            transfer_tool=$2
            shift 2
            ;;
        --commit)
            (($# >= 2)) || die "--commit requires a value"
            source_commit=$2
            shift 2
            ;;
        --speed-hz)
            (($# >= 2)) || die "--speed-hz requires a value"
            expected_speed_hz=$2
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

((EUID == 0)) || die "run this verifier with sudo"
[[ -n ${transfer_tool} ]] || die "--tool is required"
[[ -n ${source_commit} ]] || die "--commit is required"
[[ ${source_commit} =~ ^[0-9a-f]{40}$ ]] ||
    die "--commit must be a full lowercase SHA"
[[ ${expected_speed_hz} == 1000000 || ${expected_speed_hz} == 2000000 ]] ||
    die "--speed-hz must be 1000000 or the reviewed 2000000 experiment"

for command_name in awk basename cat chown date dirname dmesg dpkg file grep \
        ldd mkdir mktemp readlink sed sha256sum stat strings systemctl tail tar \
        timeout tr uname vcgencmd; do
    command -v "${command_name}" >/dev/null 2>&1 ||
        die "required command is missing: ${command_name}"
done

script_dir=$(dirname "$(readlink -f "$0")") || die "cannot resolve script"
manager="${script_dir}/managePhase5Lpd8806Binding.sh"
[[ -x ${manager} ]] || die "binding manager is missing or not executable"

transfer_tool=$(readlink -f "${transfer_tool}") || die "cannot resolve tool"
[[ -f ${transfer_tool} && -x ${transfer_tool} ]] ||
    die "transfer tool is not an executable regular file"

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
umask 077
if [[ -z ${output_dir} ]]; then
    output_dir=$(mktemp -d "/tmp/oclock-phase5-lpd-transfer-${timestamp}-XXXXXXXX") ||
        die "cannot create result directory"
else
    [[ ${output_dir} != / ]] || die "unsafe output directory"
    mkdir -p "$(dirname "${output_dir}")" || die "cannot create output parent"
    mkdir "${output_dir}" ||
        die "output directory already exists or cannot be created"
fi

archive_path="${output_dir%/}.tar.gz"
result_file="${output_dir}/result.txt"

finish()
{
    local rc=$?
    trap - EXIT
    set +e
    if ((binding_active)); then
        echo "Emergency rollback: unbinding the strip..." >&2
        if "${manager}" unbind --confirm UNBIND \
                >>"${output_dir}/emergency-unbind.txt" 2>&1; then
            binding_active=0
        else
            echo "ERROR: emergency strip unbind failed; reboot clears the runtime binding." >&2
            rc=1
        fi
    fi
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

warn()
{
    echo "WARNING: $*" >>"${result_file}"
    warnings=$((warnings + 1))
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

file "${transfer_tool}" >"${output_dir}/tool-file.txt" 2>&1
ldd "${transfer_tool}" >"${output_dir}/tool-ldd.txt" 2>&1
sha256sum "${transfer_tool}" >"${output_dir}/tool.sha256"
strings "${transfer_tool}" >"${output_dir}/tool-strings.txt"
{
    echo "Office Clock Phase 5 LPD8806 first-transfer gate"
    echo
    echo "collected_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "source_commit: ${source_commit}"
    echo "expected_speed_hz: ${expected_speed_hz}"
    echo "transfer_tool: ${transfer_tool}"
    echo "model: ${model}"
    echo "revision: ${revision:-unknown}"
    echo "kernel: $(uname -srvm)"
    echo "machine: ${machine}"
    echo "architecture: ${architecture}"
    echo "os_codename: ${os_codename}"
} >"${output_dir}/context.txt"

{
    echo "Exactly one controlled all-off frame is authorized after confirmation."
    echo "The partial oclock application is not executed."
    echo "The strip binding is runtime-only and is removed immediately after transfer."
    echo "The verifier does not read IIO, request GPIO, edit boot files, or reboot."
} >"${output_dir}/safety.txt"

[[ ${model} == "${expected_model}" ]] ||
    die "expected ${expected_model}; found ${model}"
[[ ${revision:-unknown} == "${expected_revision}" ]] ||
    die "expected revision ${expected_revision}; found ${revision:-unknown}"
[[ ${machine} == armv6l && ${architecture} == armhf &&
   ${os_codename} == trixie ]] || die "expected ARMv6/armhf Trixie target"
[[ -f ${installed_overlay} ]] || die "installed overlay is missing"
printf '%s  %s\n' "${expected_overlay_sha256}" "${installed_overlay}" |
    sha256sum --check --status || die "installed overlay checksum is unexpected"
if systemctl is-active --quiet oclock; then
    die "oclock.service is active; keep it stopped during this gate"
fi
grep -Eq 'ELF 32-bit LSB.*ARM.*EABI5' "${output_dir}/tool-file.txt" ||
    die "transfer tool is not the expected ARM EABI5 executable"
if grep -q 'libwiringPi' "${output_dir}/tool-ldd.txt"; then
    die "transfer tool unexpectedly resolves WiringPi"
fi
grep -Fqx '/oclock-strip-spi/lpd8806@0' "${output_dir}/tool-strings.txt" ||
    die "transfer tool lacks dynamic strip discovery"

"${manager}" status >"${output_dir}/status-before.txt" 2>&1 ||
    die "initial binding status failed"
grep -Fq 'Driver:             none' "${output_dir}/status-before.txt" ||
    die "strip must be unbound before this gate"

echo "Candidate tool:      ${transfer_tool}"
echo "Candidate commit:    ${source_commit}"
echo "Authorized transfer: one 728-byte all-off LPD8806 frame at ${expected_speed_hz} Hz"
echo
echo "The strip should remain completely dark and stable."
printf 'Type TRANSFER to bind, send once, and unbind: ' >&2
read -r confirmation
[[ ${confirmation} == TRANSFER ]] || die "transfer was not confirmed"

dmesg | tail -200 >"${output_dir}/dmesg-before.txt" 2>&1 || true
vcgencmd get_throttled >"${output_dir}/throttling-before.txt" 2>&1 || true

"${manager}" bind --confirm BIND >"${output_dir}/bind.txt" 2>&1 ||
    die "strip binding failed"
binding_active=1

transfer_status=0
timeout --signal=TERM --kill-after=5s 15s "${transfer_tool}" \
    >"${output_dir}/transfer.txt" 2>"${output_dir}/transfer-stderr.txt" ||
    transfer_status=$?

if "${manager}" unbind --confirm UNBIND \
        >"${output_dir}/unbind.txt" 2>&1; then
    binding_active=0
else
    die "normal strip unbind failed; emergency rollback will be attempted"
fi

"${manager}" status >"${output_dir}/status-after.txt" 2>&1 ||
    die "final binding status failed"
dmesg | tail -200 >"${output_dir}/dmesg-after.txt" 2>&1 || true
vcgencmd get_throttled >"${output_dir}/throttling-after.txt" 2>&1 || true
systemctl show oclock -p LoadState -p ActiveState -p SubState -p MainPID \
    -p FragmentPath >"${output_dir}/service-after.txt" 2>&1

printf 'Did the entire LED strip remain off and stable? [yes/no]: ' >&2
read -r visual_answer

echo "Phase 5 LPD8806 first-transfer checks" >"${result_file}"
ok "target is the selected Zero W/Trixie ARMv6 image"
ok "installed overlay matches the accepted artifact"
ok "oclock.service was inactive before the transfer"
ok "transfer tool is ARM EABI5 and has no WiringPi dependency"
ok "strip began unbound and was discovered without a bus-number assumption"

if ((transfer_status == 0)); then
    ok "standalone transfer tool returned success"
else
    fail "standalone transfer tool returned status ${transfer_status}"
fi

for expected_line in led_count=240 data_bytes=720 latch_bytes=8 \
        payload_bytes=728 speed_hz="${expected_speed_hz}"; do
    if grep -Fqx "${expected_line}" "${output_dir}/transfer.txt"; then
        ok "transfer reported ${expected_line}"
    else
        fail "transfer did not report ${expected_line}"
    fi
done

if grep -Eq '^elapsed_microseconds=[1-9][0-9]*$' \
        "${output_dir}/transfer.txt"; then
    ok "transfer wall time was captured"
else
    fail "transfer wall time is missing or invalid"
fi

if [[ ${visual_answer,,} == yes || ${visual_answer,,} == y ]]; then
    ok "operator confirmed the strip remained off and stable"
else
    fail "operator did not confirm an off and stable strip"
fi

if grep -Fq 'Driver:             none' "${output_dir}/status-after.txt" &&
        grep -Fq 'not present' "${output_dir}/status-after.txt"; then
    ok "strip binding and character device were removed"
else
    fail "strip did not return to the unbound state"
fi
if grep -Fq '(mcp320x)' "${output_dir}/status-after.txt"; then
    ok "MCP3002 remained bound to its native driver"
else
    fail "MCP3002 native binding was not preserved"
fi
if systemctl is-active --quiet oclock; then
    fail "oclock.service became active"
else
    ok "oclock.service remained inactive"
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
    echo "Exactly one standalone all-off strip frame was attempted."
    echo "The whole application was not run, the ADC was not read, and the"
    echo "runtime strip binding was removed before the operator prompt."
} >>"${result_file}"

cat "${result_file}"
if ((failures > 0)); then
    die "first-transfer gate recorded ${failures} failure(s)"
fi
