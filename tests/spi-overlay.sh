#!/bin/bash

set -eu
set -o pipefail

[[ $# == 1 ]] || {
    echo "usage: $0 OVERLAY.dtbo" >&2
    exit 2
}

for command_name in awk dtc fdtoverlay fdtget mktemp rm; do
    command -v "${command_name}" >/dev/null 2>&1 || {
        echo "missing test dependency: ${command_name}" >&2
        exit 2
    }
done

overlay=$1
[[ -r ${overlay} ]] || {
    echo "overlay is not readable: ${overlay}" >&2
    exit 2
}

test_dir=$(mktemp -d /tmp/oclock-spi-overlay-test-XXXXXXXX)
trap 'rm -rf "${test_dir}"' EXIT

base_tree="${test_dir}/base.dtb"
merged_tree="${test_dir}/merged.dtb"

dtc -@ -I dts -O dtb -o "${base_tree}" \
    tests/fixtures/bcm2835-overlay-base.dts
fdtoverlay -i "${base_tree}" -o "${merged_tree}" "${overlay}"

assert_string()
{
    local node=$1
    local property=$2
    local expected=$3
    local actual
    actual=$(fdtget -t s "${merged_tree}" "${node}" "${property}")
    [[ ${actual} == "${expected}" ]] || {
        echo "${node}/${property}: expected ${expected}, got ${actual}" >&2
        exit 1
    }
}

assert_integer()
{
    local node=$1
    local property=$2
    local index=$3
    local expected=$4
    local actual
    actual=$(fdtget -t i "${merged_tree}" "${node}" "${property}" |
        awk -v field="${index}" '{ print $field }')
    [[ ${actual} == "${expected}" ]] || {
        echo "${node}/${property}[${index}]: expected ${expected}, got ${actual}" >&2
        exit 1
    }
}

assert_string /oclock-strip-spi compatible spi-gpio
assert_string /oclock-strip-spi/lpd8806@0 compatible \
    flaviof,oclock-lpd8806
assert_integer /oclock-strip-spi num-chipselects 1 0
assert_integer /oclock-strip-spi sck-gpios 2 20
assert_integer /oclock-strip-spi mosi-gpios 2 21

assert_string /oclock-adc-spi compatible spi-gpio
assert_string /oclock-adc-spi/mcp3002@0 compatible microchip,mcp3002
assert_integer /oclock-adc-spi num-chipselects 1 1
assert_integer /oclock-adc-spi sck-gpios 2 17
assert_integer /oclock-adc-spi miso-gpios 2 27
assert_integer /oclock-adc-spi mosi-gpios 2 22
assert_integer /oclock-adc-spi cs-gpios 2 4
assert_integer /oclock-adc-spi cs-gpios 3 1

echo "SPI overlay tests passed"
