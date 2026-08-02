#include "bcm2835GpioRegisters.h"

#include <stdexcept>

Bcm2835GpioRegisters::Bcm2835GpioRegisters(
    volatile std::uint32_t *registers)
    : registers_(registers) {
  if (registers_ == nullptr)
    throw std::invalid_argument("BCM2835 GPIO register mapping is null");
}

bool Bcm2835GpioRegisters::supportsOffset(int bcmGpio) {
  return bcmGpio >= 0 && bcmGpio < 32;
}

std::uint32_t Bcm2835GpioRegisters::maskForOffset(int bcmGpio) {
  if (!supportsOffset(bcmGpio))
    throw std::out_of_range("BCM2835 GPIO offset is outside bank 0");
  return std::uint32_t(1) << static_cast<unsigned int>(bcmGpio);
}

GpioValue Bcm2835GpioRegisters::read(int bcmGpio) const {
  const std::uint32_t mask = maskForOffset(bcmGpio);
  // The mapping is device memory on Raspberry Pi OS. The full barriers keep
  // compiler and CPU ordering explicit around a level sample that may follow
  // a software-clock write.
  __sync_synchronize();
  const std::uint32_t levels = registers_[levelRegister0];
  __sync_synchronize();
  return (levels & mask) != 0 ? GpioValue::high : GpioValue::low;
}

void Bcm2835GpioRegisters::write(int bcmGpio, GpioValue value) const {
  const std::uint32_t mask = maskForOffset(bcmGpio);
  registers_[value == GpioValue::high ? setRegister0 : clearRegister0] = mask;
  // Complete this edge before the caller emits the next data or clock edge.
  __sync_synchronize();
}
