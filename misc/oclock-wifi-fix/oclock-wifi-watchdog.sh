#!/bin/bash
#
# oclock wifi watchdog
#
# Recovers oclock from the brcmfmac wedge documented in handoff section 30: the
# board stays alive and the clock application keeps running, but the network stack
# goes silent between one 5-second probe and the next and never comes back. The
# observed instance lasted 6 h 37 m and needed a manual power cycle.
#
# This does NOT diagnose the wedge. It makes recovery cheap: ~4 minutes instead of
# however long until somebody notices.
#
# Deliberately conservative:
#   - never reboots. poke remains the outer backstop for anything this cannot fix.
#   - never writes to disk. The root filesystem is read-only (overlayroot=tmpfs) and
#     all output goes to journald, which is capped at RuntimeMaxUse=16M.
#   - will not act until it has seen the network work at least once, so a slow boot
#     can never trigger a driver reload.
#   - ignores short outages. Observed brief outages are 1-22 s and self-recover;
#     thrashing the radio for those would cause more harm than it prevents.
#
# Install/revert: see RUNBOOK.md in the oclock-wifi-fix directory.

set -u

GATEWAY="${GATEWAY:-192.168.10.1}"
IFACE="${IFACE:-wlan0}"
CONN="${CONN:-netplan-wlan0-ffiot}"
MODULE="${MODULE:-brcmfmac}"

PROBE_INTERVAL=30     # seconds between probes
FAIL_SOFT=4           # 4 x 30 s = 2 min  -> ask NetworkManager to re-associate
FAIL_HARD=8           # 8 x 30 s = 4 min  -> reload the wifi driver and firmware
COOLDOWN=600          # min seconds between two hard recoveries
GRACE=120             # seconds after start before the watchdog may act at all

# Log to journald AND to a file. journald here is capped at RuntimeMaxUse=16M and
# was observed on 2026-08-11 to have rotated away this service's entire history
# after only ~10 hours. For a daemon whose whole purpose is recording rare events,
# that is useless. /tmp is tmpfs so this costs RAM, not SD writes, and it is never
# rotated -- but it does die on reboot, so copy it off before power cycling.
EVENTLOG=/tmp/oclock-watchdog-events.log

log() {
    logger -t oclock-wifi-watchdog -p daemon.notice -- "$*"
    printf '%s  %s\n' "$(date --iso-8601=seconds)" "$*" >> "$EVENTLOG" 2>/dev/null
    echo "$*"
}

probe() {
    ping -n -c1 -W5 "$GATEWAY" >/dev/null 2>&1
}

soft_recover() {
    log "SOFT RECOVERY: no gateway for $((FAIL_SOFT * PROBE_INTERVAL))s, re-associating $IFACE"
    nmcli device disconnect "$IFACE" >/dev/null 2>&1
    sleep 2
    nmcli connection up "$CONN" >/dev/null 2>&1
    local rc=$?
    log "SOFT RECOVERY: nmcli connection up returned $rc"
}

hard_recover() {
    log "HARD RECOVERY: no gateway for $((FAIL_HARD * PROBE_INTERVAL))s, reloading $MODULE"
    ip link set "$IFACE" down 2>/dev/null
    sleep 2

    # brcmfmac_cyw depends on brcmfmac and MUST be removed first. `modprobe -r`
    # unloads what the target depends ON, never what depends on the target, so
    # removing brcmfmac alone always fails with "Module brcmfmac is in use".
    # Confirmed by testing on 2026-08-13 (kernel 6.18): the original version of
    # this function never once unloaded the driver, yet reported success -- see
    # the note below about verification.
    modprobe -r "${MODULE}_cyw" 2>/dev/null
    modprobe -r "$MODULE" 2>/dev/null

    # VERIFY, do not trust. `modprobe <module>` returns 0 when the module is
    # already loaded, so a failed unload followed by a "successful" load looks
    # exactly like a working recovery. That is how the original version logged
    # "reloaded OK, firmware re-downloaded" while doing nothing at all.
    if lsmod | grep -q "^${MODULE} "; then
        log "HARD RECOVERY: ERROR $MODULE STILL LOADED after unload -- this recovery did NOTHING."
        log "HARD RECOVERY: held by: $(lsmod | awk -v m="^${MODULE} " '$0 ~ m {print $4}')"
        log "HARD RECOVERY: falling back to a reconnect; poke will power cycle if that is not enough."
        nmcli connection up "$CONN" >/dev/null 2>&1
        return 1
    fi
    log "HARD RECOVERY: $MODULE unloaded (verified via lsmod)"
    sleep 3

    modprobe "$MODULE" 2>/dev/null
    modprobe "${MODULE}_cyw" 2>/dev/null
    if ! lsmod | grep -q "^${MODULE} "; then
        log "HARD RECOVERY: ERROR $MODULE FAILED TO RELOAD -- wifi will not come back."
        log "HARD RECOVERY: poke should power cycle. Investigate before trusting this host."
        return 1
    fi
    log "HARD RECOVERY: $MODULE reloaded, firmware re-downloaded (verified via lsmod)"

    # NetworkManager autoconnects when the device reappears. Nudge it if it does not.
    # wifi.powersave is re-applied automatically here because it comes from the
    # conf.d drop-in rather than the connection profile -- verified 2026-08-13.
    sleep 10
    nmcli connection up "$CONN" >/dev/null 2>&1
    return 0
}

log "started: gateway=$GATEWAY iface=$IFACE module=$MODULE probe=${PROBE_INTERVAL}s" \
    "soft=$((FAIL_SOFT * PROBE_INTERVAL))s hard=$((FAIL_HARD * PROBE_INTERVAL))s"

sleep "$GRACE"

fails=0
armed=0
soft_done=0
last_hard=0
was_down=0

while true; do
    if probe; then
        if [ "$armed" = "0" ]; then
            armed=1
            log "armed: gateway reachable, watchdog is now active"
        fi
        if [ "$was_down" = "1" ]; then
            log "RECOVERED: gateway reachable again after $((fails * PROBE_INTERVAL))s"
        fi
        fails=0
        soft_done=0
        was_down=0
    else
        fails=$(( fails + 1 ))
        [ "$was_down" = "0" ] && log "gateway unreachable (probe 1)"
        was_down=1

        if [ "$armed" = "1" ]; then
            now=$(date +%s)
            if [ "$fails" -ge "$FAIL_HARD" ] && [ $(( now - last_hard )) -ge "$COOLDOWN" ]; then
                hard_recover
                last_hard=$now
                fails=0
                soft_done=0
            elif [ "$fails" -ge "$FAIL_SOFT" ] && [ "$soft_done" = "0" ]; then
                soft_recover
                soft_done=1
            fi
        fi
    fi
    sleep "$PROBE_INTERVAL"
done
