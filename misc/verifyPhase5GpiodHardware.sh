#!/bin/bash

# Guarded functional trial of the libgpiod candidate on the intended modern
# Raspberry Pi Zero W Rev 1.1 target. This script can restore a service active
# on the Trixie card, but a physical return to the preserved Zero/Jessie unit
# remains a manual, powered-off rollback step.

set -u
set -o pipefail

binary_path=
candidate_commit=
expected_sha256=
duration=90
startup_timeout=120
bind_address=0.0.0.0
port=80
mqtt_host=192.168.10.238
mqtt_port=1883
service_name=oclock
output_dir=
dark_threshold=360
bright_threshold=500

usage()
{
    cat <<EOF
Usage: sudo $0 --binary PATH --commit SHA --sha256 SHA256 [options]

Required:
  --binary PATH         Phase 3 libgpiod binary built on ARMv6
  --commit SHA          Source commit used to build that binary
  --sha256 SHA256       Expected binary SHA-256

Options:
  --duration SECONDS    Observation window (default: ${duration})
  --startup-timeout S   Candidate readiness timeout (default: ${startup_timeout})
  --bind ADDRESS        HTTP bind address (default: ${bind_address})
  --port PORT           HTTP port (default: ${port})
  --mqtt-host ADDRESS   MQTT broker (default: ${mqtt_host})
  --mqtt-port PORT      MQTT port (default: ${mqtt_port})
  --service NAME        Candidate-card service to stop/restore (default: ${service_name})
  --output DIRECTORY    New result directory (default: secure directory in /tmp)
  -h, --help            Show this help

This script refuses to run unless the host reports the intended Raspberry Pi
Zero W Rev 1.1 identity, the Trixie ARMv6/armhf target, and a connected Wi-Fi
device. Run it without a USB Wi-Fi dongle. It never modifies the preserved
Zero/Jessie rollback unit and cannot reconnect that unit for you.
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
            binary_path=$2; shift 2 ;;
        --commit)
            (($# >= 2)) || die "--commit requires a value"
            candidate_commit=$2; shift 2 ;;
        --sha256)
            (($# >= 2)) || die "--sha256 requires a value"
            expected_sha256=$2; shift 2 ;;
        --duration)
            (($# >= 2)) || die "--duration requires a value"
            duration=$2; shift 2 ;;
        --startup-timeout)
            (($# >= 2)) || die "--startup-timeout requires a value"
            startup_timeout=$2; shift 2 ;;
        --bind)
            (($# >= 2)) || die "--bind requires a value"
            bind_address=$2; shift 2 ;;
        --port)
            (($# >= 2)) || die "--port requires a value"
            port=$2; shift 2 ;;
        --mqtt-host)
            (($# >= 2)) || die "--mqtt-host requires a value"
            mqtt_host=$2; shift 2 ;;
        --mqtt-port)
            (($# >= 2)) || die "--mqtt-port requires a value"
            mqtt_port=$2; shift 2 ;;
        --service)
            (($# >= 2)) || die "--service requires a value"
            service_name=$2; shift 2 ;;
        --output)
            (($# >= 2)) || die "--output requires a value"
            output_dir=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
done

[[ -n ${binary_path} ]] || die "--binary is required"
[[ ${candidate_commit} =~ ^[0-9a-fA-F]{7,40}$ ]] ||
    die "--commit must be a hexadecimal Git revision"
[[ ${expected_sha256} =~ ^[0-9a-fA-F]{64}$ ]] ||
    die "--sha256 must contain 64 hexadecimal characters"
if [[ ! ${duration} =~ ^[0-9]+$ ]] ||
        ((duration < 30 || duration > 600)); then
    die "--duration must be between 30 and 600 seconds"
fi
if [[ ! ${startup_timeout} =~ ^[0-9]+$ ]] ||
        ((startup_timeout < 15 || startup_timeout > 300)); then
    die "--startup-timeout must be between 15 and 300 seconds"
fi
if [[ ! ${port} =~ ^[0-9]+$ ]] || ((port < 1 || port > 65535)); then
    die "--port must be between 1 and 65535"
fi
if [[ ! ${mqtt_port} =~ ^[0-9]+$ ]] ||
        ((mqtt_port < 1 || mqtt_port > 65535)); then
    die "--mqtt-port must be between 1 and 65535"
fi
((EUID == 0)) || die "run this script with sudo"

for command_name in awk curl date dpkg file gpiodetect grep ldd nmcli ps \
        readlink sha256sum sort strings systemctl tail tar tr uname wc; do
    command -v "${command_name}" >/dev/null 2>&1 ||
        die "required command is missing: ${command_name}"
done

[[ -r /proc/device-tree/model ]] || die "cannot read Raspberry Pi model"
model=$(tr -d '\0' </proc/device-tree/model) || die "cannot read Raspberry Pi model"
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

gpio_chips=$(gpiodetect 2>&1) || die "gpiodetect failed"
grep -q '\[pinctrl-bcm2835\]' <<<"${gpio_chips}" ||
    die "pinctrl-bcm2835 GPIO chip was not found"
[[ -c /dev/gpiomem && -r /dev/gpiomem && -w /dev/gpiomem ]] ||
    die "the restricted /dev/gpiomem value path is unavailable"

network_devices=$(nmcli -t -f DEVICE,TYPE,STATE device status 2>&1) ||
    die "NetworkManager device inspection failed"
grep -Eq '^[^:]+:wifi:connected' <<<"${network_devices}" ||
    die "no connected Wi-Fi device was found"
wifi_signal=$(nmcli -t -f IN-USE,SIGNAL device wifi list --rescan no 2>&1 |
    awk -F: '$1 == "yes" { print $2; exit }')
[[ ${wifi_signal} =~ ^[0-9]+$ ]] || wifi_signal=unknown

if command -v vcgencmd >/dev/null 2>&1; then
    throttling=$(vcgencmd get_throttled 2>&1) ||
        die "vcgencmd get_throttled failed"
    [[ ${throttling} == throttled=0x0 ]] ||
        die "firmware reports throttling: ${throttling}"
else
    throttling="vcgencmd unavailable"
fi

binary_path=$(readlink -f "${binary_path}") ||
    die "cannot resolve candidate binary"
[[ -f ${binary_path} && -x ${binary_path} ]] ||
    die "candidate is not executable: ${binary_path}"
actual_sha256=$(sha256sum "${binary_path}" | awk '{ print $1 }')
[[ ${actual_sha256,,} == "${expected_sha256,,}" ]] ||
    die "candidate SHA-256 mismatch: ${actual_sha256}"

candidate_file=$(file "${binary_path}") || die "file inspection failed"
grep -q 'ELF 32-bit LSB executable, ARM' <<<"${candidate_file}" ||
    die "candidate is not a 32-bit ARM executable"
candidate_dependencies=$(ldd "${binary_path}" 2>&1) ||
    die "candidate dependency inspection failed"
grep -q 'libgpiod\.so\.3 =>' <<<"${candidate_dependencies}" ||
    die "candidate does not resolve libgpiod.so.3"
grep -q 'libatomic\.so\.1 =>' <<<"${candidate_dependencies}" ||
    die "candidate does not resolve libatomic.so.1"
! grep -q 'libwiringPi' <<<"${candidate_dependencies}" ||
    die "candidate unexpectedly links WiringPi"
! grep -q 'not found' <<<"${candidate_dependencies}" ||
    die "candidate has an unresolved dynamic dependency"
candidate_strings=$(strings "${binary_path}") ||
    die "candidate string inspection failed"
grep -q '^/dev/gpiomem$' <<<"${candidate_strings}" ||
    die "candidate does not contain the reviewed /dev/gpiomem value path"

status_url="http://127.0.0.1:${port}/status"
stop_url="http://127.0.0.1:${port}/stop"
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
service_was_active=no
systemctl is-active --quiet "${service_name}" && service_was_active=yes

echo "Candidate:          ${binary_path}"
echo "Candidate commit:   ${candidate_commit}"
echo "Candidate SHA-256:  ${actual_sha256}"
echo "Board:              ${model} (${revision})"
echo "Candidate-card svc: ${service_name} (${service_was_active})"
echo "Observation window: ${duration} seconds"
echo
echo "This will drive the real office-clock GPIO lines."
echo "The USB Wi-Fi dongle must be disconnected for this target trial."
echo "The preserved Zero/Jessie rollback unit must be powered off."
echo "Afterward, power off before reconnecting the rollback unit."
printf 'Type RUN to begin the exact-board candidate trial: '
IFS= read -r confirmation || die "confirmation was not read"
[[ ${confirmation} == RUN ]] || die "confirmation was not RUN; no change made"

umask 077
if [[ -z ${output_dir} ]]; then
    output_dir=$(mktemp -d "/tmp/oclock-phase5-${timestamp}-XXXXXXXX") ||
        die "cannot create result directory"
else
    [[ ${output_dir} != / ]] || die "unsafe output directory"
    mkdir -p "$(dirname "${output_dir}")" || die "cannot create output parent"
    mkdir "${output_dir}" || die "output directory already exists"
fi

candidate_pid=
candidate_exit_status=not-recorded
service_restored=not-required
archive_created=no

wait_for_exit()
{
    local pid=$1
    local attempts=${2:-300}
    local i
    for ((i=0; i < attempts; ++i)); do
        kill -0 "${pid}" 2>/dev/null || return 0
        sleep 0.1
    done
    return 1
}

stop_candidate()
{
    [[ -n ${candidate_pid} ]] || return 0
    if kill -0 "${candidate_pid}" 2>/dev/null; then
        curl --silent --max-time 5 --request POST "${stop_url}" \
            >/dev/null 2>&1 || true
        wait_for_exit "${candidate_pid}" 300 ||
            kill -INT "${candidate_pid}" 2>/dev/null || true
        wait_for_exit "${candidate_pid}" 100 ||
            kill -TERM "${candidate_pid}" 2>/dev/null || true
    fi
    wait "${candidate_pid}" 2>/dev/null
    candidate_exit_status=$?
    candidate_pid=
}

restore_service()
{
    [[ ${service_was_active} == yes ]] || return 0
    systemctl is-active --quiet "${service_name}" ||
        systemctl start "${service_name}" >/dev/null 2>&1 || return 1
    systemctl is-active --quiet "${service_name}" || return 1
    service_restored=yes
}

archive_results()
{
    [[ -d ${output_dir} && ${archive_created} == no ]] || return 0
    archive_path="${output_dir}.tar.gz"
    tar -czf "${archive_path}" -C "$(dirname "${output_dir}")" \
        "$(basename "${output_dir}")" || return 1
    (
        cd "$(dirname "${archive_path}")" || exit 1
        sha256sum "$(basename "${archive_path}")" \
            >"$(basename "${archive_path}").sha256"
    ) || return 1
    if [[ -n ${SUDO_UID:-} && -n ${SUDO_GID:-} ]]; then
        chown "${SUDO_UID}:${SUDO_GID}" "${archive_path}" \
            "${archive_path}.sha256" 2>/dev/null || true
    fi
    archive_created=yes
}

cleanup()
{
    local rc=$?
    trap - EXIT HUP INT TERM
    set +e
    stop_candidate
    restore_service || {
        echo "ERROR: could not restore active candidate-card service" >&2
        rc=1
    }
    archive_results || rc=1
    echo "Result directory: ${output_dir}" >&2
    [[ ${archive_created} == yes ]] && {
        echo "Share this archive: ${output_dir}.tar.gz" >&2
        echo "Archive checksum:   ${output_dir}.tar.gz.sha256" >&2
    }
    exit "${rc}"
}

trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

{
    echo "Phase 5 exact-board libgpiod trial"
    echo
    echo "started_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "candidate_commit: ${candidate_commit}"
    echo "candidate_binary: ${binary_path}"
    echo "candidate_sha256: ${actual_sha256}"
    echo "model: ${model}"
    echo "revision: ${revision}"
    echo "os: ${PRETTY_NAME:-unknown}"
    echo "machine: $(uname -m)"
    echo "architecture: $(dpkg --print-architecture)"
    echo "throttling: ${throttling}"
    echo "gpiomem: available"
    echo "duration_seconds: ${duration}"
    echo "dark_threshold: ${dark_threshold}"
    echo "bright_threshold: ${bright_threshold}"
    echo "startup_timeout_seconds: ${startup_timeout}"
    echo "mqtt: ${mqtt_host}:${mqtt_port}"
    echo "active_wifi_signal_percent: ${wifi_signal}"
    echo "service_was_active: ${service_was_active}"
} >"${output_dir}/context.txt"
printf '%s\n' "${candidate_file}" >"${output_dir}/candidate-file.txt"
printf '%s\n' "${candidate_dependencies}" \
    >"${output_dir}/candidate-dependencies.txt"
printf '%s\n' "${gpio_chips}" >"${output_dir}/gpiodetect.txt"
printf '%s\n' "${network_devices}" >"${output_dir}/network-devices.txt"
systemctl status "${service_name}" --no-pager \
    >"${output_dir}/service-before.txt" 2>&1 || true

if [[ ${service_was_active} == yes ]]; then
    echo "Stopping active candidate-card service ${service_name}..."
    systemctl stop "${service_name}" || die "could not stop ${service_name}"
    systemctl is-active --quiet "${service_name}" &&
        die "${service_name} is still active"
fi
curl --fail --silent --max-time 1 "${status_url}" >/dev/null 2>&1 &&
    die "HTTP port ${port} is already serving"

runtime_dir="${output_dir}/runtime"
mkdir -p "${runtime_dir}/log"
echo "Starting libgpiod candidate on ${bind_address}:${port}..."
(
    cd "${runtime_dir}" || exit 1
    exec "${binary_path}" -b "${bind_address}" -p "${port}" \
        -M "${mqtt_host}" -P "${mqtt_port}" \
        -l "${output_dir}/candidate.log"
) >"${output_dir}/candidate-stdout.txt" \
  2>"${output_dir}/candidate-stderr.txt" &
candidate_pid=$!

ready=no
for ((i=0; i < startup_timeout * 10; ++i)); do
    if curl --fail --silent --max-time 2 "${status_url}" \
            >"${output_dir}/initial-status.txt"; then
        ready=yes
        break
    fi
    kill -0 "${candidate_pid}" 2>/dev/null || break
    sleep 0.1
done
[[ ${ready} == yes ]] ||
    die "candidate did not become ready; inspect candidate-stderr.txt"

echo
echo "Candidate is running. During the next ${duration} seconds:"
echo "  - watch all four display panels for refresh and corruption;"
echo "  - watch LED-strip colors, smoothness, and responsiveness;"
echo "  - fully cover the light sensor for at least 12 seconds, until the"
echo "    status average passes below ${dark_threshold} and the outputs dim;"
echo "  - then uncover it for at least 12 seconds, until the average passes"
echo "    ${bright_threshold} and the outputs return to normal brightness;"
echo "  - enter and leave the motion-sensor field;"
echo "  - trigger the normal external MQTT data feed."

status_samples="${output_dir}/status-samples.txt"
process_samples="${output_dir}/process-samples.txt"
cpu_samples="${output_dir}/candidate-cpu-samples.tsv"
sample_started=$(date +%s)
sample_number=0
previous_process_ticks=$(awk '{ print $14 + $15 }' \
    "/proc/${candidate_pid}/stat")
previous_total_ticks=$(awk '/^cpu / {
    total = 0
    for (field = 2; field <= NF; ++field) total += $field
    print total
}' /proc/stat)
printf 'elapsed_seconds\tprocess_ticks\ttotal_ticks\thost_cpu_percent\n' \
    >"${cpu_samples}"
while :; do
    elapsed=$(($(date +%s) - sample_started))
    ((elapsed < duration)) || break
    sample_number=$((sample_number + 1))
    {
        echo "===== sample ${sample_number} $(date -u +%Y-%m-%dT%H:%M:%SZ) ====="
        curl --silent --show-error --max-time 3 \
            --write-out '\ncurl_http_code: %{http_code}\ncurl_time_total_seconds: %{time_total}\n' \
            "${status_url}"
        echo "curl_exit_status: $?"
        echo
    } >>"${status_samples}" 2>&1
    ps -p "${candidate_pid}" -o pid=,etime=,%cpu=,%mem=,rss=,vsz=,stat= \
        >>"${process_samples}" 2>&1 || true
    if [[ -r /proc/${candidate_pid}/stat ]]; then
        current_process_ticks=$(awk '{ print $14 + $15 }' \
            "/proc/${candidate_pid}/stat")
        current_total_ticks=$(awk '/^cpu / {
            total = 0
            for (field = 2; field <= NF; ++field) total += $field
            print total
        }' /proc/stat)
        process_delta=$((current_process_ticks - previous_process_ticks))
        total_delta=$((current_total_ticks - previous_total_ticks))
        cpu_percent=$(awk -v process_delta="${process_delta}" \
            -v total_delta="${total_delta}" 'BEGIN {
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
    kill -0 "${candidate_pid}" 2>/dev/null ||
        die "candidate exited during observation"
    sleep 1
done

prompt_yes()
{
    local prompt=$1
    local answer
    printf '%s [yes/no]: ' "${prompt}" >&2
    IFS= read -r answer || answer=not-recorded
    printf '%s' "${answer}"
}

display_observation=$(prompt_yes \
    "Did all four display panels refresh normally without corruption?")
strip_observation=$(prompt_yes \
    "Was the LED-strip animation smooth with normal colors?")
light_observation=$(prompt_yes \
    "Did sustained cover dim both outputs and uncover restore brightness?")
motion_observation=$(prompt_yes \
    "Did the motion state respond when you moved?")
external_observation=$(prompt_yes \
    "Did the normal external MQTT feed update the display?")
timing_observation=$(prompt_yes \
    "Were display and LED response times acceptable versus production?")
wifi_observation=$(prompt_yes \
    "Did onboard Wi-Fi remain stable and responsive throughout the trial?")

cat >"${output_dir}/operator-notes.txt" <<EOF
display: ${display_observation}
led_strip: ${strip_observation}
light_sensor: ${light_observation}
motion_sensor: ${motion_observation}
external_input: ${external_observation}
timing: ${timing_observation}
onboard_wifi: ${wifi_observation}
EOF

echo "Requesting clean candidate shutdown..."
stop_status=$(curl --silent --output /dev/null --write-out '%{http_code}' \
    --max-time 10 --request POST "${stop_url}" || true)
[[ ${stop_status} == 204 ]] ||
    die "candidate shutdown returned HTTP ${stop_status:-no-response}"
wait_for_exit "${candidate_pid}" 300 ||
    die "candidate did not exit within 30 seconds of HTTP shutdown"
wait "${candidate_pid}"
candidate_exit_status=$?
candidate_pid=

restore_service || die "could not restore active candidate-card service"

failures=0
result_file="${output_dir}/result.txt"
echo "Phase 5 exact-board hardware checks" >"${result_file}"

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

answer_is_yes() { [[ $1 == yes || $1 == y ]]; }
status_has_motion_transition()
{
    [[ $(awk '/^motion: / { print $2 }' "${status_samples}" | sort -u | wc -l) -ge 2 ]]
}
status_has_light_change()
{
    [[ $(awk '/^light_sensor: / { print $2 }' "${status_samples}" | sort -u | wc -l) -ge 2 ]]
}
status_crosses_light_thresholds()
{
    awk -v dark="${dark_threshold}" -v bright="${bright_threshold}" '
        /^light_sensor: / {
            if ($2 < dark) saw_dark = 1
            if ($2 >= bright) saw_bright = 1
        }
        END { exit !(saw_dark && saw_bright) }
    ' "${status_samples}"
}

result_check "candidate returned success after HTTP shutdown" \
    test "${candidate_exit_status}" -eq 0
result_check "candidate status endpoint was sampled" \
    grep -q '^Stats and status$' "${status_samples}"
result_check "status samples include a motion transition" \
    status_has_motion_transition
result_check "status samples include changing light values" \
    status_has_light_change
result_check "status samples cross both dimming thresholds" \
    status_crosses_light_thresholds
result_check "candidate connected to the MQTT broker" \
    grep -q '^mqttBrokerConnected: yes$' "${status_samples}"
result_check "display observation passed" answer_is_yes "${display_observation}"
result_check "LED-strip observation passed" answer_is_yes "${strip_observation}"
result_check "light-sensor observation passed" answer_is_yes "${light_observation}"
result_check "motion-sensor observation passed" answer_is_yes "${motion_observation}"
result_check "external-data observation passed" answer_is_yes "${external_observation}"
result_check "operator timing observation passed" answer_is_yes "${timing_observation}"
result_check "onboard Wi-Fi observation passed" answer_is_yes "${wifi_observation}"
if [[ ${service_was_active} == yes ]]; then
    result_check "candidate-card service was restored" test "${service_restored}" = yes
fi

{
    echo
    echo "failures: ${failures}"
    echo
    echo "Manual rollback remains available: power off this Zero W, reconnect"
    echo "the preserved Zero/Jessie unit, and verify the WiringPi clock."
} >>"${result_file}"

systemctl status "${service_name}" --no-pager \
    >"${output_dir}/service-after.txt" 2>&1 || true
archive_results || die "could not create result archive"

cat "${result_file}"
echo
echo "Share this archive: ${output_dir}.tar.gz"
echo "Archive checksum:   ${output_dir}.tar.gz.sha256"

((failures == 0)) || die "Phase 5 trial recorded ${failures} failure(s)"
