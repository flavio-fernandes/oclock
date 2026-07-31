#!/bin/bash

# Run the Phase 1 WiringPi-backed candidate during a short maintenance window.
# The production service is restored by the EXIT trap on success, failure,
# interruption, or terminal hangup.

set -u
set -o pipefail

binary_path=
candidate_commit=
duration=60
bind_address=0.0.0.0
port=80
service_name=oclock
output_dir=

usage()
{
    cat <<EOF
Usage: sudo $0 --binary PATH --commit SHA [options]

Required:
  --binary PATH       Phase 1 hardware binary built on the Pi
  --commit SHA        Source commit used to build that binary

Options:
  --duration SECONDS  Candidate observation window (default: ${duration})
  --bind ADDRESS      Candidate HTTP bind address (default: ${bind_address})
  --port PORT         Candidate HTTP port (default: ${port})
  --service NAME      Production service to restore (default: ${service_name})
  --output DIRECTORY  New result directory (default: secure directory in /tmp)
  -h, --help          Show this help

This script stops the production service, runs the candidate as root, samples
its status endpoint, requests a clean HTTP shutdown, and restores the production
service. It requires an interactive confirmation immediately before stopping
the service.
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
            candidate_commit=$2
            shift 2
            ;;
        --duration)
            (($# >= 2)) || die "--duration requires a value"
            duration=$2
            shift 2
            ;;
        --bind)
            (($# >= 2)) || die "--bind requires a value"
            bind_address=$2
            shift 2
            ;;
        --port)
            (($# >= 2)) || die "--port requires a value"
            port=$2
            shift 2
            ;;
        --service)
            (($# >= 2)) || die "--service requires a value"
            service_name=$2
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
[[ -n ${candidate_commit} ]] || die "--commit is required"
[[ ${candidate_commit} =~ ^[0-9a-fA-F]{7,40}$ ]] ||
    die "--commit must be a 7-to-40-character hexadecimal Git revision"
[[ ${duration} =~ ^[0-9]+$ ]] ||
    die "--duration must be an integer"
((duration >= 15 && duration <= 600)) ||
    die "--duration must be between 15 and 600 seconds"
[[ -n ${bind_address} && ${bind_address} != -* ]] ||
    die "--bind must be a non-option address"
[[ ${port} =~ ^[0-9]+$ ]] || die "--port must be an integer"
((port >= 1 && port <= 65535)) ||
    die "--port must be between 1 and 65535"
((EUID == 0)) || die "run this script with sudo"

for required_command in awk curl file grep ldd readlink sha256sum sort \
        systemctl tar wc; do
    command -v "${required_command}" >/dev/null 2>&1 ||
        die "required command is missing: ${required_command}"
done

binary_path=$(readlink -f "${binary_path}") ||
    die "cannot resolve candidate binary path"
[[ -f ${binary_path} && -x ${binary_path} ]] ||
    die "candidate is not an executable file: ${binary_path}"
candidate_file=$(file "${binary_path}") ||
    die "could not inspect candidate executable"
grep 'ELF 32-bit LSB executable, ARM' <<<"${candidate_file}" >/dev/null ||
    die "candidate is not the expected 32-bit ARM executable"
candidate_dependencies=$(ldd "${binary_path}" 2>&1) ||
    die "could not inspect candidate dynamic dependencies"
if ! grep 'libwiringPi\.so => /usr/local/lib/libwiringPi\.so' \
        <<<"${candidate_dependencies}" >/dev/null; then
    echo "Observed WiringPi dependency:" >&2
    grep 'libwiringPi' <<<"${candidate_dependencies}" >&2 || echo "  none" >&2
    die "candidate does not resolve the preserved /usr/local WiringPi library"
fi
systemctl is-active --quiet "${service_name}" ||
    die "production service is not active; restore it before this test"

status_url="http://127.0.0.1:${port}/status"
stop_url="http://127.0.0.1:${port}/stop"
production_status_url=http://127.0.0.1:80/status
timestamp=$(date -u +%Y%m%dT%H%M%SZ)

echo "Candidate:          ${binary_path}"
echo "Candidate commit:   ${candidate_commit}"
echo "Production service: ${service_name}"
echo "Candidate HTTP:     ${bind_address}:${port}"
echo "Observation window: ${duration} seconds"
echo
echo "The production clock will be unavailable during this check."
printf 'Type RUN to stop the service and begin: '
IFS= read -r confirmation || die "confirmation was not read"
[[ ${confirmation} == RUN ]] || die "confirmation was not RUN; no change made"

umask 077
if [[ -z ${output_dir} ]]; then
    output_dir=$(mktemp -d "/tmp/oclock-phase1-${timestamp}-XXXXXXXX") ||
        die "cannot create secure result directory"
else
    [[ ${output_dir} != / ]] || die "unsafe output directory"
    mkdir -p "$(dirname "${output_dir}")" ||
        die "cannot create output parent"
    mkdir "${output_dir}" ||
        die "output directory already exists or cannot be created: ${output_dir}"
fi

candidate_pid=
service_was_active=yes
service_restored=no
candidate_exit_status=not-recorded

wait_for_exit()
{
    local pid=$1
    local attempts=${2:-50}
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
    kill -0 "${candidate_pid}" 2>/dev/null || {
        wait "${candidate_pid}" 2>/dev/null
        candidate_exit_status=$?
        candidate_pid=
        return 0
    }

    curl --silent --max-time 2 --request POST "${stop_url}" \
        >/dev/null 2>&1 || true
    if ! wait_for_exit "${candidate_pid}" 50; then
        kill -INT "${candidate_pid}" 2>/dev/null || true
    fi
    if ! wait_for_exit "${candidate_pid}" 50; then
        kill -TERM "${candidate_pid}" 2>/dev/null || true
    fi
    wait "${candidate_pid}" 2>/dev/null
    candidate_exit_status=$?
    candidate_pid=
}

restore_service()
{
    [[ ${service_was_active} == yes ]] || return 0
    if ! systemctl is-active --quiet "${service_name}"; then
        systemctl start "${service_name}" >/dev/null 2>&1 || return 1
    fi

    local i
    for ((i=0; i < 100; ++i)); do
        if systemctl is-active --quiet "${service_name}"; then
            service_restored=yes
            return 0
        fi
        sleep 0.1
    done
    return 1
}

cleanup()
{
    local rc=$?
    trap - EXIT HUP INT TERM
    set +e

    stop_candidate
    if ! restore_service; then
        echo "ERROR: could not restore ${service_name}" >&2
        rc=1
    fi

    echo "Result directory: ${output_dir}" >&2
    exit "${rc}"
}

trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

{
    echo "Phase 1 Pi Zero hardware verification"
    echo
    echo "started_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "candidate_commit: ${candidate_commit}"
    echo "candidate_binary: ${binary_path}"
    echo "candidate_sha256: $(sha256sum "${binary_path}" | awk '{ print $1 }')"
    echo "duration_seconds: ${duration}"
    echo "bind_address: ${bind_address}"
    echo "http_port: ${port}"
} >"${output_dir}/context.txt"

file "${binary_path}" >"${output_dir}/candidate-file.txt" 2>&1
ldd "${binary_path}" >"${output_dir}/candidate-dependencies.txt" 2>&1
systemctl status "${service_name}" --no-pager \
    >"${output_dir}/production-before.txt" 2>&1 || true

echo "Stopping ${service_name}..."
systemctl stop "${service_name}" ||
    die "systemctl could not stop ${service_name}"
if systemctl is-active --quiet "${service_name}"; then
    die "${service_name} is still active; refusing to start candidate"
fi
if curl --fail --silent --max-time 1 "${status_url}" >/dev/null 2>&1; then
    die "HTTP port ${port} is still serving after ${service_name} stopped"
fi

runtime_dir="${output_dir}/runtime"
mkdir "${runtime_dir}"
mkdir "${runtime_dir}/log"

echo "Starting Phase 1 candidate on ${bind_address}:${port}..."
(
    cd "${runtime_dir}" || exit 1
    exec "${binary_path}" -b "${bind_address}" -p "${port}" \
        -l "${output_dir}/candidate.log"
) >"${output_dir}/candidate-stdout.txt" \
  2>"${output_dir}/candidate-stderr.txt" &
candidate_pid=$!

ready=no
for ((i=0; i < 100; ++i)); do
    if curl --fail --silent --max-time 2 "${status_url}" \
            >"${output_dir}/initial-status.txt"; then
        ready=yes
        break
    fi
    if ! kill -0 "${candidate_pid}" 2>/dev/null; then
        break
    fi
    sleep 0.1
done
[[ ${ready} == yes ]] ||
    die "candidate did not become ready; see candidate-stderr.txt"

echo
echo "Candidate is running. During the next ${duration} seconds:"
echo "  - watch the four-panel display and LED-strip animation;"
echo "  - cover and uncover the light sensor;"
echo "  - walk into and out of the motion sensor field;"
echo "  - trigger the normal external data feed and verify its display update."

status_samples="${output_dir}/status-samples.txt"
sample_started=$(date +%s)
sample_number=0
while :; do
    now=$(date +%s)
    elapsed=$((now - sample_started))
    ((elapsed < duration)) || break
    sample_number=$((sample_number + 1))
    {
        echo "===== sample ${sample_number} $(date -u +%Y-%m-%dT%H:%M:%SZ) ====="
        curl --silent --show-error --max-time 2 "${status_url}"
        echo "curl_exit_status: $?"
        echo
    } >>"${status_samples}" 2>&1
    kill -0 "${candidate_pid}" 2>/dev/null ||
        die "candidate exited during observation"
    sleep 1
done

grep -E \
    '^(motion|motion_last_change|light_sensor|display_mode|led_strip_mode|mqttBrokerConnected|mqttMessages|dictAdds):' \
    "${status_samples}" >"${output_dir}/observed-status-summary.txt" || true

prompt_yes()
{
    local prompt=$1
    local answer
    # The function's stdout is captured as its return value. Keep the prompt
    # visible on the terminal by writing it to stderr.
    printf '%s [yes/no]: ' "${prompt}" >&2
    IFS= read -r answer || answer=not-recorded
    printf '%s' "${answer}"
}

display_observation=$(prompt_yes \
    "Did the four-panel display refresh normally without corruption?")
strip_observation=$(prompt_yes \
    "Was the LED-strip animation smooth with normal colors?")
light_observation=$(prompt_yes \
    "Did the light value respond when the sensor was covered?")
motion_observation=$(prompt_yes \
    "Did the motion state respond when you moved?")
external_input_observation=$(prompt_yes \
    "Did the normal external data feed update the display?")

cat >"${output_dir}/operator-notes.txt" <<EOF
display: ${display_observation}
led_strip: ${strip_observation}
light_sensor: ${light_observation}
motion_sensor: ${motion_observation}
external_input: ${external_input_observation}
EOF

echo "Requesting clean candidate shutdown..."
stop_status=$(curl --silent --output /dev/null --write-out '%{http_code}' \
    --max-time 5 --request POST "${stop_url}" || true)
[[ ${stop_status} == 204 ]] ||
    die "candidate shutdown returned HTTP ${stop_status:-no-response}"

wait "${candidate_pid}"
candidate_exit_status=$?
candidate_pid=

echo "Restoring ${service_name}..."
restore_service || die "could not restart ${service_name}"

production_ready=no
for ((i=0; i < 100; ++i)); do
    if curl --fail --silent --max-time 2 "${production_status_url}" \
            >"${output_dir}/production-status.txt"; then
        production_ready=yes
        break
    fi
    sleep 0.1
done
[[ ${production_ready} == yes ]] ||
    die "production service is active but its status endpoint is not ready"

production_observation=$(prompt_yes \
    "Is the restored production clock visually healthy?")
echo "production_restored: ${production_observation}" \
    >>"${output_dir}/operator-notes.txt"

failures=0
result_file="${output_dir}/result.txt"
echo "Phase 1 hardware checks" >"${result_file}"

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

answer_is_yes()
{
    [[ $1 == yes || $1 == y ]]
}

status_has_motion_transition()
{
    local count
    count=$(awk '/^motion: / { print $2 }' "${status_samples}" |
        sort -u | wc -l)
    ((count >= 2))
}

status_has_light_change()
{
    local count
    count=$(awk '/^light_sensor: / { print $2 }' "${status_samples}" |
        sort -u | wc -l)
    ((count >= 2))
}

status_has_mqtt_connection()
{
    grep -q '^mqttBrokerConnected: yes$' "${status_samples}"
}

result_check "candidate returned success after HTTP shutdown" \
    test "${candidate_exit_status}" -eq 0
result_check "candidate status endpoint was sampled" \
    grep -q '^Stats and status$' "${status_samples}"
result_check "status samples include a motion transition" \
    status_has_motion_transition
result_check "status samples include changing light values" \
    status_has_light_change
result_check "candidate connected to the configured MQTT broker" \
    status_has_mqtt_connection
result_check "display observation passed" \
    answer_is_yes "${display_observation}"
result_check "LED-strip observation passed" \
    answer_is_yes "${strip_observation}"
result_check "light-sensor observation passed" \
    answer_is_yes "${light_observation}"
result_check "motion-sensor observation passed" \
    answer_is_yes "${motion_observation}"
result_check "external data-feed observation passed" \
    answer_is_yes "${external_input_observation}"
result_check "production service was restored" \
    test "${service_restored}" = yes
result_check "restored production observation passed" \
    answer_is_yes "${production_observation}"

{
    echo
    echo "failures: ${failures}"
} >>"${result_file}"

systemctl status "${service_name}" --no-pager \
    >"${output_dir}/production-after.txt" 2>&1 || true

archive_path="${output_dir}.tar.gz"
tar -czf "${archive_path}" -C "$(dirname "${output_dir}")" \
    "$(basename "${output_dir}")" ||
    die "could not create result archive"
sha256sum "${archive_path}" >"${archive_path}.sha256"

if [[ -n ${SUDO_UID:-} && -n ${SUDO_GID:-} ]]; then
    chown "${SUDO_UID}:${SUDO_GID}" "${archive_path}" \
        "${archive_path}.sha256" 2>/dev/null || true
fi

cat "${result_file}"
echo
echo "Share this archive: ${archive_path}"
echo "Archive checksum:   ${archive_path}.sha256"

((failures == 0)) ||
    die "Phase 1 hardware check recorded ${failures} failure(s)"
