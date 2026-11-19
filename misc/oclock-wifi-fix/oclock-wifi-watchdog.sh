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
# Min seconds between two hard recoveries. Was 600. On 2026-11-17 and 2026-11-18 the
# soft path failed every time (5/5) and only a driver reload brought wifi back -- the
# 2nd and 3rd reload respectively -- so the 10 min wait between reloads is what made
# those outages last 15 and 25 min. At 240 the cooldown no longer binds: after a hard
# recovery the fail count restarts, so reloads repeat every FAIL_HARD probes (~4 min).
COOLDOWN=240
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

# One line describing the radio: the AP it is associated with (if any) and every AP
# NetworkManager can currently see. This is what tells "the APs vanished / signal
# collapsed" (RF interference) apart from "APs visible and strong but refusing us"
# (wedge or AP side). Read-only and cheap: uses NetworkManager's cached scan results
# and never triggers a scan of its own.
wifi_state() {
    local link aps
    link=$(iw dev "$IFACE" link 2>/dev/null \
        | awk '/^Connected to/ {printf "%s ", $3} /signal:/ {printf "%sdBm", $2}')
    aps=$(nmcli -t -f BSSID,CHAN,SIGNAL device wifi list --rescan no ifname "$IFACE" 2>/dev/null \
        | sed 's/\\:/-/g' | tr '\n' ' ')
    printf 'link=[%s] seen=[%s]' "${link:-none}" "${aps% }"
}

soft_recover() {
    log "SOFT RECOVERY: no gateway for $1s, re-associating $IFACE"
    nmcli device disconnect "$IFACE" >/dev/null 2>&1
    sleep 2
    nmcli connection up "$CONN" >/dev/null 2>&1
    local rc=$?
    log "SOFT RECOVERY: nmcli connection up returned $rc"
}

hard_recover() {
    log "HARD RECOVERY: no gateway for $1s, reloading $MODULE; $(wifi_state)"
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

fails=0          # failed probes since the outage began or since the last hard recovery
armed=0
soft_done=0      # at most one soft recovery per outage
last_hard=0
down_since=0     # epoch of the first failed probe of the current outage; 0 = link up
hard_count=0     # hard recoveries during the current outage

while true; do
    if probe; then
        if [ "$armed" = "0" ]; then
            armed=1
            log "armed: gateway reachable, watchdog is now active"
        fi
        if [ "$down_since" != "0" ]; then
            # Measured from the first failed probe, not from $fails: a hard recovery
            # resets $fails, which made this line report "after 0s" for outages that
            # had lasted 15-25 min.
            log "RECOVERED: gateway reachable again after $(( $(date +%s) - down_since ))s," \
                "$hard_count hard recoveries; $(wifi_state)"
        fi
        fails=0
        soft_done=0
        down_since=0
        hard_count=0
    else
        fails=$(( fails + 1 ))
        if [ "$down_since" = "0" ]; then
            down_since=$(date +%s)
            log "gateway unreachable (probe 1); $(wifi_state)"
        fi

        if [ "$armed" = "1" ]; then
            now=$(date +%s)
            if [ "$fails" -ge "$FAIL_HARD" ] && [ $(( now - last_hard )) -ge "$COOLDOWN" ]; then
                hard_recover $(( now - down_since ))
                last_hard=$now
                fails=0
                hard_count=$(( hard_count + 1 ))
                # soft_done is deliberately NOT reset. hard_recover already finishes
                # with `nmcli connection up`, and a second soft pass between reloads
                # never helped (0 of 3 on 2026-11-17/18) -- it only delays the next
                # reload.
            elif [ "$fails" -ge "$FAIL_SOFT" ] && [ "$soft_done" = "0" ]; then
                soft_recover $(( now - down_since ))
                soft_done=1
            fi
        fi
    fi
    sleep "$PROBE_INTERVAL"
done
