#!/usr/bin/env bash

# Drive one guarded HT1632 matrix sequence and benchmark repeated worst-case
# renders. The matrix GPIOs are not claimed by the SPI overlay, so this gate
# needs no spidev binding and touches neither strip nor ADC. The helper blanks
# the panel before returning. No application is started.

set -euo pipefail

tick_budget_microseconds=12000
expected_renders=20
tool=
commit=
output_dir=
failures=0
warnings=0
matrix_gpios=(6 13 19 26)

usage() {
    cat <<'EOF'
Usage: sudo misc/verifyPhase5Ht1632Render.sh --tool PATH --commit GIT_COMMIT

Requires the accepted Zero W/Trixie target, an inactive oclock.service, and
unclaimed matrix GPIOs. After the explicit RENDER prompt it shows green then
red stripes, benchmarks 20 worst-case renders, and blanks the panel before the
visual question. It does not bind spidev, read the ADC, or start the clock.
EOF
}

die() {
    echo "error: $*" >&2
    exit 1
}

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
tool=$(realpath "${tool}")
[[ -x ${tool} ]] || die "render helper is not executable: ${tool}"

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

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
umask 077
output_dir=$(mktemp -d "/tmp/oclock-phase5-ht1632-${timestamp}-XXXXXXXX")
archive_path="${output_dir%/}.tar.gz"
result_file=${output_dir}/result.txt

ok() { echo "OK: $*" >>"${result_file}"; }
fail() { echo "FAIL: $*" >>"${result_file}"; failures=$((failures + 1)); }
warn() { echo "WARNING: $*" >>"${result_file}"; warnings=$((warnings + 1)); }

# The matrix lines must be free before the helper requests them. A stale
# consumer here would mean something else is still driving the panel.
claimed=""
if command -v gpioinfo >/dev/null 2>&1; then
    gpioinfo >"${output_dir}/gpioinfo-before.txt" 2>&1 || true
    for offset in "${matrix_gpios[@]}"; do
        if grep -Eq "^[[:space:]]*line[[:space:]]+${offset}:.*(consumer=\"[^\"]|used)" \
                "${output_dir}/gpioinfo-before.txt"; then
            claimed+="${offset} "
        fi
    done
    [[ -z ${claimed} ]] || die "matrix GPIOs already claimed: ${claimed}"
fi

{
    printf 'captured_at_utc=%s\n' "${timestamp}"
    printf 'source_commit=%s\n' "${commit}"
    printf 'model=%s\n' "${model}"
    printf 'architecture=%s/%s\n' "$(uname -m)" "$(dpkg --print-architecture)"
    printf 'os_codename=%s\n' "${VERSION_CODENAME}"
    printf 'tick_budget_microseconds=%d\n' "${tick_budget_microseconds}"
    printf 'matrix_gpios=%s\n' "${matrix_gpios[*]}"
} >"${output_dir}/context.txt"
file "${tool}" >"${output_dir}/tool-file.txt"
ldd "${tool}" >"${output_dir}/tool-ldd.txt"
sha256sum "${tool}" >"${output_dir}/tool.sha256"
vcgencmd get_throttled >"${output_dir}/throttling-before.txt" 2>&1 || true

{
    echo "One standalone HT1632 sequence and render benchmark is authorized."
    echo "The partial oclock application is not executed."
    echo "No spidev binding is created and the ADC is not read."
    echo "The helper blanks the panel before returning."
} >"${output_dir}/safety.txt"

echo "Candidate helper:   ${tool}"
echo "Candidate commit:   ${commit}"
echo "Authorized output:  green stripes, then red stripes, about 3 s each,"
echo "                    then ${expected_renders} timed renders, then blank"
echo "Acceptance budget:  every render() <= ${tick_budget_microseconds} us"
echo
echo "Expect: vertical stripes every 8 columns across the whole 128x16 panel,"
echo "first green then red, then brief flicker while timing, then dark."
printf 'Type RENDER to begin: '
read -r answer
[[ ${answer} == RENDER ]] || die "render sequence was not authorized"

operator=${SUDO_USER:-root}
render_status=0
timeout --signal=TERM --kill-after=5s 90s \
    runuser -u "${operator}" -- "${tool}" \
    >"${output_dir}/render.txt" 2>"${output_dir}/render-stderr.txt" ||
    render_status=$?

vcgencmd get_throttled >"${output_dir}/throttling-after.txt" 2>&1 || true
if command -v gpioinfo >/dev/null 2>&1; then
    gpioinfo >"${output_dir}/gpioinfo-after.txt" 2>&1 || true
fi

echo
printf 'Did you see green stripes, then red stripes, then a dark panel? [yes/no]: '
read -r visual_answer

echo "Phase 5 HT1632 render checks" >"${result_file}"
ok "target is the selected Zero W/Trixie ARMv6 image"
ok "helper is ARM EABI5 and has no WiringPi dependency"
ok "matrix GPIOs were unclaimed before the run"

if ((render_status == 0)); then
    ok "render helper returned success"
else
    fail "render helper returned status ${render_status}"
fi

for expected_line in panel_width=128 panel_height=16 active_chips=16 \
        sequence=green,red,benchmark,off final_state=off \
        valid_renders="${expected_renders}"; do
    if grep -Fqx "${expected_line}" "${output_dir}/render.txt"; then
        ok "helper reported ${expected_line}"
    else
        fail "helper did not report ${expected_line}"
    fi
done

for color in green red; do
    if grep -Eq "^frame_${color}_microseconds=[1-9][0-9]*$" \
            "${output_dir}/render.txt"; then
        ok "${color} frame wall time was captured"
    else
        fail "${color} frame wall time is missing or invalid"
    fi
done

within=$(sed -n 's/^renders_within_budget=//p' "${output_dir}/render.txt")
maximum=$(sed -n 's/^maximum_microseconds=//p' "${output_dir}/render.txt")
if [[ ${within} =~ ^[0-9]+$ && ${maximum} =~ ^[1-9][0-9]*$ ]]; then
    if ((within == expected_renders)); then
        ok "all ${expected_renders} renders met the ${tick_budget_microseconds} us budget (max ${maximum} us)"
    else
        fail "only ${within} of ${expected_renders} renders met the budget (max ${maximum} us)"
    fi
else
    fail "render budget statistics are missing or invalid"
fi

if [[ ${visual_answer,,} == yes || ${visual_answer,,} == y ]]; then
    ok "operator confirmed the expected stripe sequence ending dark"
else
    fail "operator did not confirm the expected stripe sequence"
fi

if systemctl is-active --quiet oclock; then
    fail "oclock.service became active"
else
    ok "oclock.service remained inactive"
fi
if [[ -e /dev/spidev4.0 ]]; then
    fail "an unexpected strip spidev node is present"
else
    ok "no strip spidev binding was created"
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
    echo "One standalone HT1632 sequence and benchmark was attempted."
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
    die "HT1632 gate recorded ${failures} failure(s)"
fi
