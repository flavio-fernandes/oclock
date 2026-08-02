#!/usr/bin/env bash

# Drive one guarded colored LPD8806 sequence through the reviewed runtime
# spidev binding. This is the first gate that latches non-zero pixel data, so
# it is the first that can expose a signal-integrity problem at the higher
# clock rate. The helper ends with an all-off frame; the strip is unbound
# before the visual question. No application or ADC read is involved.

set -euo pipefail

tick_budget_microseconds=12000
expected_brightness=63
expected_frames=4
tool=
commit=
speed_hz=2000000
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
manager=${script_dir}/managePhase5Lpd8806Binding.sh
binding_active=0
output_dir=
failures=0
warnings=0

usage() {
    cat <<'EOF'
Usage: sudo misc/verifyPhase5Lpd8806Colors.sh \
  --tool PATH --commit GIT_COMMIT [--speed-hz HZ]

Requires the accepted Zero W/Trixie overlay, inactive service, native MCP3002
binding, and an initially unbound strip. After the explicit COLORS prompt, it
binds the strip once and runs a red/green/blue/off sequence at half brightness
through the named helper. The strip is unbound before the visual question.
EOF
}

die() {
    echo "error: $*" >&2
    exit 1
}

cleanup() {
    if ((binding_active)); then
        echo "Emergency rollback: unbinding the strip..." >&2
        "${manager}" unbind --confirm UNBIND >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT INT TERM HUP

while (($# > 0)); do
    case "$1" in
        --tool)
            (($# >= 2)) || die "--tool requires a value"
            tool=$2
            shift 2
            ;;
        --commit)
            (($# >= 2)) || die "--commit requires a value"
            commit=$2
            shift 2
            ;;
        --speed-hz)
            (($# >= 2)) || die "--speed-hz requires a value"
            speed_hz=$2
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
done

[[ ${EUID} -eq 0 ]] || die "run this verifier with sudo"
[[ -n ${tool} && -n ${commit} ]] || {
    usage >&2
    exit 2
}
[[ ${commit} =~ ^[0-9a-f]{40}$ ]] || die "commit must be a full Git SHA"
[[ ${speed_hz} == 1000000 || ${speed_hz} == 2000000 ]] ||
    die "--speed-hz must be 1000000 or the reviewed 2000000 experiment"
[[ -x ${manager} ]] || die "binding manager is missing: ${manager}"
tool=$(realpath "${tool}")
[[ -x ${tool} ]] || die "color helper is not executable: ${tool}"

model=$(tr -d '\0' </proc/device-tree/model 2>/dev/null || true)
[[ ${model} == "Raspberry Pi Zero W Rev 1.1" ]] ||
    die "target is not the accepted Raspberry Pi Zero W revision"
[[ $(uname -m) == armv6l ]] || die "target is not ARMv6"
[[ $(dpkg --print-architecture) == armhf ]] || die "target is not armhf"
# This fixed system file is present on the selected target.
# shellcheck disable=SC1091
. /etc/os-release
[[ ${VERSION_CODENAME:-} == trixie ]] || die "target is not Trixie"
systemctl is-active --quiet oclock && die "oclock.service must be inactive"

file "${tool}" | grep -Fq 'ELF 32-bit LSB executable, ARM, EABI5' ||
    die "helper is not an ARM EABI5 executable"
if ldd "${tool}" | grep -q libwiringPi; then
    die "helper unexpectedly resolves WiringPi"
fi

initial_status=$("${manager}" status)
grep -Fq 'Driver:             none' <<<"${initial_status}" ||
    die "strip is not initially unbound"
grep -Fq 'not present' <<<"${initial_status}" ||
    die "strip character device is unexpectedly present"
grep -Fq '(mcp320x)' <<<"${initial_status}" ||
    die "MCP3002 is not bound to mcp320x"
grep -Fq 'oclock.service:     inactive' <<<"${initial_status}" ||
    die "oclock.service is not inactive"

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
umask 077
output_dir=$(mktemp -d "/tmp/oclock-phase5-lpd-colors-${timestamp}-XXXXXXXX")
archive_path="${output_dir%/}.tar.gz"
result_file=${output_dir}/result.txt

ok() { echo "OK: $*" >>"${result_file}"; }
fail() { echo "FAIL: $*" >>"${result_file}"; failures=$((failures + 1)); }
warn() { echo "WARNING: $*" >>"${result_file}"; warnings=$((warnings + 1)); }

{
    printf 'captured_at_utc=%s\n' "${timestamp}"
    printf 'source_commit=%s\n' "${commit}"
    printf 'model=%s\n' "${model}"
    printf 'architecture=%s/%s\n' "$(uname -m)" "$(dpkg --print-architecture)"
    printf 'os_codename=%s\n' "${VERSION_CODENAME}"
    printf 'speed_hz=%d\n' "${speed_hz}"
    printf 'expected_brightness=%d\n' "${expected_brightness}"
    printf 'tick_budget_microseconds=%d\n' "${tick_budget_microseconds}"
} >"${output_dir}/context.txt"
file "${tool}" >"${output_dir}/tool-file.txt"
ldd "${tool}" >"${output_dir}/tool-ldd.txt"
sha256sum "${tool}" >"${output_dir}/tool.sha256"
printf '%s\n' "${initial_status}" >"${output_dir}/status-before.txt"
vcgencmd get_throttled >"${output_dir}/throttling-before.txt" 2>&1 || true

{
    echo "One red/green/blue/off LPD8806 sequence is authorized after"
    echo "confirmation. The partial oclock application is not executed."
    echo "The helper ends with an all-off frame because LPD8806 pixels retain"
    echo "their last latched value after the runtime binding is removed."
} >"${output_dir}/safety.txt"

echo "Candidate helper:   ${tool}"
echo "Candidate commit:   ${commit}"
echo "Authorized output:  red, green, blue at brightness ${expected_brightness}/127,"
echo "                    about 3 s each, then a final all-off frame"
echo "Acceptance budget:  every show() call <= ${tick_budget_microseconds} us"
echo
echo "Expect: whole strip solid red, then solid green, then solid blue,"
echo "then completely dark. Uniform color, no stray or wrong-colored pixels."
printf 'Type COLORS to bind and begin: '
read -r answer
[[ ${answer} == COLORS ]] || die "color sequence was not authorized"

"${manager}" bind --confirm BIND >"${output_dir}/bind.txt" 2>&1 ||
    die "strip binding failed"
binding_active=1

operator=${SUDO_USER:-root}
transfer_status=0
timeout --signal=TERM --kill-after=5s 40s \
    runuser -u "${operator}" -- "${tool}" \
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
vcgencmd get_throttled >"${output_dir}/throttling-after.txt" 2>&1 || true

echo
printf 'Did you see solid red, then green, then blue, each uniform, ending dark? [yes/no]: '
read -r visual_answer

echo "Phase 5 LPD8806 colored-sequence checks" >"${result_file}"
ok "target is the selected Zero W/Trixie ARMv6 image"
ok "helper is ARM EABI5 and has no WiringPi dependency"
ok "strip began unbound with the MCP3002 on its native driver"

if ((transfer_status == 0)); then
    ok "color helper returned success"
else
    fail "color helper returned status ${transfer_status}"
fi

for expected_line in led_count=240 data_bytes=720 latch_bytes=8 \
        payload_bytes=728 speed_hz="${speed_hz}" \
        brightness="${expected_brightness}" \
        sequence=red,green,blue,off final_state=off \
        frames_sent="${expected_frames}"; do
    if grep -Fqx "${expected_line}" "${output_dir}/transfer.txt"; then
        ok "helper reported ${expected_line}"
    else
        fail "helper did not report ${expected_line}"
    fi
done

for color in red green blue off; do
    if grep -Eq "^frame_${color}_microseconds=[1-9][0-9]*$" \
            "${output_dir}/transfer.txt"; then
        ok "${color} frame wall time was captured"
    else
        fail "${color} frame wall time is missing or invalid"
    fi
done

max_frame=$(sed -n 's/^max_frame_microseconds=//p' "${output_dir}/transfer.txt")
if [[ ${max_frame} =~ ^[1-9][0-9]*$ ]]; then
    if ((max_frame <= tick_budget_microseconds)); then
        ok "slowest colored frame ${max_frame} us met the ${tick_budget_microseconds} us budget"
    else
        fail "slowest colored frame ${max_frame} us exceeded the budget"
    fi
else
    fail "maximum frame time is missing or invalid"
fi

if [[ ${visual_answer,,} == yes || ${visual_answer,,} == y ]]; then
    ok "operator confirmed the expected uniform color sequence ending dark"
else
    fail "operator did not confirm the expected color sequence"
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
    echo "One standalone colored sequence was attempted at half brightness."
    echo "The whole application was not run and the ADC was not read."
} >>"${result_file}"

cat "${result_file}"

tar -czf "${archive_path}" -C "$(dirname "${output_dir}")" \
    "$(basename "${output_dir}")"
(
    cd "$(dirname "${archive_path}")" &&
        sha256sum "$(basename "${archive_path}")" \
            >"$(basename "${archive_path}").sha256"
)
if [[ -n ${SUDO_UID:-} && -n ${SUDO_GID:-} ]]; then
    chown "${SUDO_UID}:${SUDO_GID}" "${archive_path}" \
        "${archive_path}.sha256" 2>/dev/null || true
fi
echo "Share this archive: ${archive_path}" >&2
echo "Archive checksum:   ${archive_path}.sha256" >&2

if ((failures > 0)); then
    die "colored gate recorded ${failures} failure(s)"
fi
