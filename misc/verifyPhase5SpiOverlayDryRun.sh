#!/bin/bash

# Compile and merge the Phase 5 SPI overlay into a file copy of the active
# Device Tree. Nothing is applied to the live tree and no GPIO is requested.

set -u
set -o pipefail

overlay_source=
output_dir=
expected_model="Raspberry Pi Zero W Rev 1.1"
expected_revision=9000c1

usage()
{
    cat <<EOF
Usage: sudo $0 --overlay SOURCE.dts [--output DIRECTORY]

Compile the disabled Office Clock SPI overlay and merge it into an offline
copy of the selected Zero W/Trixie Device Tree. The service must be inactive.
This does not load a module, apply an overlay, bind a driver, open a device,
request a GPIO line, or modify /boot.
EOF
}

die()
{
    echo "error: $*" >&2
    exit 2
}

while (($# > 0)); do
    case "$1" in
        --overlay)
            (($# >= 2)) || die "--overlay requires a value"
            overlay_source=$2
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

((EUID == 0)) || die "run this verifier with sudo"
[[ -n ${overlay_source} && -r ${overlay_source} ]] ||
    die "--overlay must name a readable Device Tree source"

for command_name in awk basename chown date dirname dpkg dtc fdtoverlay \
        fdtget grep id mkdir mktemp modinfo rm sha256sum sort stat systemctl \
        tail tar tr uname; do
    command -v "${command_name}" >/dev/null 2>&1 ||
        die "required command is missing: ${command_name}"
done

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
umask 077
if [[ -z ${output_dir} ]]; then
    output_dir=$(mktemp -d "/tmp/oclock-phase5-spi-dry-run-${timestamp}-XXXXXXXX") ||
        die "cannot create result directory"
else
    [[ ${output_dir} != / ]] || die "unsafe output directory"
    mkdir -p "$(dirname "${output_dir}")" || die "cannot create output parent"
    mkdir "${output_dir}" ||
        die "output directory already exists or cannot be created"
fi

archive_path="${output_dir%/}.tar.gz"
result_file="${output_dir}/result.txt"
failures=0

finish()
{
    local rc=$?
    set +e
    if [[ -d ${output_dir} ]]; then
        # Never package complete live-tree copies; they can contain unrelated
        # board identifiers. This also protects early-error paths.
        rm -f "${output_dir}/active-base.dtb" \
            "${output_dir}/merged-offline.dtb"
        if tar -czf "${archive_path}" -C "$(dirname "${output_dir}")" \
                "$(basename "${output_dir}")" && (
            cd "$(dirname "${archive_path}")" || exit 1
            sha256sum "$(basename "${archive_path}")" \
                >"$(basename "${archive_path}").sha256"
        ); then
            if [[ -n ${SUDO_UID:-} && -n ${SUDO_GID:-} ]]; then
                chown "${SUDO_UID}:${SUDO_GID}" "${archive_path}" \
                    "${archive_path}.sha256" 2>/dev/null || true
            fi
            echo "Share this archive: ${archive_path}" >&2
            echo "Archive checksum:   ${archive_path}.sha256" >&2
        else
            rc=1
        fi
        echo "Result directory:   ${output_dir}" >&2
    fi
    exit "${rc}"
}
trap finish EXIT

ok()
{
    echo "OK: $*" >>"${result_file}"
}

fail()
{
    echo "FAIL: $*" >>"${result_file}"
    failures=$((failures + 1))
}

model=unknown
if [[ -r /proc/device-tree/model ]]; then
    model=$(tr -d '\0' </proc/device-tree/model)
fi
revision=$(awk -F: '/^Revision/ {
    gsub(/[[:space:]]/, "", $2); print tolower($2)
}' /proc/cpuinfo | tail -1)
architecture=$(dpkg --print-architecture)
machine=$(uname -m)
os_codename=unknown
if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    source /etc/os-release
    os_codename=${VERSION_CODENAME:-unknown}
fi

{
    echo "Office Clock Phase 5 SPI overlay offline verification"
    echo
    echo "collected_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "model: ${model}"
    echo "revision: ${revision:-unknown}"
    echo "kernel: $(uname -srvm)"
    echo "machine: ${machine}"
    echo "architecture: ${architecture}"
    echo "os_codename: ${os_codename}"
    echo "overlay_source: ${overlay_source}"
    sha256sum "${overlay_source}"
} >"${output_dir}/context.txt"

echo "Phase 5 SPI overlay offline checks" >"${result_file}"

if [[ ${model} == "${expected_model}" && ${revision:-unknown} == "${expected_revision}" &&
      ${machine} == armv6l && ${architecture} == armhf &&
      ${os_codename} == trixie ]]; then
    ok "target is the selected Zero W/Trixie ARMv6 image"
else
    fail "target identity does not match the selected Zero W/Trixie image"
fi

if systemctl is-active --quiet oclock; then
    fail "oclock.service is active; stop it before overlay work"
else
    ok "oclock.service is inactive"
fi

if [[ -e /proc/device-tree/oclock-strip-spi ||
      -e /proc/device-tree/oclock-adc-spi ]]; then
    fail "Office Clock SPI overlay nodes are already active"
else
    ok "Office Clock SPI overlay is not active"
fi

for module_name in spi-gpio spidev mcp320x; do
    if modinfo "${module_name}" >/dev/null 2>&1; then
        ok "${module_name} is available for the running kernel"
    else
        fail "${module_name} is unavailable for the running kernel"
    fi
done

overlay_binary="${output_dir}/oclock-spi-overlay.dtbo"
base_tree="${output_dir}/active-base.dtb"
merged_tree="${output_dir}/merged-offline.dtb"

if dtc -@ -I dts -O dtb -o "${overlay_binary}" "${overlay_source}" \
        >"${output_dir}/overlay-compile.txt" 2>&1; then
    ok "overlay compiled with target dtc"
else
    fail "overlay did not compile with target dtc"
fi

if dtc -I fs -O dtb -o "${base_tree}" /proc/device-tree \
        >"${output_dir}/active-tree-copy.txt" 2>&1; then
    ok "active Device Tree was copied to an offline blob"
else
    fail "active Device Tree could not be copied"
fi

if [[ -r ${overlay_binary} && -r ${base_tree} ]] &&
        fdtoverlay -i "${base_tree}" -o "${merged_tree}" "${overlay_binary}" \
            >"${output_dir}/overlay-merge.txt" 2>&1; then
    ok "overlay merged into the offline Device Tree"
else
    fail "overlay did not merge into the offline Device Tree"
fi

property_equals()
{
    local node=$1
    local property=$2
    local expected=$3
    [[ -r ${merged_tree} ]] || return 1
    [[ $(fdtget -t s "${merged_tree}" "${node}" "${property}" 2>/dev/null) == \
        "${expected}" ]]
}

gpio_offset_equals()
{
    local node=$1
    local property=$2
    local expected_offset=$3
    local values
    [[ -r ${merged_tree} ]] || return 1
    values=$(fdtget -t i "${merged_tree}" "${node}" "${property}" 2>/dev/null) ||
        return 1
    [[ $(awk '{ print $(NF - 1) }' <<<"${values}") == "${expected_offset}" ]]
}

if property_equals /oclock-strip-spi compatible spi-gpio &&
        property_equals /oclock-strip-spi/lpd8806@0 compatible \
            flaviof,oclock-lpd8806 &&
        gpio_offset_equals /oclock-strip-spi sck-gpios 20 &&
        gpio_offset_equals /oclock-strip-spi mosi-gpios 21; then
    ok "offline strip controller preserves GPIO20 clock and GPIO21 data"
else
    fail "offline strip controller properties are not the reviewed mapping"
fi

if property_equals /oclock-adc-spi compatible spi-gpio &&
        property_equals /oclock-adc-spi/mcp3002@0 compatible \
            microchip,mcp3002 &&
        gpio_offset_equals /oclock-adc-spi sck-gpios 17 &&
        gpio_offset_equals /oclock-adc-spi miso-gpios 27 &&
        gpio_offset_equals /oclock-adc-spi mosi-gpios 22 &&
        gpio_offset_equals /oclock-adc-spi cs-gpios 4; then
    ok "offline MCP3002 controller preserves GPIO17/27/22/4"
else
    fail "offline MCP3002 controller properties are not the reviewed mapping"
fi

{
    for node in /oclock-strip-spi /oclock-strip-spi/lpd8806@0 \
            /oclock-adc-spi /oclock-adc-spi/mcp3002@0; do
        echo "===== ${node} ====="
        for property in compatible num-chipselects sck-gpios miso-gpios \
                mosi-gpios cs-gpios reg spi-max-frequency; do
            if fdtget -p "${merged_tree}" "${node}" 2>/dev/null |
                    grep -Fxq "${property}"; then
                printf '%s: ' "${property}"
                fdtget -t x "${merged_tree}" "${node}" "${property}"
            fi
        done
        echo
    done
} >"${output_dir}/merged-oclock-properties.txt"

# Full Device Trees may contain board identifiers and unrelated local
# configuration. Retain only the reviewed Office Clock properties above.
rm -f "${base_tree}" "${merged_tree}"

{
    echo
    echo "failures: ${failures}"
    echo
    echo "The temporary full Device Tree copies were removed before archiving."
    echo "Only files in ${output_dir} were created. No live Device Tree, module,"
    echo "driver binding, device node, GPIO line, service, package, or boot file"
    echo "was changed."
} >>"${result_file}"

cat "${result_file}"
if ((failures > 0)); then
    die "SPI overlay offline verification recorded ${failures} failure(s)"
fi
