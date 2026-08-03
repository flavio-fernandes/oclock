#ifndef OCLOCK_SPI_STRIP_SPEED_H
#define OCLOCK_SPI_STRIP_SPEED_H

#include <cstdint>

// Production runs at 2 MHz. This is not a performance tweak: the kernel's
// spi-gpio driver inserts a rounded-up nanosecond busy-wait on both sides of
// every clock edge whenever the requested half-cycle is at least 500 ns, which
// is exactly 1 MHz or slower. At 1 MHz a 728-byte frame took about 20.5 ms and
// missed the 12 ms tick on 25 of 25 attempts; at 2 MHz the same frame takes
// roughly 3 to 4.4 ms on the undelayed path.
// See docs/wiringpi-phase5-lpd8806-2mhz-result.md and
// docs/wiringpi-phase5-lpd8806-colors-result.md.
//
// Helpers may still override this at compile time to characterize other rates
// without changing the application binary.
#ifndef OCLOCK_STRIP_SPEED_HZ
#define OCLOCK_STRIP_SPEED_HZ 2000000U
#endif

namespace oclockSpi {

const std::uint32_t stripSpeedHz = OCLOCK_STRIP_SPEED_HZ;

} // namespace oclockSpi

#endif
