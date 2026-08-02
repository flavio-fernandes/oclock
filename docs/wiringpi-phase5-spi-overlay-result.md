# Phase 5 SPI overlay offline result

## Decision

The disabled Office Clock overlay passed its exact-board offline gate on
2026-08-02. It compiled with the target's `dtc 1.7.2`, merged into a file copy
of the active Raspberry Pi Zero W Device Tree, and preserved every reviewed
controller, child binding, and BCM offset. The verifier removed both complete
Device Tree blobs before packaging and did not change the live target.

This result authorizes preparation of the separately reversible live-boot
gate. It does not authorize starting `oclock`, binding the strip to `spidev`,
reading the ADC, or deploying a modern candidate.

## Evidence

| Item | Value |
| --- | --- |
| Source commit | `c79c53cd1ee06aced881244cb90594894c5ca025` |
| Capture | `oclock-phase5-spi-dry-run-20260802T143057Z-NbEl7pZe.tar.gz` |
| Capture SHA-256 | `963c6d2cf03bfbad056736306a8561ec13be3756035f27ac775cb4c5408e50d1` |
| Overlay source SHA-256 | `854c697eb70800770b1da4e8aec261412b1e42067c8ea72d3b58e65ce8219086` |
| Compiled overlay SHA-256 | `53b593f4c30a78c8beb446cf506816af29a817d0daf93bb899a9054c7aa61959` |
| Failures | 0 |

The adjacent checksum verified before extraction, and the archive contained
no absolute or parent-traversal path. Raw evidence remains outside Git.

## Accepted merged properties

| Node | Accepted properties |
| --- | --- |
| `/oclock-strip-spi` | `spi-gpio`, no chip select, SCK 20, MOSI 21 |
| `/oclock-strip-spi/lpd8806@0` | `flaviof,oclock-lpd8806`, child 0, 1 MHz maximum |
| `/oclock-adc-spi` | `spi-gpio`, one chip select, SCK 17, MISO 27, MOSI 22, active-low CS 4 |
| `/oclock-adc-spi/mcp3002@0` | `microchip,mcp3002`, child 0, 1 MHz maximum |

The target also reconfirmed that `spi-gpio`, `spidev`, and `mcp320x` are
available, `oclock.service` is inactive, and neither Office Clock overlay node
is currently active.

The warnings emitted while converting `/proc/device-tree` are normal
round-trip schema warnings for the already-running Raspberry Pi tree. The
overlay compilation and `fdtoverlay` merge themselves emitted no errors or
warnings. No full tree was retained in the archive; only the warning text and
the four Office Clock nodes' extracted properties were kept.

## Safety outcome

The run did not:

- apply or install an overlay;
- modify `/boot`;
- load a module or bind a driver;
- create or open a device node;
- read an IIO value or perform an SPI transfer;
- request or drive GPIO;
- start, stop, or restart the service.

The next gate is the guarded [live overlay boot](wiringpi-phase5-spi-live-boot.md).
