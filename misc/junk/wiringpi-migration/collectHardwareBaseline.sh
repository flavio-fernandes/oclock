#!/bin/bash

# Collect the read-only production evidence needed by Phase 0 of
# docs/wiringpi-migration.md. This script never stops the service, changes GPIO
# direction or values, installs packages, or modifies boot configuration.

set -u
set -o pipefail

duration=60
service_name=oclock
binary_path=/home/pi/oclock.git/oclock
status_url=http://127.0.0.1:80/status
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
output_dir=

usage()
{
    cat <<EOF
Usage: sudo $0 [options]

Options:
  --duration SECONDS  Status/CPU sampling window (default: ${duration})
  --output DIRECTORY  Output directory (default: secure directory under /tmp)
  --service NAME      systemd service name (default: ${service_name})
  --binary PATH       deployed executable (default: ${binary_path})
  --status-url URL    read-only status endpoint (default: ${status_url})
  -h, --help          Show this help

During the sampling window, walk into and out of the PIR sensor's field of view
and briefly cover and uncover the light sensor. Visually observe the display
and LED strip, then answer the prompts that are stored in operator-notes.md.
EOF
}

die()
{
    echo "error: $*" >&2
    exit 2
}

while (($# > 0)); do
    case "$1" in
        --duration)
            (($# >= 2)) || die "--duration requires a value"
            duration=$2
            shift 2
            ;;
        --output)
            (($# >= 2)) || die "--output requires a value"
            output_dir=$2
            shift 2
            ;;
        --service)
            (($# >= 2)) || die "--service requires a value"
            service_name=$2
            shift 2
            ;;
        --binary)
            (($# >= 2)) || die "--binary requires a value"
            binary_path=$2
            shift 2
            ;;
        --status-url)
            (($# >= 2)) || die "--status-url requires a value"
            status_url=$2
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

[[ ${duration} =~ ^[0-9]+$ ]] ||
    die "--duration must be an integer"
((duration >= 5 && duration <= 600)) ||
    die "--duration must be between 5 and 600 seconds"
((EUID == 0)) ||
    die "run this collector as root (for example, with sudo)"

umask 077
if [[ -z ${output_dir} ]]; then
    output_dir=$(mktemp -d "/tmp/oclock-phase0-${timestamp}-XXXXXXXX") ||
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

copy_if_present()
{
    local source=$1
    local destination=$2
    if [[ -e ${source} ]]; then
        cp -a "${source}" "${destination}" 2>>"${output_dir}/copy-errors.txt" ||
            echo "failed to copy ${source}" >>"${output_dir}/copy-errors.txt"
    fi
}

{
    echo "Office Clock Phase 0 production baseline"
    echo
    echo "collection_utc: $(date -u --iso-8601=seconds 2>/dev/null || date -u)"
    echo "collection_local: $(date --iso-8601=seconds 2>/dev/null || date)"
    echo "collector: $0"
    echo "collector_uid: $(id -u)"
    echo "service: ${service_name}"
    echo "binary: ${binary_path}"
    echo "status_url: ${status_url}"
    echo "sample_duration_seconds: ${duration}"
    echo
    echo "Accepted hardware references:"
    echo "  https://flaviof.com/blog/hacks/office-clock-part1.html"
    echo "  https://flaviof.com/blog/hacks/office-clock-part2.html"
    echo "  BCM GPIO constants in this repository"
} >"${output_dir}/collection-context.txt"

capture identity id
capture kernel uname -a
capture uptime uptime
capture hostname hostname
capture filesystem df -h
capture memory free -h
capture mounts mount
capture block-devices lsblk

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
    if [[ -r /proc/cpuinfo ]]; then
        echo
        echo "cpuinfo (Serial omitted):"
        sed '/^Serial[[:space:]]*:/d' /proc/cpuinfo
    fi
} >"${output_dir}/raspberry-pi.txt" 2>&1

if command -v vcgencmd >/dev/null 2>&1; then
    capture firmware-version vcgencmd version
    capture throttling vcgencmd get_throttled
    capture temperature vcgencmd measure_temp
fi

capture service-unit systemctl cat "${service_name}"
capture service-status systemctl status "${service_name}" --no-pager
capture service-properties systemctl show "${service_name}" \
    --property=Id \
    --property=LoadState \
    --property=ActiveState \
    --property=SubState \
    --property=FragmentPath \
    --property=MainPID \
    --property=ExecStart \
    --property=User \
    --property=Group \
    --property=WorkingDirectory \
    --property=Restart \
    --property=NRestarts
capture service-journal journalctl -u "${service_name}" -n 200 --no-pager

service_pid=$(systemctl show "${service_name}" \
    --property=MainPID 2>/dev/null)
service_pid=${service_pid#MainPID=}
if [[ ${service_pid} =~ ^[1-9][0-9]*$ && -d /proc/${service_pid} ]]; then
    capture process ps -p "${service_pid}" \
        -o pid,ppid,user,group,lstart,etime,%cpu,%mem,rss,vsz,stat,args
    copy_if_present "/proc/${service_pid}/status" \
        "${output_dir}/process-status.txt"
    {
        echo -n "command_line: "
        tr '\000' ' ' <"/proc/${service_pid}/cmdline"
        echo
    } >"${output_dir}/process-command-line.txt" 2>&1
else
    echo "No running MainPID found for ${service_name}" \
        >"${output_dir}/process-not-running.txt"
    service_pid=
fi

capture listening-tcp-sockets ss -ltnp

capture gpio-version gpio -v
capture gpio-readall gpio readall
capture gpio-command-path command -v gpio

{
    shopt -s nullglob
    gpio_nodes=(/dev/gpiomem /dev/mem /dev/gpiochip*)
    if ((${#gpio_nodes[@]} == 0)); then
        echo "No GPIO device nodes found"
    else
        ls -l "${gpio_nodes[@]}"
    fi
} >"${output_dir}/gpio-device-nodes.txt" 2>&1

gpio_command=$(command -v gpio 2>/dev/null)
if [[ -n ${gpio_command} ]]; then
    capture gpio-command-file file "${gpio_command}"
    capture gpio-command-stat stat "${gpio_command}"
    if command -v dpkg-query >/dev/null 2>&1; then
        capture gpio-package-owner dpkg-query -S "${gpio_command}"
    fi
fi

if command -v dpkg-query >/dev/null 2>&1; then
    capture wiringpi-packages dpkg-query -W \
        -f='${binary:Package}\t${Version}\t${Status}\n' \
        wiringpi libwiringpi-dev libwiringpi2
fi
if command -v apt-cache >/dev/null 2>&1; then
    capture wiringpi-package-policy apt-cache policy \
        wiringpi libwiringpi-dev libwiringpi2
fi
if command -v ldconfig >/dev/null 2>&1; then
    capture dynamic-library-cache ldconfig -p
fi

mkdir "${output_dir}/rollback"
if [[ -f ${binary_path} ]]; then
    capture deployed-binary-file file "${binary_path}"
    capture deployed-binary-stat stat "${binary_path}"
    capture deployed-binary-sha256 sha256sum "${binary_path}"
    capture deployed-binary-dependencies ldd "${binary_path}"
    if command -v getcap >/dev/null 2>&1; then
        capture deployed-binary-capabilities getcap "${binary_path}"
    fi
    if command -v readelf >/dev/null 2>&1; then
        capture deployed-binary-dynamic-section readelf -d "${binary_path}"
    fi

    wiringpi_library=$(ldd "${binary_path}" 2>/dev/null |
        awk '/libwiringPi/ { print $3; exit }')
    if [[ -n ${wiringpi_library} && -f ${wiringpi_library} ]]; then
        capture wiringpi-library-file file "${wiringpi_library}"
        capture wiringpi-library-stat stat "${wiringpi_library}"
        capture wiringpi-library-sha256 sha256sum "${wiringpi_library}"
        if command -v dpkg-query >/dev/null 2>&1; then
            capture wiringpi-library-package-owner \
                dpkg-query -S "${wiringpi_library}"
        fi
    fi

    cp -a "${binary_path}" "${output_dir}/rollback/oclock" ||
        die "could not copy deployed binary into rollback directory"
    if cmp -s "${binary_path}" "${output_dir}/rollback/oclock"; then
        echo "rollback copy is byte-for-byte identical" \
            >"${output_dir}/rollback/verification.txt"
    else
        die "rollback copy differs from deployed binary"
    fi
    stat "${output_dir}/rollback/oclock" \
        >>"${output_dir}/rollback/verification.txt" 2>&1
    sha256sum "${output_dir}/rollback/oclock" \
        >>"${output_dir}/rollback/verification.txt" 2>&1
else
    echo "Deployed binary not found: ${binary_path}" \
        >"${output_dir}/rollback/MISSING.txt"
fi

source_dir=$(dirname "${binary_path}")
if command -v git >/dev/null 2>&1 && git -C "${source_dir}" rev-parse \
        --is-inside-work-tree >/dev/null 2>&1; then
    capture source-version git -C "${source_dir}" rev-parse HEAD
    capture source-status git -C "${source_dir}" status --short --branch
    capture source-remotes git -C "${source_dir}" remote -v
    capture source-diff-stat git -C "${source_dir}" diff --stat
    if [[ -f ${source_dir}/src/display.cpp &&
          -f ${source_dir}/src/ledStrip.cpp &&
          -f ${source_dir}/src/lightSensor.cpp &&
          -f ${source_dir}/src/motionSensor.cpp ]]; then
        capture source-pin-constants grep -nE \
            'const (int|Int8U) (Display|LedStrip|LightSensor|MotionSensor)::' \
            "${source_dir}/src/display.cpp" \
            "${source_dir}/src/ledStrip.cpp" \
            "${source_dir}/src/lightSensor.cpp" \
            "${source_dir}/src/motionSensor.cpp"
    fi
fi

mkdir "${output_dir}/boot"
copy_if_present /boot/config.txt "${output_dir}/boot/config.txt"
copy_if_present /boot/cmdline.txt "${output_dir}/boot/cmdline.txt"
copy_if_present /boot/firmware/config.txt \
    "${output_dir}/boot/firmware-config.txt"
copy_if_present /boot/firmware/cmdline.txt \
    "${output_dir}/boot/firmware-cmdline.txt"
capture boot-directory ls -la /boot

{
    echo "Sampling the read-only status endpoint for ${duration} seconds."
    echo "Walk into and out of the PIR sensor's field of view now."
    echo "Briefly cover and uncover the light sensor."
} >&2

status_samples="${output_dir}/status-samples.txt"
process_samples="${output_dir}/process-samples.txt"
cpu_samples="${output_dir}/service-cpu-samples.tsv"
sample_started=$(date +%s)
sample_number=0
previous_process_ticks=
previous_total_ticks=
if [[ -n ${service_pid} && -r /proc/${service_pid}/stat ]]; then
    previous_process_ticks=$(awk '{ print $14 + $15 }' \
        "/proc/${service_pid}/stat")
    previous_total_ticks=$(awk '/^cpu / {
        total = 0
        for (field = 2; field <= NF; ++field) total += $field
        print total
    }' /proc/stat)
    printf 'elapsed_seconds\tprocess_ticks\ttotal_ticks\thost_cpu_percent\n' \
        >"${cpu_samples}"
fi
while :; do
    now=$(date +%s)
    elapsed=$((now - sample_started))
    ((elapsed < duration)) || break
    sample_number=$((sample_number + 1))

    {
        echo "===== sample ${sample_number} $(date -u +%Y-%m-%dT%H:%M:%SZ) ====="
        if command -v curl >/dev/null 2>&1; then
            curl --silent --show-error --max-time 2 "${status_url}"
            echo "curl_exit_status: $?"
        else
            echo "curl is not installed"
        fi
        echo
    } >>"${status_samples}" 2>&1

    {
        echo "===== sample ${sample_number} $(date -u +%Y-%m-%dT%H:%M:%SZ) ====="
        if [[ -n ${service_pid} && -d /proc/${service_pid} ]]; then
            ps -p "${service_pid}" \
                -o pid,etime,%cpu,%mem,rss,vsz,stat,args
        else
            echo "service process is not running"
        fi
        echo
    } >>"${process_samples}" 2>&1

    if [[ -n ${previous_process_ticks} &&
          -r /proc/${service_pid}/stat ]]; then
        current_process_ticks=$(awk '{ print $14 + $15 }' \
            "/proc/${service_pid}/stat")
        current_total_ticks=$(awk '/^cpu / {
            total = 0
            for (field = 2; field <= NF; ++field) total += $field
            print total
        }' /proc/stat)
        process_delta=$((current_process_ticks - previous_process_ticks))
        total_delta=$((current_total_ticks - previous_total_ticks))
        cpu_percent=$(awk -v process_delta="${process_delta}" \
            -v total_delta="${total_delta}" \
            'BEGIN {
                if (total_delta > 0)
                    printf "%.2f", 100 * process_delta / total_delta
                else
                    printf "0.00"
            }')
        printf '%d\t%d\t%d\t%s\n' "${elapsed}" "${process_delta}" \
            "${total_delta}" "${cpu_percent}" >>"${cpu_samples}"
        previous_process_ticks=${current_process_ticks}
        previous_total_ticks=${current_total_ticks}
    fi

    sleep 1
done

grep -E '^(motion|motion_last_change|light_sensor|display_mode|led_strip_mode):' \
    "${status_samples}" >"${output_dir}/observed-status-summary.txt" 2>/dev/null ||
    true

echo >&2
echo "Observed status values:" >&2
cat "${output_dir}/observed-status-summary.txt" >&2
echo >&2

prompt_observation()
{
    local prompt=$1
    local answer
    printf '%s [yes/no/notes]: ' "${prompt}" >&2
    if ! IFS= read -r answer; then
        answer="not recorded (non-interactive run)"
    fi
    [[ -n ${answer} ]] || answer="not recorded"
    printf '%s' "${answer}"
}

display_observation=$(prompt_observation \
    "Did the four-panel display refresh normally without corruption?")
strip_observation=$(prompt_observation \
    "Was the LED strip animation smooth with normal colors?")
light_observation=$(prompt_observation \
    "Did the sampled light value respond when the sensor was covered?")
motion_observation=$(prompt_observation \
    "Did the sampled motion state respond when you moved?")
service_observation=$(prompt_observation \
    "Did the clock remain healthy with no unexpected restart or error?")
additional_observation=$(prompt_observation "Any additional observations?")

cat >"${output_dir}/operator-notes.md" <<EOF
# Phase 0 operator observations

Collection time: ${timestamp}

- Display: ${display_observation}
- LED strip: ${strip_observation}
- Light sensor: ${light_observation}
- Motion sensor: ${motion_observation}
- Service health: ${service_observation}
- Additional notes: ${additional_observation}
EOF

{
    echo "Phase 0 collector does not alter the running clock."
    echo "It intentionally does not start the rollback binary while the service"
    echo "owns GPIO. The archive contains an identical copy plus its metadata"
    echo "and checksum; executable rollback is verified during a maintenance"
    echo "window before the modern backend is deployed."
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

echo "Phase 0 collection checks" >"${result_file}"

if ((EUID == 0)); then
    result_ok "collector ran as root"
else
    result_fail "collector did not run as root; rerun it with sudo"
fi

if systemctl is-active --quiet "${service_name}" 2>/dev/null; then
    result_ok "${service_name} remained active"
else
    result_fail "${service_name} was not active at final validation"
fi

if [[ -f ${output_dir}/rollback/oclock &&
      -f ${output_dir}/rollback/verification.txt ]]; then
    result_ok "deployed binary and byte-identical rollback copy were captured"
else
    result_fail "deployed binary or rollback verification is missing"
fi

if [[ -n ${gpio_command} ]] &&
        grep -q '^exit_status: 0$' "${output_dir}/gpio-version.txt" &&
        grep -q '^exit_status: 0$' "${output_dir}/gpio-readall.txt"; then
    result_ok "WiringPi gpio version and pin state were captured"
else
    result_fail "WiringPi gpio version or pin state is missing"
fi

if grep -q '^Stats and status$' "${status_samples}"; then
    result_ok "application status endpoint was sampled"
else
    result_fail "application status endpoint did not return expected output"
fi

if [[ -f ${output_dir}/source-version.txt ]]; then
    result_ok "deployed source revision was captured"
else
    result_warn "deployed binary directory was not a readable git worktree"
fi

if [[ -f ${output_dir}/wiringpi-library-sha256.txt ]]; then
    result_ok "linked WiringPi library metadata was captured"
else
    result_warn "linked WiringPi library path was not resolved from the binary"
fi

if [[ -f ${output_dir}/boot/config.txt ||
      -f ${output_dir}/boot/firmware-config.txt ]]; then
    result_ok "boot configuration was captured"
else
    result_warn "no supported boot configuration path was found"
fi

motion_value_count=$(awk '/^motion: / { print $2 }' "${status_samples}" |
    sort -u | wc -l)
if ((motion_value_count >= 2)); then
    result_ok "status samples include a motion transition"
else
    result_warn "status samples do not include both motion states"
fi

light_value_count=$(awk '/^light_sensor: / { print $2 }' "${status_samples}" |
    sort -u | wc -l)
if ((light_value_count >= 2)); then
    result_ok "status samples include changing light values"
else
    result_warn "status samples do not include changing light values"
fi

if grep -Eq \
        '^- (Display|LED strip|Light sensor|Motion sensor|Service health): (no|not recorded)$' \
        "${output_dir}/operator-notes.md"; then
    result_warn "one or more operator observations need review"
else
    result_ok "operator observations were recorded without a negative answer"
fi

if [[ -s ${output_dir}/copy-errors.txt ]]; then
    result_warn "one or more optional files could not be copied"
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
echo "Phase 0 collection complete."
echo "Review observations:  ${output_dir}/operator-notes.md"
echo "Review checks:        ${result_file}"
echo "Share this archive:   ${archive_path}"
echo "Archive checksum:     ${archive_path}.sha256"

if ((failures > 0)); then
    echo "Collection has ${failures} required check failure(s)." >&2
    exit 1
fi
