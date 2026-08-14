#!/bin/bash
# Remove the oclock wifi fix completely. Run ON oclock, inside the writable window.
# Leaves the machine exactly as it was before install.sh. Full procedure: RUNBOOK.md
set -u
DST=/opt/oclock-wifi-fix
DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

run() { if [ "$DRY" = "1" ]; then echo "  [dry-run] $*"; else eval "$@"; fi; }

echo "=== preflight ==="
if [ "$(id -u)" != "0" ]; then echo "  must run as root (use sudo)"; exit 1; fi
ov=$(raspi-config nonint get_overlay_now 2>/dev/null)
if [ "$DRY" = "0" ] && [ "$ov" != "1" ]; then
    echo "  ABORT: overlay is still active (get_overlay_now=$ov, need 1 = writable)."
    echo "  Run: sudo raspi-config nonint disable_overlayfs && sudo reboot"
    exit 1
fi

echo "=== what is present ==="
systemctl is-enabled oclock-wifi-watchdog.service 2>/dev/null | sed 's/^/  service enabled: /'
for f in "$DST" /etc/NetworkManager/conf.d/10-wifi-powersave.conf \
         /etc/systemd/system/oclock-wifi-watchdog.service; do
    [ -e "$f" ] && echo "  present: $f"
done

echo "=== removing ==="
run "systemctl stop oclock-wifi-watchdog.service 2>/dev/null; true"
run "systemctl disable oclock-wifi-watchdog.service 2>/dev/null; true"
run "rm -f /etc/systemd/system/oclock-wifi-watchdog.service"
run "systemctl daemon-reload"
run "systemctl reset-failed oclock-wifi-watchdog.service 2>/dev/null; true"
run "rm -f /etc/NetworkManager/conf.d/10-wifi-powersave.conf"
run "rm -rf '$DST'"

echo "=== verification ==="
if [ "$DRY" = "1" ]; then echo "  dry run, nothing verified"; exit 0; fi
fail=0
for f in "$DST" /etc/NetworkManager/conf.d/10-wifi-powersave.conf \
         /etc/systemd/system/oclock-wifi-watchdog.service; do
    if [ -e "$f" ]; then echo "  STILL PRESENT: $f"; fail=1; else echo "  gone: $f"; fi
done
[ "$fail" = "0" ] && echo "  RESULT: clean" || echo "  RESULT: INCOMPLETE"
echo
echo "  Power save returns to the NetworkManager default (enabled) on next boot."
echo "  Now: sudo raspi-config nonint enable_overlayfs && sudo reboot"
