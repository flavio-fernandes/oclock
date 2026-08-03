#!/usr/bin/env bash

# Benchmark repeated all-off LPD8806 frames with the already accepted transfer
# helper. Bind once, run 25 measured show() calls, unbind immediately, then ask
# for the visual observation. No application or ADC read is involved.

set -euo pipefail

frame_count=25
tick_budget_microseconds=12000
tool=
commit=
speed_hz=2000000
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
manager=${script_dir}/managePhase5Lpd8806Binding.sh
binding_active=0
output_dir=

usage() {
    cat <<'EOF'
Usage: sudo misc/verifyPhase5Lpd8806Cadence.sh \
  --tool PATH --commit GIT_COMMIT [--speed-hz HZ]

Requires the accepted Zero W/Trixie overlay, inactive service, native MCP3002
binding, and an initially unbound strip. After the explicit BENCHMARK prompt,
it binds the strip once and runs 25 all-off frames through the accepted helper.
The strip is unbound before the final visual question.
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
[[ -x ${tool} ]] || die "all-off helper is not executable: ${tool}"

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
output_dir=$(mktemp -d "/tmp/oclock-phase5-lpd-cadence-${timestamp}-XXXXXXXX")
samples_file=${output_dir}/samples.tsv
result_file=${output_dir}/result.txt
printf 'frame\telapsed_microseconds\n' >"${samples_file}"

{
    printf 'captured_at_utc=%s\n' "${timestamp}"
    printf 'source_commit=%s\n' "${commit}"
    printf 'model=%s\n' "${model}"
    printf 'architecture=%s/%s\n' "$(uname -m)" "$(dpkg --print-architecture)"
    printf 'os_codename=%s\n' "${VERSION_CODENAME}"
    printf 'frame_count=%d\n' "${frame_count}"
    printf 'speed_hz=%d\n' "${speed_hz}"
    printf 'tick_budget_microseconds=%d\n' "${tick_budget_microseconds}"
} >"${output_dir}/context.txt"
file "${tool}" >"${output_dir}/tool-file.txt"
ldd "${tool}" >"${output_dir}/tool-ldd.txt"
sha256sum "${tool}" >"${output_dir}/tool.sha256"
printf '%s\n' "${initial_status}" >"${output_dir}/status-before.txt"
vcgencmd get_throttled >"${output_dir}/throttling-before.txt" 2>&1 || true

echo "Candidate helper:   ${tool}"
echo "Candidate commit:   ${commit}"
echo "Authorized output:  ${frame_count} all-off frames at ${speed_hz} Hz"
echo "Acceptance budget:  every show() call <= ${tick_budget_microseconds} us"
echo
echo "The entire strip should remain completely dark and stable."
printf 'Type BENCHMARK to bind and begin: '
read -r answer
[[ ${answer} == BENCHMARK ]] || die "benchmark was not authorized"

"${manager}" bind --confirm BIND >"${output_dir}/bind.txt" 2>&1 ||
    die "strip binding failed"
binding_active=1

operator=${SUDO_USER:-root}
sample_failures=0
for ((frame = 1; frame <= frame_count; ++frame)); do
    status=0
    output=$(timeout --signal=TERM --kill-after=2s 15s \
        runuser -u "${operator}" -- "${tool}" \
        2>>"${output_dir}/transfer-stderr.txt") || status=$?
    elapsed=$(sed -n 's/^elapsed_microseconds=//p' <<<"${output}")
    if ((status != 0)) ||
            ! grep -Fqx 'led_count=240' <<<"${output}" ||
            ! grep -Fqx 'data_bytes=720' <<<"${output}" ||
            ! grep -Fqx 'latch_bytes=8' <<<"${output}" ||
            ! grep -Fqx 'payload_bytes=728' <<<"${output}" ||
            ! grep -Fqx "speed_hz=${speed_hz}" <<<"${output}" ||
            [[ ! ${elapsed} =~ ^[1-9][0-9]*$ ]]; then
        echo "invalid frame=${frame} status=${status}" \
            >>"${output_dir}/sample-errors.txt"
        sample_failures=$((sample_failures + 1))
    else
        printf '%d\t%d\n' "${frame}" "${elapsed}" >>"${samples_file}"
    fi
done

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
printf 'Did the entire LED strip remain off and stable? [yes/no]: '
read -r visual_answer

tail -n +2 "${samples_file}" | cut -f2 | sort -n \
    >"${output_dir}/elapsed-sorted.txt"
valid_frames=$(wc -l <"${output_dir}/elapsed-sorted.txt")
if ((valid_frames > 0)); then
    minimum=$(head -1 "${output_dir}/elapsed-sorted.txt")
    maximum=$(tail -1 "${output_dir}/elapsed-sorted.txt")
    median_index=$(((valid_frames + 1) / 2))
    p95_index=$(((95 * valid_frames + 99) / 100))
    median=$(sed -n "${median_index}p" "${output_dir}/elapsed-sorted.txt")
    p95=$(sed -n "${p95_index}p" "${output_dir}/elapsed-sorted.txt")
    mean=$(awk '{sum += $1} END {if (NR) printf "%.1f", sum / NR}' \
        "${output_dir}/elapsed-sorted.txt")
    within_budget=$(awk -v budget="${tick_budget_microseconds}" \
        '$1 <= budget {count++} END {print count + 0}' \
        "${output_dir}/elapsed-sorted.txt")
else
    minimum=0
    maximum=0
    median=0
    p95=0
    mean=0
    within_budget=0
fi

{
    printf 'valid_frames=%d\n' "${valid_frames}"
    printf 'minimum_microseconds=%s\n' "${minimum}"
    printf 'median_microseconds=%s\n' "${median}"
    printf 'p95_microseconds=%s\n' "${p95}"
    printf 'maximum_microseconds=%s\n' "${maximum}"
    printf 'mean_microseconds=%s\n' "${mean}"
    printf 'frames_within_12000_microseconds=%d\n' "${within_budget}"
} >"${output_dir}/summary.txt"

failures=0
warnings=0
{
    echo "Phase 5 LPD8806 repeated-cadence checks"
    if ((sample_failures == 0 && valid_frames == frame_count)); then
        echo "OK: all ${frame_count} all-off frames returned valid metadata"
    else
        echo "FAIL: ${sample_failures} frame(s) failed; ${valid_frames}/${frame_count} valid"
        failures=$((failures + 1))
    fi
    if ((valid_frames == frame_count && within_budget == frame_count)); then
        echo "OK: every show() call met the 12 ms tick budget"
    else
        echo "FAIL: ${within_budget}/${frame_count} show() calls met the 12 ms tick budget"
        failures=$((failures + 1))
    fi
    if [[ ${visual_answer,,} == yes || ${visual_answer,,} == y ]]; then
        echo "OK: operator confirmed the strip remained off and stable"
    else
        echo "FAIL: operator did not confirm an off and stable strip"
        failures=$((failures + 1))
    fi
    if grep -Fq 'Driver:             none' "${output_dir}/status-after.txt" &&
            grep -Fq 'not present' "${output_dir}/status-after.txt"; then
        echo "OK: strip binding and character device were removed"
    else
        echo "FAIL: strip did not return to the unbound state"
        failures=$((failures + 1))
    fi
    if grep -Fq '(mcp320x)' "${output_dir}/status-after.txt"; then
        echo "OK: MCP3002 remained bound to its native driver"
    else
        echo "FAIL: MCP3002 native binding was not preserved"
        failures=$((failures + 1))
    fi
    if systemctl is-active --quiet oclock; then
        echo "FAIL: oclock.service became active"
        failures=$((failures + 1))
    else
        echo "OK: oclock.service remained inactive"
    fi
    if grep -Fqx 'throttled=0x0' "${output_dir}/throttling-after.txt"; then
        echo "OK: firmware reported no current or historical throttling"
    else
        echo "WARNING: review the captured firmware throttling result"
        warnings=$((warnings + 1))
    fi
    echo
    echo "failures: ${failures}"
    echo "warnings: ${warnings}"
    echo
    echo "The benchmark used repeated standalone all-off frames."
    echo "The whole application and ADC were not run."
} >"${result_file}"

cat "${result_file}"
echo
cat "${output_dir}/summary.txt"

archive=${output_dir}.tar.gz
tar -czf "${archive}" -C /tmp "$(basename "${output_dir}")"
(
    cd /tmp
    sha256sum "$(basename "${archive}")" \
        >"$(basename "${archive}").sha256"
)
chmod 0644 "${archive}" "${archive}.sha256"
if [[ -n ${SUDO_UID:-} && -n ${SUDO_GID:-} ]]; then
    chown "${SUDO_UID}:${SUDO_GID}" "${archive}" "${archive}.sha256"
fi

echo
echo "Share this archive: ${archive}"
echo "Archive checksum:   ${archive}.sha256"
echo "Result directory:   ${output_dir}"

((failures == 0)) || die "LPD8806 cadence gate recorded ${failures} failure(s)"
