#!/usr/bin/env bash

# Collect three controlled MCP3002 windows with the standalone application
# reader: uncovered, fully covered, and uncovered again. This records evidence
# for a later threshold decision; it does not change calibration or start the
# whole application.

set -euo pipefail

accepted_overlay_sha=53b593f4c30a78c8beb446cf506816af29a817d0daf93bb899a9054c7aa61959
adc_dt_suffix=/oclock-adc-spi/mcp3002@0
strip_dt_suffix=/oclock-strip-spi/lpd8806@0
samples_per_window=10
sample_interval=0.6
tool=
commit=

usage() {
    cat <<'EOF'
Usage: sudo misc/collectPhase5Mcp3002Calibration.sh \
  --tool PATH --commit GIT_COMMIT

Uses the accepted standalone IIO reader to collect ten pairs at 600 ms for:
  1. the normally uncovered sensor;
  2. the fully covered sensor;
  3. the uncovered sensor after restoration.

The script pauses before every window. It keeps channels separate, reports
summary statistics and existing-threshold crossings, and makes no calibration
decision or hardware/service change.
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

[[ ${EUID} -eq 0 ]] || die "run this collector with sudo"
[[ -n ${tool} && -n ${commit} ]] || {
    usage >&2
    exit 2
}
[[ ${commit} =~ ^[0-9a-f]{40}$ ]] || die "commit must be a full Git SHA"
tool=$(realpath "${tool}")
[[ -x ${tool} ]] || die "reader is not executable: ${tool}"

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

overlay=/boot/firmware/overlays/oclock-spi.dtbo
[[ -f ${overlay} ]] || die "accepted Office Clock overlay is not installed"
[[ $(sha256sum "${overlay}" | awk '{print $1}') == \
   "${accepted_overlay_sha}" ]] || die "installed overlay checksum differs"

find_spi_child() {
    local suffix=$1
    local -a matches=()
    local device node
    for device in /sys/bus/spi/devices/spi*; do
        [[ -e ${device} ]] || continue
        node=$(readlink -f "${device}/of_node" 2>/dev/null || true)
        [[ ${node} == *"${suffix}" ]] && matches+=("${device}")
    done
    ((${#matches[@]} == 1)) ||
        die "expected one SPI child ending in ${suffix}; found ${#matches[@]}"
    printf '%s\n' "${matches[0]}"
}

adc_spi=$(find_spi_child "${adc_dt_suffix}")
strip_spi=$(find_spi_child "${strip_dt_suffix}")
[[ $(basename "$(readlink -f "${adc_spi}/driver")") == mcp320x ]] ||
    die "MCP3002 is not bound to mcp320x"
[[ ! -e ${strip_spi}/driver ]] || die "strip must remain unbound"

operator=${SUDO_USER:-root}
file "${tool}" | grep -Fq 'ELF 32-bit LSB executable, ARM, EABI5' ||
    die "reader is not an ARM EABI5 executable"
if ldd "${tool}" | grep -q libwiringPi; then
    die "reader unexpectedly resolves WiringPi"
fi
strings "${tool}" | grep -F "${adc_dt_suffix}" >/dev/null ||
    die "reader lacks Device Tree identity discovery"

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
output_dir=$(mktemp -d "/tmp/oclock-phase5-mcp3002-calibration-${timestamp}-XXXXXXXX")
samples_file=${output_dir}/samples.tsv
result_file=${output_dir}/result.txt
printf 'window\tsample\tchannel0_raw\tchannel1_raw\tpair_average\telapsed_microseconds\n' \
    >"${samples_file}"

capture_state() {
    local destination=$1
    {
        printf 'adc_spi_child=%s\n' "$(basename "${adc_spi}")"
        printf 'adc_driver=%s\n' \
            "$(basename "$(readlink -f "${adc_spi}/driver")")"
        if [[ -e ${strip_spi}/driver ]]; then
            printf 'strip_driver=%s\n' \
                "$(basename "$(readlink -f "${strip_spi}/driver")")"
        else
            echo 'strip_driver=none'
        fi
        printf 'service_active=%s\n' \
            "$(systemctl is-active oclock 2>/dev/null || true)"
    } >"${destination}"
}

{
    printf 'captured_at_utc=%s\n' "${timestamp}"
    printf 'source_commit=%s\n' "${commit}"
    printf 'model=%s\n' "${model}"
    printf 'architecture=%s/%s\n' "$(uname -m)" "$(dpkg --print-architecture)"
    printf 'os_codename=%s\n' "${VERSION_CODENAME}"
    printf 'samples_per_window=%d\n' "${samples_per_window}"
    printf 'sample_interval_seconds=%s\n' "${sample_interval}"
    echo 'threshold_low=360'
    echo 'threshold_high=500'
} >"${output_dir}/context.txt"
file "${tool}" >"${output_dir}/tool-file.txt"
ldd "${tool}" >"${output_dir}/tool-ldd.txt"
sha256sum "${tool}" >"${output_dir}/tool.sha256"
capture_state "${output_dir}/state-before.txt"
vcgencmd get_throttled >"${output_dir}/throttling-before.txt" 2>&1 || true

sample_failures=0
collect_window() {
    local window=$1
    local index output channel0 channel1 elapsed pair_average status
    for ((index = 1; index <= samples_per_window; ++index)); do
        status=0
        output=$(timeout --signal=TERM --kill-after=2s 10s \
            runuser -u "${operator}" -- "${tool}" 2>>"${output_dir}/read-stderr.txt") ||
            status=$?
        channel0=$(sed -n 's/^channel0_raw=//p' <<<"${output}")
        channel1=$(sed -n 's/^channel1_raw=//p' <<<"${output}")
        elapsed=$(sed -n 's/^read_pair_elapsed_microseconds=//p' <<<"${output}")
        if ((status != 0)) || [[ ! ${channel0} =~ ^[0-9]+$ ]] ||
                [[ ! ${channel1} =~ ^[0-9]+$ ]] ||
                [[ ! ${elapsed} =~ ^[0-9]+$ ]] ||
                ((channel0 > 1023 || channel1 > 1023)); then
            echo "invalid sample: window=${window} index=${index} status=${status}" \
                >>"${output_dir}/sample-errors.txt"
            sample_failures=$((sample_failures + 1))
        else
            pair_average=$(((channel0 + channel1) / 2))
            printf '%s\t%d\t%d\t%d\t%d\t%d\n' \
                "${window}" "${index}" "${channel0}" "${channel1}" \
                "${pair_average}" "${elapsed}" >>"${samples_file}"
        fi
        ((index == samples_per_window)) || sleep "${sample_interval}"
    done
}

echo "Candidate reader: ${tool}"
echo "Candidate commit: ${commit}"
echo "Each window:     ${samples_per_window} pairs at ${sample_interval}s"
echo
echo "Leave the light sensor normally uncovered."
printf 'Type BASELINE to collect the uncovered window: '
read -r answer
[[ ${answer} == BASELINE ]] || die "baseline window was not authorized"
collect_window baseline

echo
echo "Now cover the light sensor completely and keep it covered."
printf 'Type COVERED when the sensor is fully covered: '
read -r answer
[[ ${answer} == COVERED ]] || die "covered window was not authorized"
collect_window covered

echo
echo "Uncover the light sensor and return it to the baseline condition."
printf 'Type RESTORED when the sensor is uncovered again: '
read -r answer
[[ ${answer} == RESTORED ]] || die "restored window was not authorized"
collect_window restored

capture_state "${output_dir}/state-after.txt"
vcgencmd get_throttled >"${output_dir}/throttling-after.txt" 2>&1 || true

awk -F '\t' '
    NR == 1 { next }
    {
        window = $1
        count[window]++
        sum0[window] += $3
        sum1[window] += $4
        sumPair[window] += $5
        if (count[window] == 1 || $3 < min0[window]) min0[window] = $3
        if (count[window] == 1 || $3 > max0[window]) max0[window] = $3
        if (count[window] == 1 || $4 < min1[window]) min1[window] = $4
        if (count[window] == 1 || $4 > max1[window]) max1[window] = $4
        if ($5 < 360) belowLow[window]++
        if ($5 < 500) belowHigh[window]++
    }
    END {
        print "window\tsamples\tchannel0_min\tchannel0_max\tchannel0_mean\tchannel1_min\tchannel1_max\tchannel1_mean\tpair_mean\tpairs_below_360\tpairs_below_500"
        order[1] = "baseline"; order[2] = "covered"; order[3] = "restored"
        for (i = 1; i <= 3; ++i) {
            window = order[i]
            if (count[window] == 0) continue
            printf "%s\t%d\t%d\t%d\t%.1f\t%d\t%d\t%.1f\t%.1f\t%d\t%d\n", window, count[window], min0[window], max0[window], sum0[window] / count[window], min1[window], max1[window], sum1[window] / count[window], sumPair[window] / count[window], belowLow[window] + 0, belowHigh[window] + 0
        }
    }
' "${samples_file}" >"${output_dir}/summary.tsv"

failures=0
warnings=0
{
    echo "Phase 5 MCP3002 controlled light capture"
    if ((sample_failures == 0)); then
        echo "OK: all 30 two-channel samples were valid"
    else
        echo "FAIL: ${sample_failures} sample(s) were invalid"
        failures=$((failures + 1))
    fi
    if cmp -s "${output_dir}/state-before.txt" "${output_dir}/state-after.txt"; then
        echo "OK: ADC, strip, and service state did not change"
    else
        echo "FAIL: hardware or service state changed during capture"
        failures=$((failures + 1))
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
    echo "This capture records transport and controlled light response only."
    echo "It does not change or approve dimming thresholds."
} >"${result_file}"

cat "${result_file}"
echo
column -t -s $'\t' "${output_dir}/summary.tsv" ||
    cat "${output_dir}/summary.tsv"

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

((failures == 0)) || die "controlled light capture recorded ${failures} failure(s)"
