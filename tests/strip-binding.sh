#!/bin/bash

# Exercise misc/bindOclockStripSpi.sh against a fake sysfs tree.
#
# The reboot gate proves the binding works on the real unit. This test covers
# what a single hardware gate cannot: every refusal path, the idempotent
# re-run, and the exact order of writes the helper performs. Those are the
# parts most likely to regress silently, because a bind that half-succeeds
# still leaves a character device behind.
#
# The fake tree cannot contain a real character device -- mknod is not
# permitted in an unprivileged container -- so the strip's node is a symlink to
# /dev/null, which is a character device and satisfies the same test.

set -eu
set -o pipefail

helper=${1:-misc/bindOclockStripSpi.sh}
[[ -x ${helper} ]] || {
    echo "helper is not executable: ${helper}" >&2
    exit 2
}
helper=$(readlink -f "${helper}")

failures=0
checks=0

work_dir=$(mktemp -d /tmp/oclock-strip-binding-XXXXXXXX)
trap 'rm -rf "${work_dir}"' EXIT

pass()
{
    checks=$((checks + 1))
    echo "ok   - $1"
}

fail()
{
    checks=$((checks + 1))
    failures=$((failures + 1))
    echo "FAIL - $1" >&2
    [[ $# -lt 2 ]] || echo "       $2" >&2
}

# Build a fake tree. $1 is the tree name; the strip child is spi4.0 and the
# ADC-like decoy is spi3.0, so a helper that matches too loosely is caught.
make_tree()
{
    local root="${work_dir}/$1"
    local devices="${root}/sys/bus/spi/devices"
    local drivers="${root}/sys/bus/spi/drivers/spidev"
    local dt="${root}/proc/device-tree"

    mkdir -p "${devices}/spi4.0" "${devices}/spi3.0" "${drivers}" \
        "${root}/dev" \
        "${dt}/oclock-strip-spi/lpd8806@0" \
        "${dt}/oclock-adc-spi/mcp3002@0"

    ln -s "${dt}/oclock-strip-spi/lpd8806@0" "${devices}/spi4.0/of_node"
    ln -s "${dt}/oclock-adc-spi/mcp3002@0" "${devices}/spi3.0/of_node"

    : >"${devices}/spi4.0/driver_override"
    : >"${devices}/spi3.0/driver_override"
    : >"${drivers}/bind"
    : >"${drivers}/unbind"

    # The decoy is bound to mcp320x, as on the real unit.
    mkdir -p "${root}/sys/bus/spi/drivers/mcp320x"
    ln -s "${root}/sys/bus/spi/drivers/mcp320x" "${devices}/spi3.0/driver"

    echo "${root}"
}

# Simulate what the kernel does on a successful bind: the driver symlink
# appears and udev creates the character device.
simulate_kernel_bind()
{
    local root=$1
    ln -sfn "${root}/sys/bus/spi/drivers/spidev" \
        "${root}/sys/bus/spi/devices/spi4.0/driver"
    ln -sfn /dev/null "${root}/dev/spidev4.0"
}

run_helper()
{
    local root=$1
    shift
    OCLOCK_BIND_TEST_ROOT="${root}" "${helper}" "$@" 2>&1
}

echo "== bind refuses when the strip child is absent =="
root=$(make_tree absent)
rm -rf "${root:?}/sys/bus/spi/devices/spi4.0"
if output=$(run_helper "${root}" bind --wait 0); then
    fail "bind must fail with no strip child" "${output}"
else
    if [[ ${output} == *"did not appear within"* ]]; then
        pass "bind reports the child never appeared"
    else
        fail "bind gave the wrong reason" "${output}"
    fi
fi

echo "== bind names the overlay when the Device Tree child is missing too =="
root=$(make_tree no_overlay)
rm -rf "${root:?}/sys/bus/spi/devices/spi4.0"
rm -rf "${root:?}/proc/device-tree/oclock-strip-spi"
output=$(run_helper "${root}" bind --wait 0) && rc=0 || rc=$?
if ((rc != 0)) && [[ ${output} == *"dtoverlay=oclock-spi"* ]]; then
    pass "bind points at the overlay directive"
else
    fail "bind should name the overlay when the DT child is absent" "${output}"
fi

echo "== bind refuses when two strip children match =="
root=$(make_tree duplicate)
mkdir -p "${root}/sys/bus/spi/devices/spi5.0"
ln -s "${root}/proc/device-tree/oclock-strip-spi/lpd8806@0" \
    "${root}/sys/bus/spi/devices/spi5.0/of_node"
: >"${root}/sys/bus/spi/devices/spi5.0/driver_override"
output=$(run_helper "${root}" bind --wait 0) && rc=0 || rc=$?
if ((rc != 0)) && [[ ${output} == *"found 2"* ]]; then
    pass "bind refuses an ambiguous match instead of picking one"
else
    fail "bind should refuse two matching children" "${output}"
fi

echo "== bind refuses a child already bound to another driver =="
root=$(make_tree wrong_driver)
mkdir -p "${root}/sys/bus/spi/drivers/mcp320x"
ln -sfn "${root}/sys/bus/spi/drivers/mcp320x" \
    "${root}/sys/bus/spi/devices/spi4.0/driver"
output=$(run_helper "${root}" bind --wait 0) && rc=0 || rc=$?
if ((rc != 0)) && [[ ${output} == *"bound to mcp320x, not spidev"* ]]; then
    pass "bind refuses to steal a child from another driver"
else
    fail "bind should refuse a foreign driver" "${output}"
fi

echo "== bind writes driver_override then the child name =="
root=$(make_tree happy)
simulate_kernel_bind "${root}"
# Clear the simulated result so the helper takes the real bind path, then let
# the write to the bind file trigger the simulated kernel response.
rm -f "${root}/sys/bus/spi/devices/spi4.0/driver" "${root}/dev/spidev4.0"
# The helper polls for the end state after writing, so a delayed simulation is
# the honest model of the kernel reacting to the bind write.
(
    sleep 0.3
    simulate_kernel_bind "${root}"
) &
simulator=$!
output=$(run_helper "${root}" bind --wait 5) && rc=0 || rc=$?
wait "${simulator}" 2>/dev/null || true
if ((rc == 0)); then
    pass "bind succeeds on a healthy tree"
else
    fail "bind should succeed on a healthy tree" "${output}"
fi

override=$(cat "${root}/sys/bus/spi/devices/spi4.0/driver_override")
if [[ ${override} == spidev ]]; then
    pass "driver_override was set to spidev"
else
    fail "driver_override should be spidev" "got '${override}'"
fi

bound=$(cat "${root}/sys/bus/spi/drivers/spidev/bind")
if [[ ${bound} == spi4.0 ]]; then
    pass "the strip child name was written to the spidev bind control"
else
    fail "bind control should receive spi4.0" "got '${bound}'"
fi

decoy_override=$(cat "${root}/sys/bus/spi/devices/spi3.0/driver_override")
if [[ -z ${decoy_override} ]]; then
    pass "the ADC child was left untouched"
else
    fail "the ADC child must not be modified" "got '${decoy_override}'"
fi

echo "== bind is idempotent =="
root=$(make_tree idempotent)
simulate_kernel_bind "${root}"
printf 'spidev\n' >"${root}/sys/bus/spi/devices/spi4.0/driver_override"
output=$(run_helper "${root}" bind --wait 0) && rc=0 || rc=$?
if ((rc == 0)) && [[ ${output} == *"already bound"* ]]; then
    pass "a second bind succeeds without acting"
else
    fail "bind should be idempotent" "${output}"
fi
bound=$(cat "${root}/sys/bus/spi/drivers/spidev/bind")
if [[ -z ${bound} ]]; then
    pass "the idempotent path issues no bind write"
else
    fail "the idempotent path must not write to bind" "got '${bound}'"
fi

echo "== unbind reverses the binding =="
root=$(make_tree unbind)
simulate_kernel_bind "${root}"
printf 'spidev\n' >"${root}/sys/bus/spi/devices/spi4.0/driver_override"
# Simulate the kernel tearing the device down in response to the write.
(
    sleep 0.3
    rm -f "${root}/sys/bus/spi/devices/spi4.0/driver" "${root}/dev/spidev4.0"
) &
simulator=$!
output=$(run_helper "${root}" unbind) && rc=0 || rc=$?
wait "${simulator}" 2>/dev/null || true
if ((rc == 0)); then
    pass "unbind succeeds on a bound tree"
else
    fail "unbind should succeed on a bound tree" "${output}"
fi
unbound=$(cat "${root}/sys/bus/spi/drivers/spidev/unbind")
if [[ ${unbound} == spi4.0 ]]; then
    pass "the strip child name was written to the spidev unbind control"
else
    fail "unbind control should receive spi4.0" "got '${unbound}'"
fi
override=$(cat "${root}/sys/bus/spi/devices/spi4.0/driver_override")
if [[ -z ${override} || ${override} == $'\n' ]]; then
    pass "driver_override was cleared"
else
    fail "driver_override should be cleared" "got '${override}'"
fi

echo "== unbind is a no-op on an unbound tree =="
root=$(make_tree already_unbound)
output=$(run_helper "${root}" unbind) && rc=0 || rc=$?
if ((rc == 0)) && [[ ${output} == *"not bound"* ]]; then
    pass "unbind tolerates an already-unbound child"
else
    fail "unbind should be a no-op when unbound" "${output}"
fi

echo
echo "checks: ${checks}, failures: ${failures}"
((failures == 0)) || exit 1
echo "strip binding tests passed"
