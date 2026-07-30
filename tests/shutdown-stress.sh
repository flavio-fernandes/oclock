#!/usr/bin/env bash

set -euo pipefail

binary=${1:-./oclock-sandbox}
base_port=${OCLOCK_SHUTDOWN_TEST_PORT:-18100}
iterations=${OCLOCK_SHUTDOWN_ITERATIONS:-20}
test_dir=$(mktemp -d)
pid=

cleanup() {
    if [[ -n ${pid} ]] && kill -0 "${pid}" 2>/dev/null; then
        kill "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
    fi
    rm -rf "${test_dir}"
}
trap cleanup EXIT

for iteration in $(seq 1 "${iterations}"); do
    port=$((base_port + iteration))
    run_dir="${test_dir}/${iteration}"
    mkdir "${run_dir}"

    "${binary}" \
        -b 127.0.0.1 \
        -p "${port}" \
        -w 1 \
        -l "${run_dir}/pulsar.log" \
        -M 127.0.0.1 \
        >"${run_dir}/stdout.log" 2>"${run_dir}/stderr.log" &
    pid=$!

    base_url="http://127.0.0.1:${port}"
    for _ in $(seq 1 100); do
        if curl --fail --silent "${base_url}/status" >/dev/null; then
            break
        fi
        if ! kill -0 "${pid}" 2>/dev/null; then
            echo "oclock exited before shutdown iteration ${iteration}" >&2
            cat "${run_dir}/stderr.log" >&2
            exit 1
        fi
        sleep 0.05
    done

    stop_method=(--request POST)
    if (( iteration % 2 == 0 )); then
        stop_method=()
    fi
    stop_status=$(curl --silent --output /dev/null --write-out '%{http_code}' \
        "${stop_method[@]}" "${base_url}/stop")
    if [[ ${stop_status} != 204 ]]; then
        echo "shutdown iteration ${iteration} returned HTTP ${stop_status}" >&2
        exit 1
    fi

    if ! wait "${pid}"; then
        echo "oclock failed during shutdown iteration ${iteration}" >&2
        cat "${run_dir}/stderr.log" >&2
        exit 1
    fi
    pid=
done

echo "graceful shutdown stress test passed (${iterations} iterations)"
