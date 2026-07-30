#!/usr/bin/env bash

set -euo pipefail

binary=${1:-./oclock-sandbox}
test_dir=$(mktemp -d)

cleanup() {
    rm -rf "${test_dir}"
}
trap cleanup EXIT

# The original CLI printed help and returned failure. More importantly, the
# help text exposes the deployed network defaults without binding port 80.
if "${binary}" -h >"${test_dir}/help.txt" 2>&1; then
    echo "legacy help exit status changed" >&2
    exit 1
fi
grep -q 'IPv4 address to bind (default: 0.0.0.0)' "${test_dir}/help.txt"
grep -q 'tcp port number to listen on (default: 80)' "${test_dir}/help.txt"
grep -q 'broker to connect to (default: 192.168.10.238)' "${test_dir}/help.txt"
grep -q 'port of that mqtt server (default: 1883)' "${test_dir}/help.txt"
grep -q 'keep alive interval in seconds (default: 182)' "${test_dir}/help.txt"

# Preserve the service and plain `make` deployment contracts used on Jessie.
grep -qx 'After=network.target' misc/oclock.service
grep -qx 'ExecStart=/home/pi/oclock.git/oclock' misc/oclock.service
grep -qx 'StandardOutput=null' misc/oclock.service
grep -qx 'Alias=oclock.service' misc/oclock.service
make -n sudo_oclock >"${test_dir}/make.txt"
grep -q 'sudo chown root:root oclock' "${test_dir}/make.txt"
grep -q 'sudo chmod u+s oclock' "${test_dir}/make.txt"
make -n CC=legacy-cxx hardware >"${test_dir}/compiler.txt"
grep -q 'legacy-cxx -c' "${test_dir}/compiler.txt"

# Existing clients may abbreviate display POST keys; preserve that behavior.
grep -Fq 'strncasecmp(key, "msg", strlen(key)) == 0' src/displayInternal.cpp
grep -Fq 'strncasecmp(key, "animationStep", strlen(key)) == 0' src/displayInternal.cpp

# Keep the Phase 0 hardware collector runnable on the legacy Bash environment
# without executing its production-only collection path in the sandbox.
bash -n misc/collectHardwareBaseline.sh
misc/collectHardwareBaseline.sh --help >"${test_dir}/collector-help.txt"
grep -q -- '--duration SECONDS' "${test_dir}/collector-help.txt"
grep -q -- '--binary PATH' "${test_dir}/collector-help.txt"
if misc/collectHardwareBaseline.sh --duration 0 \
        >"${test_dir}/collector-invalid.txt" 2>&1; then
    echo "hardware collector accepted an invalid duration" >&2
    exit 1
fi

echo "legacy compatibility tests passed"
