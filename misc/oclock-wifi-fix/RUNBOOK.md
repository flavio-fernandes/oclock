# oclock wifi fix — runbook

**This is the single source of truth for what gets changed on oclock and how to undo it.**
Everything is in this one directory. The evidence and investigation live elsewhere
(`../uptime-kuma-proxmox-handoff.md` sections 27–30) and you do not need to read any of it to
implement or revert.

---

## What this fixes

| Symptom | Fix | Evidence |
|---|---|---|
| Median RTT 100–127 ms when the network is idle, p99 over 1 s | `wifi.powersave = 2` | handoff 28.8, 28.10 |
| Network stack wedges dead, board still running, needs a manual power cycle (observed: 6 h 37 m) | watchdog reloads the wifi driver after 4 min | handoff 30 |

The watchdog does **not** diagnose the wedge. It makes recovery cheap — about 4 minutes instead
of however long until someone notices. poke stays in place as the outer backstop.

---

## Exactly what gets installed

Three files. Nothing else. No packages, no modified existing files.

```
/opt/oclock-wifi-fix/oclock-wifi-watchdog.sh          the watchdog
/opt/oclock-wifi-fix/uninstall.sh                     the revert, kept on the box
/opt/oclock-wifi-fix/RUNBOOK.md                       this file, kept on the box
/etc/NetworkManager/conf.d/10-wifi-powersave.conf     power save off
/etc/systemd/system/oclock-wifi-watchdog.service      the unit
```

Both changes need the writable window, so implementing costs **two reboots**. So does reverting.

---

## IMPLEMENT

### 1. Preflight — do not skip

A half-configured package or a failed unit baked into a read-only image is painful to undo.
Checked on 2026-08-06 the hard way.

```bash
ssh oclock 'sudo dpkg --audit; echo "--- failed units ---"; systemctl --failed; echo "--- overlay ---"; sudo raspi-config nonint get_overlay_now'
```

Expected: `dpkg --audit` silent, no failed units, `get_overlay_now` = `0` (overlay active).

### 2. Copy the files over

```bash
scp -r /Users/gute/projects/update-uptimekuma/oclock-wifi-fix oclock:~/
```

### 3. Open the writable window (reboot 1)

```bash
ssh oclock 'sudo raspi-config nonint disable_overlayfs && sudo reboot'
```

Wait ~90 s, then confirm it really is writable — `get_overlay_now` must be **1**:

```bash
ssh oclock 'sudo raspi-config nonint get_overlay_now; findmnt -no SOURCE,FSTYPE /'
```

Expect `1` and `/dev/mmcblk0p2 ext4`. If it still says `0`, stop; the next step will refuse anyway.

### 4. Install

```bash
ssh oclock 'sudo ~/oclock-wifi-fix/install.sh'
```

It refuses to run if the filesystem is read-only, and verifies every file afterwards.

### 5. Close the writable window (reboot 2)

```bash
ssh oclock 'sudo raspi-config nonint enable_overlayfs && sudo reboot'
```

---

## VERIFY

Wait ~90 s after reboot 2, then:

```bash
ssh oclock 'sudo raspi-config nonint get_overlay_now; echo "--- power save ---"; sudo dmesg | grep power_mgmt | tail -2; nmcli -f 802-11-wireless.powersave connection show netplan-wlan0-ffiot; echo "--- watchdog ---"; systemctl is-active oclock-wifi-watchdog.service; systemctl is-enabled oclock-wifi-watchdog.service; echo "--- clock still fine ---"; systemctl is-active oclock.service mqtt2cmd.service'
```

All four must be true:

| Check | Expected |
|---|---|
| `get_overlay_now` | `0` — read-only protection is back on |
| `dmesg \| grep power_mgmt` | last line is **`power save disabled`** |
| `oclock-wifi-watchdog.service` | `active` and `enabled` |
| `oclock.service`, `mqtt2cmd.service` | `active` |

**If `dmesg` still says `power save enabled`, the fix is not in effect** — the drop-in did not
apply. Do not accept a green-looking result without this line.

Latency should also drop. From CT 140:

```bash
ssh uptimekuma 'ping -c20 -i0.5 192.168.10.236 | tail -2'
```

Expect a median around 6 ms rather than ~100 ms.

---

## REVERT

Same shape, two reboots. `uninstall.sh` is kept on the box, so this works even without this repo.

```bash
ssh oclock 'sudo raspi-config nonint disable_overlayfs && sudo reboot'
# wait ~90 s
ssh oclock 'sudo /opt/oclock-wifi-fix/uninstall.sh --dry-run'   # preview, changes nothing
ssh oclock 'sudo /opt/oclock-wifi-fix/uninstall.sh'             # remove, verifies afterwards
ssh oclock 'sudo raspi-config nonint enable_overlayfs && sudo reboot'
```

Leaves the machine exactly as it was: power save returns to the NetworkManager default
(enabled), and no watchdog. Confirm with:

```bash
ssh oclock 'ls /opt/oclock-wifi-fix /etc/NetworkManager/conf.d/10-wifi-powersave.conf /etc/systemd/system/oclock-wifi-watchdog.service 2>&1; sudo dmesg | grep power_mgmt | tail -1'
```

Expect three "No such file or directory" and `power save enabled`.

