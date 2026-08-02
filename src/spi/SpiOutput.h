#ifndef OCLOCK_SPI_SPI_OUTPUT_H
#define OCLOCK_SPI_SPI_OUTPUT_H

#include <cstddef>
#include <cstdint>
#include <memory>

class SpiOutput {
public:
  virtual ~SpiOutput() {}

  virtual bool initialize() = 0;
  virtual void transfer(const std::uint8_t *data, std::size_t length) = 0;
};

// The build selects exactly one factory. Legacy and sandbox builds return an
// empty pointer and keep the LPD8806 GPIO path. The opt-in modern strip build
// returns the Linux spidev transport.
std::unique_ptr<SpiOutput> createStripSpiOutput();

#endif
