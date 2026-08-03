#ifndef OCLOCK_GPIO_GPIO_H
#define OCLOCK_GPIO_GPIO_H

#include <cstddef>
#include <cstdint>
#include <memory>

enum class GpioValue {
  low,
  high
};

class GpioBurstPins;

class Gpio {
public:
  virtual ~Gpio() {}

  virtual bool initialize() = 0;
  virtual void configureInput(int bcmGpio) = 0;
  virtual void configureOutput(int bcmGpio, GpioValue initialValue) = 0;
  virtual GpioValue read(int bcmGpio) = 0;
  virtual void write(int bcmGpio, GpioValue value) = 0;
  virtual void delayMilliseconds(unsigned int duration) = 0;

  // Optionally hand out a pre-resolved value path for lines this object has
  // already configured as outputs. Only the HT1632 asks for this, and only
  // because its protocol has no kernel subsystem to delegate to.
  //
  // Returning false is always correct and is the default: callers must keep a
  // working write() path. A backend that returns true still owns the lines.
  virtual bool acquireBurst(const int *bcmGpios, std::size_t count,
                            GpioBurstPins &pins, std::uint32_t *masks) {
    (void)bcmGpios;
    (void)count;
    (void)pins;
    (void)masks;
    return false;
  }
};

// The build selects exactly one implementation of this factory. Plain
// hardware builds provide the selected WiringPi or libgpiod backend; sandbox
// and test builds provide the fake backend.
std::unique_ptr<Gpio> createGpio();

#endif
