#!/bin/bash
# Install the oclock wifi fix. Run ON oclock, inside the writable window.
# Reverse with uninstall.sh. Full procedure: RUNBOOK.md
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
DST=/opt/oclock-wifi-fix

echo "=== preflight ==="
if [ "$(id -u)" != "0" ]; then echo "  must run as root (use sudo)"; exit 1; fi
ov=$(raspi-config nonint get_overlay_now 2>/dev/null)
if [ "$ov" != "1" ]; then
    echo "  ABORT: overlay is still active (get_overlay_now=$ov, need 1 = writable)."
    echo "  Run: sudo raspi-config nonint disable_overlayfs && sudo reboot"
    exit 1
fi
echo "  filesystem is writable (get_overlay_now=1)"
findmnt -no SOURCE,FSTYPE / | sed 's/^/  root: /'

echo "=== installing ==="
install -d -m 0755 "$DST"
install -m 0755 "$SRC/oclock-wifi-watchdog.sh" "$DST/oclock-wifi-watchdog.sh"
install -m 0644 "$SRC/RUNBOOK.md"              "$DST/RUNBOOK.md" 2>/dev/null || true
install -m 0644 "$SRC/uninstall.sh"            "$DST/uninstall.sh"
chmod 0755 "$DST/uninstall.sh"
echo "  $DST/"

install -d -m 0755 /etc/NetworkManager/conf.d
install -m 0644 "$SRC/10-wifi-powersave.conf" /etc/NetworkManager/conf.d/10-wifi-powersave.conf
echo "  /etc/NetworkManager/conf.d/10-wifi-powersave.conf"

install -m 0644 "$SRC/oclock-wifi-watchdog.service" /etc/systemd/system/oclock-wifi-watchdog.service
echo "  /etc/systemd/system/oclock-wifi-watchdog.service"

systemctl daemon-reload
systemctl enable oclock-wifi-watchdog.service
echo "  service enabled (starts on next boot)"

echo "=== verification ==="
for f in "$DST/oclock-wifi-watchdog.sh" /etc/NetworkManager/conf.d/10-wifi-powersave.conf \
         /etc/systemd/system/oclock-wifi-watchdog.service; do
    [ -f "$f" ] && echo "  present: $f" || { echo "  MISSING: $f"; exit 1; }
done
systemctl is-enabled oclock-wifi-watchdog.service | sed 's/^/  service enabled: /'
echo
echo "  RESULT: installed. Now run the pre-overlay checklist, then:"
echo "    sudo raspi-config nonint enable_overlayfs && sudo reboot"
