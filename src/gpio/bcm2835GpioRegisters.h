#ifndef OCLOCK_GPIO_BCM2835_GPIO_REGISTERS_H
#define OCLOCK_GPIO_BCM2835_GPIO_REGISTERS_H

#include <cstddef>
#include <cstdint>

#include "Gpio.h"

// Minimal BCM2835 value-register access. Direction and pull configuration are
// intentionally absent: the common libgpiod backend retains those duties.
class Bcm2835GpioRegisters {
public:
  explicit Bcm2835GpioRegisters(volatile std::uint32_t *registers);

  static bool supportsOffset(int bcmGpio);
  GpioValue read(int bcmGpio) const;
  void write(int bcmGpio, GpioValue value) const;

  // Direct value-register access for the narrow HT1632 burst path. Direction
  // and ownership stay with the libgpiod backend; these are value registers
  // only.
  volatile std::uint32_t *setRegister() const;
  volatile std::uint32_t *clearRegister() const;
  static std::uint32_t maskForOffset(int bcmGpio);

  static const std::size_t registerMapBytes = 4096;

private:
  static const std::size_t setRegister0 = 7;   // GPSET0, byte offset 0x1c
  static const std::size_t clearRegister0 = 10; // GPCLR0, byte offset 0x28
  static const std::size_t levelRegister0 = 13; // GPLEV0, byte offset 0x34

  volatile std::uint32_t *registers_;
};

#endif
