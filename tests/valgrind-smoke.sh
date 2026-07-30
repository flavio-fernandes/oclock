#!/usr/bin/env bash

set -euo pipefail

binary=${1:-./oclock-sandbox}
port=${OCLOCK_VALGRIND_PORT:-18081}
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

valgrind \
    --quiet \
    --error-exitcode=99 \
    --leak-check=full \
    --errors-for-leak-kinds=definite,possible \
    "${binary}" \
    -b 127.0.0.1 \
    -p "${port}" \
    -l "${test_dir}/pulsar.log" \
    -M 127.0.0.1 \
    >"${test_dir}/stdout.log" 2>"${test_dir}/valgrind.log" &
pid=$!

base_url="http://127.0.0.1:${port}"
for _ in $(seq 1 200); do
    if curl --fail --silent "${base_url}/status" >/dev/null; then
        break
    fi
    if ! kill -0 "${pid}" 2>/dev/null; then
        cat "${test_dir}/valgrind.log" >&2
        exit 1
    fi
    sleep 0.05
done

curl --fail --silent --request POST "${base_url}/stop" >/dev/null
if ! wait "${pid}"; then
    cat "${test_dir}/valgrind.log" >&2
    exit 1
fi
pid=
echo "valgrind smoke test passed"
