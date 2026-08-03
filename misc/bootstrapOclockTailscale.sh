#!/bin/bash

# One-time, interactive remote-maintenance bootstrap for the experimental
# Office Clock Zero W. It installs official Tailscale packages and authorizes
# one dedicated Macmini OpenSSH key restricted to that Macmini's tailnet IP.

set -euo pipefail

expected_model="Raspberry Pi Zero W Rev 1.1"
expected_revision=9000c1
source_ip=
public_key='ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIJjvwJZ6+BztayoOnGPuZPV2lgUBzwXzgZhzCWfxDrSv codex-office-clock-2026-08-02'

usage()
{
    cat <<EOF
Usage: $0 --source-ip MACMINI_TAILSCALE_IPV4

Run interactively as pi on the selected Zero W/Trixie target. After a REMOTE
confirmation, this script:

1. adds one dedicated, source-IP-restricted OpenSSH public key;
2. installs Tailscale from its official Raspbian Trixie repository;
3. starts tailscaled and runs an interactive tailscale login;
4. leaves normal Wi-Fi, default routes, DNS, sshd, boot files, GPIO, SPI, and
   oclock.service configuration unchanged.

Find the Macmini address before running this script with: tailscale ip -4
EOF
}

die()
{
    echo "error: $*" >&2
    exit 2
}

while (($# > 0)); do
    case "$1" in
        --source-ip)
            (($# >= 2)) || die "--source-ip requires a value"
            source_ip=$2
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

[[ -n ${source_ip} ]] || die "--source-ip is required"
IFS=. read -r source_a source_b source_c source_d <<<"${source_ip}"
[[ ${source_a} == 100 && ${source_b} =~ ^[0-9]+$ &&
   ${source_c} =~ ^[0-9]+$ && ${source_d} =~ ^[0-9]+$ &&
   ${source_b} -ge 64 && ${source_b} -le 127 &&
   ${source_c} -le 255 && ${source_d} -le 255 ]] ||
    die "--source-ip must be a Tailscale IPv4 address in 100.64.0.0/10"

authorized_key="from=\"${source_ip}\",no-agent-forwarding,no-port-forwarding,no-X11-forwarding,no-user-rc ${public_key}"

[[ $(id -un) == pi ]] || die "run this bootstrap as the pi user, not root"
[[ ${HOME} == /home/pi ]] || die "expected pi home directory /home/pi"

for command_name in apt-get awk dpkg grep id install mktemp sed systemctl \
        tail tr uname; do
    command -v "${command_name}" >/dev/null 2>&1 ||
        die "required command is missing: ${command_name}"
done

model=unknown
if [[ -r /proc/device-tree/model ]]; then
    model=$(tr -d '\0' </proc/device-tree/model)
fi
revision=$(awk -F: '/^Revision/ {
    gsub(/[[:space:]]/, "", $2); print tolower($2)
}' /proc/cpuinfo | tail -1)
machine=$(uname -m)
architecture=$(dpkg --print-architecture)
os_codename=unknown
if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    source /etc/os-release
    os_codename=${VERSION_CODENAME:-unknown}
fi

[[ ${model} == "${expected_model}" ]] ||
    die "expected ${expected_model}; found ${model}"
[[ ${revision:-unknown} == "${expected_revision}" ]] ||
    die "expected revision ${expected_revision}; found ${revision:-unknown}"
[[ ${machine} == armv6l && ${architecture} == armhf &&
   ${os_codename} == trixie ]] || die "expected ARMv6/armhf Trixie target"
systemctl is-active --quiet ssh || die "OpenSSH service is not active"
if systemctl is-active --quiet oclock; then
    die "oclock.service is active; keep the experimental target stopped"
fi

echo "Target:              ${model} (${machine}/${architecture} ${os_codename})"
echo "Remote hostname:     oclock"
echo "Allowed source:      ${source_ip}"
echo "SSH key fingerprint: SHA256:8sm7CaLjJIV+q7iuFr1XbKU4JGYbwJ3LxAdHL9H2ci8"
echo
printf 'Type REMOTE to install and enroll remote access: ' >&2
read -r confirmation
[[ ${confirmation} == REMOTE ]] || die "remote-access bootstrap was not confirmed"

sudo -v

if ! command -v curl >/dev/null 2>&1; then
    sudo apt-get update
    sudo apt-get install -y ca-certificates curl
fi

install -d -m 0700 "${HOME}/.ssh"
touch "${HOME}/.ssh/authorized_keys"
chmod 0600 "${HOME}/.ssh/authorized_keys"
if ! grep -Fqx "${authorized_key}" "${HOME}/.ssh/authorized_keys"; then
    printf '%s\n' "${authorized_key}" >>"${HOME}/.ssh/authorized_keys"
fi

temporary_dir=$(mktemp -d /tmp/oclock-tailscale-bootstrap-XXXXXXXX)
cleanup()
{
    rm -rf -- "${temporary_dir}"
}
trap cleanup EXIT

curl -fsSLo "${temporary_dir}/tailscale-archive-keyring.gpg" \
    https://pkgs.tailscale.com/stable/raspbian/trixie.noarmor.gpg
curl -fsSLo "${temporary_dir}/tailscale.list" \
    https://pkgs.tailscale.com/stable/raspbian/trixie.tailscale-keyring.list
[[ -s ${temporary_dir}/tailscale-archive-keyring.gpg &&
   -s ${temporary_dir}/tailscale.list ]] || die "downloaded repository files are empty"

sudo install -d -m 0755 /usr/share/keyrings
sudo install -m 0644 "${temporary_dir}/tailscale-archive-keyring.gpg" \
    /usr/share/keyrings/tailscale-archive-keyring.gpg
sudo install -m 0644 "${temporary_dir}/tailscale.list" \
    /etc/apt/sources.list.d/tailscale.list
sudo apt-get update
sudo apt-get install -y tailscale
sudo systemctl enable --now tailscaled

echo
echo "Tailscale will print a login URL. Open it and approve this oclock node."
sudo tailscale up --hostname=oclock --accept-dns=false

echo
echo "Remote-access bootstrap complete."
echo "Tailscale IPv4: $(tailscale ip -4)"
echo "Use 'tailscale status' on the Macmini to discover the final endpoint."
if sudo -n true 2>/dev/null; then
    echo "Noninteractive sudo: available"
else
    echo "Noninteractive sudo: unavailable; privileged gates will need an operator"
fi
tailscale status
