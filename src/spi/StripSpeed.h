#ifndef OCLOCK_SPI_STRIP_SPEED_H
#define OCLOCK_SPI_STRIP_SPEED_H

#include <cstdint>

// Production remains at the previously reviewed 1 MHz setting. A separately
// named Phase 5 helper overrides this at compile time to test spi-gpio's
// undelayed path without changing the application binary.
#ifndef OCLOCK_STRIP_SPEED_HZ
#define OCLOCK_STRIP_SPEED_HZ 1000000U
#endif

namespace oclockSpi {

const std::uint32_t stripSpeedHz = OCLOCK_STRIP_SPEED_HZ;

} // namespace oclockSpi

#endif
