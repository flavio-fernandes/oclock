#ifndef OCLOCK_GPIO_GPIO_H
#define OCLOCK_GPIO_GPIO_H

#include <memory>

enum class GpioValue {
  low,
  high
};

class Gpio {
public:
  virtual ~Gpio() {}

  virtual bool initialize() = 0;
  virtual void configureInput(int bcmGpio) = 0;
  virtual void configureOutput(int bcmGpio, GpioValue initialValue) = 0;
  virtual GpioValue read(int bcmGpio) = 0;
  virtual void write(int bcmGpio, GpioValue value) = 0;
  virtual void delayMilliseconds(unsigned int duration) = 0;
};

// The build selects exactly one implementation of this factory. Plain
// hardware builds provide the selected WiringPi or libgpiod backend; sandbox
// and test builds provide the fake backend.
std::unique_ptr<Gpio> createGpio();

#endif
