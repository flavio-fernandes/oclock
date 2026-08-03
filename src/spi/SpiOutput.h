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

// The hardware build supplies the Linux spidev factory. The sandbox supplies
// an empty factory and keeps hardware-free application tests isolated.
std::unique_ptr<SpiOutput> createStripSpiOutput();

#endif
