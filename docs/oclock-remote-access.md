# Office Clock remote maintenance access

## Decision

The experimental Zero W uses ordinary OpenSSH over Tailscale for continued
migration work. This avoids repeated command/result copying while keeping SSH
off the public internet. Tailscale is a maintenance transport, not an Office
Clock application or runtime dependency.

The Macmini is already enrolled in the user's tailnet. A dedicated Ed25519 key
was created locally only for the clock:

```text
SHA256:8sm7CaLjJIV+q7iuFr1XbKU4JGYbwJ3LxAdHL9H2ci8
```

The bootstrap installs that public key for `pi` with a `from=` restriction to
the Macmini's Tailscale IPv4 and disables agent, port, X11, and user-rc
forwarding. The private key remains in
`/home/flaviof/.ssh/oclock_codex_ed25519` on the Macmini and is never committed.

This uses existing OpenSSH rather than enabling Tailscale SSH, so access does
not depend on separate tailnet SSH rules. Tailnet network policy must still
permit the Macmini to reach the clock on TCP port 22.

## One-time target bootstrap

From an existing console or LAN SSH session on the Zero W, fetch the expected
PR commit and extract the script without switching the target checkout:

```sh
cd ~/oclock-phase3
git fetch origin agent/plan-wiringpi-migration

expected_commit=<commit supplied for bootstrap>
test "$(git rev-parse FETCH_HEAD)" = "${expected_commit}" || exit 1

bootstrap=/tmp/bootstrapOclockTailscale.sh
git show FETCH_HEAD:misc/bootstrapOclockTailscale.sh >"${bootstrap}"
chmod 0755 "${bootstrap}"
macmini_ts_ip=<Macmini address reported by tailscale ip -4>
"${bootstrap}" --source-ip "${macmini_ts_ip}"
```

Type `REMOTE`, then open the URL printed by `tailscale up` and approve the new
`oclock` node. The script follows Tailscale's official Raspbian Trixie package
instructions: it installs the repository signing key and source list, installs
the `tailscale` package, starts `tailscaled`, and joins with
`--accept-dns=false`. It does not enable exit-node or subnet-router behavior.

It also requires the experimental `oclock.service` to remain inactive and
does not change Wi-Fi, the default route, target DNS, sshd configuration, boot
files, GPIO, SPI, or application files.

The package commands come from Tailscale's official
[Raspbian Trixie stable instructions](https://pkgs.tailscale.com/stable/) and
the connection model follows its
[OpenSSH over Tailscale guidance](https://tailscale.com/docs/reference/ssh-over-tailscale).

## Macmini connection

After enrollment, use `tailscale status` on the Macmini to obtain the new
clock endpoint, then verify:

```sh
tailscale ping oclock
ssh -i /home/flaviof/.ssh/oclock_codex_ed25519 \
  -o IdentitiesOnly=yes pi@oclock
```

Once verified, add a local SSH alias named `oclock-ts`; do not replace any
user-maintained `Host oclock` entry. Automation should begin each privileged
gate with `sudo -n true`. If it fails, stop and request an operator rather than
placing a password in scripts or repository files.

## Rollback

Remote maintenance can be disabled without affecting ordinary Wi-Fi or the
clock application:

```sh
sudo tailscale down
sudo systemctl disable --now tailscaled
```

Remove the single `codex-office-clock-2026-08-02` line from
`/home/pi/.ssh/authorized_keys` to revoke the dedicated key. Package and
repository removal are optional cleanup steps and should be performed only
from a console or known-working LAN session.

Deleting the `oclock` machine from the Tailscale admin console independently
revokes its tailnet identity. Deleting
`/home/flaviof/.ssh/oclock_codex_ed25519` and its `.pub` file revokes the local
credential; rotate both sides together if either is suspected compromised.
