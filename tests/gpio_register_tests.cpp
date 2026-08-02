#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "gpio/bcm2835GpioRegisters.h"

static void testSupportedOffsets() {
  assert(Bcm2835GpioRegisters::supportsOffset(0));
  assert(Bcm2835GpioRegisters::supportsOffset(31));
  assert(!Bcm2835GpioRegisters::supportsOffset(-1));
  assert(!Bcm2835GpioRegisters::supportsOffset(32));
}

static void testValueRegisters() {
  std::uint32_t words[Bcm2835GpioRegisters::registerMapBytes /
                      sizeof(std::uint32_t)] = {};
  Bcm2835GpioRegisters registers(words);

  registers.write(5, GpioValue::high);
  assert(words[7] == (std::uint32_t(1) << 5));

  registers.write(27, GpioValue::low);
  assert(words[10] == (std::uint32_t(1) << 27));

  words[13] = std::uint32_t(1) << 22;
  assert(registers.read(22) == GpioValue::high);
  assert(registers.read(21) == GpioValue::low);
}

static void testInvalidOffsetRejected() {
  std::uint32_t words[Bcm2835GpioRegisters::registerMapBytes /
                      sizeof(std::uint32_t)] = {};
  Bcm2835GpioRegisters registers(words);
  bool rejected = false;
  try {
    registers.write(32, GpioValue::high);
  } catch (const std::out_of_range &) {
    rejected = true;
  }
  assert(rejected);
}

int main() {
  testSupportedOffsets();
  testValueRegisters();
  testInvalidOffsetRejected();
  std::cout << "BCM2835 GPIO register tests passed\n";
  return 0;
}
