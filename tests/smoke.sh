#!/usr/bin/env bash

set -euo pipefail

binary=${1:-./oclock-sandbox}
port=${OCLOCK_TEST_PORT:-18080}
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

if "${binary}" -p 0 -l "${test_dir}/invalid-port.log" >/dev/null 2>&1; then
    echo "invalid port was accepted" >&2
    exit 1
fi
if "${binary}" -b not-an-ip -p "${port}" -l "${test_dir}/invalid-address.log" >/dev/null 2>&1; then
    echo "invalid bind address was accepted" >&2
    exit 1
fi

"${binary}" \
    -b 127.0.0.1 \
    -p "${port}" \
    -l "${test_dir}/pulsar.log" \
    -M 127.0.0.1 \
    >"${test_dir}/stdout.log" 2>"${test_dir}/stderr.log" &
pid=$!

base_url="http://127.0.0.1:${port}"
for _ in $(seq 1 100); do
    if curl --fail --silent "${base_url}/status" >"${test_dir}/status.txt"; then
        break
    fi
    if ! kill -0 "${pid}" 2>/dev/null; then
        echo "oclock exited before becoming ready" >&2
        cat "${test_dir}/stderr.log" >&2
        exit 1
    fi
    sleep 0.05
done

grep -q "Stats and status" "${test_dir}/status.txt"
[[ -f "${test_dir}/pulsar.log" ]]
curl --fail --silent --show-error "${base_url}/" >/dev/null

request_pids=()
for _ in $(seq 1 32); do
    curl --fail --silent "${base_url}/status" >/dev/null &
    request_pids+=("$!")
done
for request_pid in "${request_pids[@]}"; do
    wait "${request_pid}"
done

printf -v long_message '%300s' ''
long_message=${long_message// /A}
long_message_status=$(curl --silent --output /dev/null --write-out '%{http_code}' \
    --request POST "${base_url}/msgMode" \
    --data-urlencode "msg=${long_message}")
[[ ${long_message_status} == 204 ]]

invalid_status=$(curl --silent --output /dev/null --write-out '%{http_code}' \
    --request POST "${base_url}/dictionary" \
    --data 'dictionaryOperation=add&dictionaryKey=bad&dictionaryData=x&dictionaryTimeout=not-a-number')
[[ ${invalid_status} == 500 ]]

empty_status=$(curl --silent --output /dev/null --write-out '%{http_code}' \
    --request POST "${base_url}/dictionary" \
    --data 'dictionaryOperation=add&dictionaryKey=empty-value&dictionaryData=&dictionaryTimeout=-1')
[[ ${empty_status} == 204 ]]
curl --fail --silent --show-error "${base_url}/status" |
    grep -q 'dict: empty-value =>'

get_stop_status=$(curl --silent --output /dev/null --write-out '%{http_code}' \
    "${base_url}/stop")
[[ ${get_stop_status} == 404 ]]

post_stop_status=$(curl --silent --output /dev/null --write-out '%{http_code}' \
    --request POST "${base_url}/stop")
[[ ${post_stop_status} == 204 ]]

wait "${pid}"
pid=
echo "smoke test passed"
