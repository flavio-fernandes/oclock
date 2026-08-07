# Status API

The clock reports itself three ways, all built from one snapshot taken by
`gatherStatusSnapshot()` in [`src/statusReportGather.cpp`](../src/statusReportGather.cpp):

| Surface | Format | When |
| --- | --- | --- |
| `GET /status` | `text/plain` | On request |
| `GET /status.json` | `application/json` | On request |
| MQTT `/officeClock/status` | `application/json`, same document | Every 5m13s |

One gather, two renderers, so the three cannot disagree about what the clock is
doing. The renderers live in [`src/statusReport.cpp`](../src/statusReport.cpp), which
deliberately depends on nothing from the running application — that is what lets
[`tests/status_tests.cpp`](../tests/status_tests.cpp) pin both output formats without
starting a thread.

## `GET /status`

The original human-readable page. **Its existing lines are a compatibility
contract.** Outside consumers grep them, so `tests/status_tests.cpp` asserts the
whole page against an exact expected buffer: a change to any line already in it
is an API change, not a refactor.

Three fields were added to it:

```
display_mode: clock
display_dimmed: no          <- is the matrix currently dimmed
led_strip_mode: off

cpu_load: 0.14 0.09 0.05    <- /proc/loadavg, 1/5/15 minute averages
mem_free_kb: 123456         <- /proc/meminfo MemFree
mem_available_kb: 234567    <-               MemAvailable
mem_total_kb: 493832        <-               MemTotal
```

When `/proc` cannot be read the value is the literal `unavailable`, never a
zero. A zero free-memory reading and an unreadable `/proc/meminfo` are very
different situations and must not look alike.

`display_dimmed` is the display thread's actual state, not a threshold applied
to `light_sensor` after the fact. The dimming decision has hysteresis
(`darkRoomThresholdLowWaterMark` / `HighWaterMark`, see
[wiringpi-phase6-dimming-recalibration.md](wiringpi-phase6-dimming-recalibration.md)),
so a reading between the two water marks does not by itself say whether the
matrix is dim. Only the display thread knows, and this is what it knows.

## `GET /status.json`

The same data, machine-readable, at schema `"version": 1`.

```json
{
  "version": 1,
  "system": {
    "cpu_load": {"1min": 0.14, "5min": 0.09, "15min": 0.05},
    "mem_free_kb": 123456,
    "mem_available_kb": 234567,
    "mem_total_kb": 493832
  },
  "motion": {
    "detected": false,
    "last_change_hour": 0,
    "last_change_min": 0,
    "last_change_sec": 1
  },
  "light_sensor": 512,
  "display": {"mode": "message", "dimmed": false},
  "led_strip": {"mode": "rainbow"},
  "mqtt": {
    "broker_connected": false,
    "broker_ip": "127.0.0.1",
    "broker_port": 1883,
    "keep_alive_secs": 182,
    "last_loop_rc": 4,
    "last_loop_rc_str": "The client is not currently connected.",
    "connect_attempts": 1,
    "connects": 0,
    "disconnects": 0,
    "publishes": 1,
    "published_motions": 0,
    "publishes_dropped": 1,
    "publish_callbacks": 0,
    "messages": 0,
    "ticks": 4
  },
  "dictionary": {
    "size": 0, "ticks": 0, "adds": 0, "removes": 0, "expires": 0,
    "entries": {}
  },
  "handlers": {"hits": {"/status (get)": 1}}
}
```

### Compatibility rules

The point of the `version` field is that it should rarely change. For that to
work, readers have to hold up their end:

- **Ignore unknown keys.** New fields are added without a version bump. A reader
  that rejects a document because it contains a key it has not seen will break
  on the next release, and that is the reader's bug.
- **Tolerate `null`.** `cpu_load` is `null` as a whole when `/proc/loadavg`
  cannot be read; each `mem_*_kb` is `null` when `/proc/meminfo` cannot be. Any
  field may become nullable this way.
- **Do not depend on key order** or on the whitespace. The document is rendered
  with newlines and indentation because a human curls it; that is not a promise.
- `version` increments only if an existing key changes meaning or disappears.

Correspondingly, on the emitting side: fields may be added freely, but renaming
or repurposing one is a version bump, and `tests/compatibility.sh` pins the
current names so that has to be a deliberate act.

### Escaping

Dictionary keys and values reach this document verbatim from HTTP POSTs, so
every string is escaped by `jsonEscape()`. Bytes below `0x20` become `\u00XX`,
NUL included — which is what makes the document safe to hand to
`mosquitto_publish()`, an API that measures its payload with `strlen()`.
`tests/status_tests.cpp` asserts that property directly rather than assuming it,
and `tests/smoke.sh` POSTs a dictionary value full of quotes, backslashes and
braces and checks what comes back out.

## MQTT `/officeClock/status`

Published by `MqttClient::doPeriodicReport()` on the existing 5m13s tick, the
same one that publishes `/officeClock/light`, with the identical payload
`/status.json` returns.

**Not retained**, deliberately, and for the same reason the light value is not:
a status snapshot is only true at the moment it was taken. A retained copy
handed to a subscriber that connects hours after the clock stopped would read as
current. A subscriber that wants the status *now* can ask the web server for it.

The other MQTT topics are unchanged: `/officeClock/light`,
`/officeClock/display_intensity`, `/officeClock/display_mode`,
`/officeClock/motion` and `/officeClock/last_motion`. The status document
duplicates several of those values; that is intended, so a single message is
enough to know the whole state.

