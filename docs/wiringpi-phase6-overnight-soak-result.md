# Phase 6 overnight soak result

## Result

**The soak blocker is closed.** The modern stack drove the real Office Clock
unattended for **8 hours 53 minutes** with zero restarts, zero warnings, and no
thermal or voltage events.

This was not a scheduled gate. The operator asked to leave the clock running
overnight after the Phase 5 trial, which turned the longest previous
observation of five minutes into a genuine soak.

| Item | Value |
| --- | --- |
| Board | Raspberry Pi Zero W Rev 1.1 (`9000c1`) |
| Kernel | 6.18.39+rpt-rpi-v6 (Raspbian 1:6.18.39-1+rpt1) |
| Binary SHA-256 | `3eacccb0f71fd1f08c97c87f8146018721ed6f65e9efa494676bcb965c8881c9` |
| Trial commit | `9677a0e7a0171e091f9553b2eb70d51e40f14812` |
| Service start | 2026-08-02 21:34:21 EDT |
| Snapshot taken | 2026-08-03 06:27:56 EDT |
| Continuous runtime | **8 h 53 min** |
| System uptime | 19 h 10 min |

## What held

| Measurement | Value | Reading |
| --- | --- | --- |
| `NRestarts` | **0** | The unit never failed or was restarted |
| Journal warnings | **none** | No application warning or error in 8 h 53 min |
| Kernel `spi`/`gpio` warnings | **none since boot** | No driver complaint from either bus |
| CPU | 4.7% | Matches the 4.72% trial mean exactly |
| Resident memory | 5316 KB (5.2 MB) | Flat; no growth over the window |
| Virtual memory | 103728 KB, 12 threads | Thread-stack reservation, not occupancy |
| Load average | 0.15 / 0.19 / 0.16 | Idle-class load |
| Temperature | 37.9 °C | Passively cooled, no headroom concern |
| `get_throttled` | `0x0` | No undervoltage or thermal capping at any point |
| HTTP `/status` | 200 in **7.5 ms** | Compare 96 ms under the deliberate stress test |
| MQTT | connected, 1 attempt, **0 disconnects**, 67570 ticks | Onboard Wi-Fi held all night on its first connect |

The MQTT counters are the strongest Wi-Fi evidence in the whole migration.
`mqttConnectAttempts: 1` with `mqttDisconnects: 0` across nearly nine hours
means the onboard Wi-Fi never dropped once. One publish was dropped
(`mqttPublishesDropped: 1`) against 165 published, which is ordinary.

Both ADC channels read 1023 at the morning snapshot, consistent with a lit
room, and the light sensor reported 1022.

## The stress recipe still applies

This soak carried ordinary load, not the deliberate stress profile. The
stressed comparison remains the Phase 5 run-1 measurement (18.29% mean, 75.68%
peak, all observations still passing). Reproduce it with:

```sh
curl -X POST -d 'ledStripMode=4&timeout=7200' http://127.0.0.1/ledStrip
```

paired with [`stickManAnimation.sh`](../misc/stickManAnimation.sh).

The two together bracket the range: the clock is comfortable at rest and still
correct under pressure.

## What this soak does not prove

Recorded so the write-up does not overclaim.

1. **It does not prove the clock dimmed overnight.** The room was dark for most
   of the window and the retuned 460 threshold should have engaged, but there
   is no record either way. `oclock.service` sets `StandardOutput=null`, so the
   application's own light-sensor trace is discarded, and nothing was polling
   `/status`. The dimming evidence remains the Phase 5 run-3 trace, which is
   direct and operator-confirmed. See
   [the observability gap](#the-observability-gap-this-exposed) below.

2. **It does not prove the clock survives a power cut.** The service was
   started by hand and is still `disabled`. Nothing has been rebooted. That is
   the remaining Phase 6 blocker, tracked in
   [the binding persistence gate](wiringpi-phase6-binding-persistence.md).

3. **It is a single soak on a single unit.** Nine hours is enough to rule out
   fast leaks, descriptor exhaustion, and Wi-Fi instability. It is not enough
   to speak to weeks.

## The observability gap this exposed

`StandardOutput=null` was inherited from the original WiringPi-era unit, where
it made sense: the application printed continuously and journald on an SD card
was a wear concern.

The cost showed up here. A nine-hour window that should have contained several
dimming transitions produced no evidence of them, and the only way to observe
the clock is to poll `/status` from outside. Every gate in this migration had
to stand up its own polling harness to see what the application already knew.

This is worth changing as part of Phase 6 rather than carrying forward, and it
is a good beat for the write-up: the migration did not create the blind spot,
it just made it expensive enough to notice.

## Swap

53 MB of swap was in use at the snapshot against 217 MB of 426 MB resident.
With the clock itself at 5.2 MB RSS this is other system tenants aging out
rather than pressure from the application. Recorded for completeness; no action
taken.