### Reverting just one of the two

```bash
# power save only -- keep the watchdog
ssh oclock 'sudo rm /etc/NetworkManager/conf.d/10-wifi-powersave.conf'   # needs writable window

# watchdog only -- keep power save off
ssh oclock 'sudo systemctl disable --now oclock-wifi-watchdog.service'   # works read-only,
                                                                         # but reverts on reboot
```

To make the watchdog stay disabled across reboots you need the writable window; disabling it at
runtime is undone by the next boot, like every other runtime change on this box.

---

## How the watchdog behaves

Probes the gateway (`192.168.10.1`) every 30 s.

| Elapsed with no gateway | Action |
|---|---|
| 0–2 min | nothing — observed brief outages are 1–22 s and self-recover |
| 2 min | **soft**: `nmcli device disconnect` + `connection up` |
| 4 min | **hard**: `ip link down`, `modprobe -r brcmfmac`, `modprobe brcmfmac`, reconnect |
| after a hard recovery | 10 min cooldown before another one |

Safety properties, on purpose:

- **Never reboots.** poke handles anything the watchdog cannot.
- **Never writes to disk.** All output goes to journald (capped at `RuntimeMaxUse=16M`).
- **Will not act until it has seen the gateway work once**, so a slow boot cannot trigger a
  driver reload. Look for the `armed:` line in the log.
- Resource-capped at `CPUQuota=5%`, `MemoryMax=24M` so it cannot crowd the clock application.
- Reloading the driver re-applies `wifi.powersave = 2` automatically, because the setting is a
  NetworkManager `conf.d` default rather than a per-connection property.

### Reading its log

```bash
ssh oclock 'journalctl -t oclock-wifi-watchdog --no-pager | tail -40'
```

Quiet is correct — it only logs state changes and actions, never routine probes. A healthy boot
logs `started:` then `armed:` and nothing more.

### Did it ever save us?

```bash
ssh oclock 'journalctl -t oclock-wifi-watchdog --no-pager | grep -E "SOFT|HARD|RECOVERED"'
```

`HARD RECOVERY` followed by `RECOVERED` is the watchdog doing exactly its job. `ERROR modprobe
... FAILED` means it could not bring wifi back and poke will have power cycled — that is the one
line worth alerting on.

---

## Testing it without waiting for a real wedge

Simulates the failure by taking the interface down. **Only do this with physical access or via
poke**, since it deliberately makes the box unreachable for a few minutes.

```bash
ssh oclock 'sudo systemd-run --on-active=5 --unit=wedgetest ip link set wlan0 down'
```

The watchdog should log `SOFT RECOVERY` at ~2 min, `HARD RECOVERY` at ~4 min, and the box should
be back shortly after. Note a soft recovery alone usually fixes a downed interface — a genuine
firmware wedge is what needs the hard path.

---

## Deployment log

### 2026-08-10 07:48 — installed

Applied after oclock wedged twice in one morning (6 h 37 m overnight, then again 16.8 min after
the power cycle). Preflight was clean both times: `dpkg --audit` silent, 0 failed units, both
clock services active, 25 GB free.

Verified after reboot 2:

```
get_overlay_now = 0                               read-only protection restored
[95.645394]  brcmfmac: power save enabled         <- NetworkManager's default, at association
[98.587963]  brcmfmac: power save disabled        <- our drop-in, 3 s later. THIS is the proof.
oclock-wifi-watchdog.service                      active, enabled
oclock.service, mqtt2cmd.service                  active, active
all 5 installed files                             present after the overlay bake
ping from CT 140                                  min 5.4 / avg 7.6 / max 24.1 ms, 0% loss
```

**Do not be alarmed that `nmcli` reports `802-11-wireless.powersave: 0 (default)`.** That is
correct and expected: the value comes from the `conf.d` drop-in as a *default*, so the
per-connection property legitimately stays unset. The authoritative check is the `dmesg` line,
which shows NetworkManager applying the drop-in three seconds after association.

### Gotcha found during install

Files staged in `~/` **before** disabling the overlay are wiped by the reboot, because `~/` is
in the tmpfs upper layer at that point. Copy the payload *after* the filesystem is writable, not
before. The runbook order above (copy at step 2, then reboot at step 3) hits this — in practice
just re-copy after reboot 1. Harmless, but it will look like the files vanished.

### 2026-08-11 08:11 — 22 h soak review

Clean. `uptime -s` unchanged since 2026-08-10 09:48:38, no reboots. Overnight p50 **5.94-6.18 ms**
with ICMP 100% and 0% TCP failures across eight idle hours — the power-save fix is persisting
correctly across reboots. Ten brief outages (5-34 s) on Aug 10 afternoon, then 14.5 h unbroken.

**Watchdog: active, 0 restarts, 0 recoveries — still unproven.** No wedge has occurred since
install, so the `modprobe` recovery path has never actually run on this hardware.

**Known defect found:** the watchdog's log lines do not survive. `journalctl -t
oclock-wifi-watchdog` was empty because journald's `RuntimeMaxUse=16M` is nearly full and had
rotated past them; retention was only ~10 hours. Until fixed, check the watchdog log the same day
as any incident, or its evidence is gone. Fix under consideration: append recoveries to a file in
`/tmp` as well as journald.
