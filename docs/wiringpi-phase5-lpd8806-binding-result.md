# Phase 5 LPD8806 spidev binding result

## Decision

The exact Raspberry Pi Zero W passed the runtime-only LPD8806 `spidev`
binding gate on 2026-08-02. The strip child was discovered from its Device
Tree path, bound explicitly, exposed one character device, and then returned
to its original unbound state. The MCP3002 remained bound to `mcp320x`, the
service remained inactive, and no device was opened or transferred through.

This result authorizes implementation and hardware-free testing of the
project-owned SPI output transport and LPD8806 frame assembly. It does not
authorize a hardware transfer or application candidate yet.

## Evidence

| Item | Value |
| --- | --- |
| Source commit | `0c191e8050436918aed6622746732774547cd3cf` |
| Capture | `oclock-phase5-lpd-bind-20260802T152824Z-7zjktxAZ.tar.gz` |
| Capture SHA-256 | `2bfdaeb9f0e0ae6a119e12925671b57da296f084e7d67c2eb3e95e5fbfef0f5a` |
| Strip child during capture | `spi4.0`, discovered as `/oclock-strip-spi/lpd8806@0` |
| Strip interface during capture | `/dev/spidev4.0`, character device, mode `0660`, owner `root:spi` |
| ADC child during capture | `spi3.0`, discovered as `/oclock-adc-spi/mcp3002@0`, driver `mcp320x` |
| Collector result | 8 OK, 0 failures |

The adjacent checksum verified before extraction, and the archive contained
no absolute or parent-traversal paths. Runtime bus and device numbers remain
observations from this boot, not stable application configuration. Raw
evidence remains outside Git.

## Accepted behavior

Before binding, the strip child had no driver or override and no character
device. The guarded helper then:

1. loaded `spidev`;
2. wrote `spidev` only to the discovered strip child's `driver_override`;
3. bound that child and waited for udev;
4. verified `/dev/spidev4.0` without opening it.

The collector confirmed kernel ownership of GPIO20 as `sck` and GPIO21 as
`mosi`. The ADC stayed on its native driver. Its metadata-only run did not
open the spidev node, issue an ioctl, transfer a byte, read IIO, request GPIO,
bind a driver, change the service, or edit boot state.

The operator then exercised explicit rollback. `unbind --confirm UNBIND`
removed the character device and cleared the override; final status again
showed the strip unbound, the ADC on `mcp320x`, and `oclock.service` inactive.
The harmless `spidev` module was deliberately left loaded.

## Next gate

Add the SPI output boundary and deterministic fake, convert the LPD8806 to
assemble one 720-byte GRB payload followed by eight zero latch bytes, and run
hardware-free tests plus a native ARM build. Only after those pass should a
separate verifier bind, open, and send the first measured frame.
