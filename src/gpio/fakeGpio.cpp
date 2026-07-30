#include "gpio/Gpio.h"

#include <chrono>
#include <thread>

namespace {

class FakeGpio : public Gpio {
public:
  virtual bool initialize() {
    return true;
  }

  virtual void configureInput(int /*bcmGpio*/) {
  }

  virtual void configureOutput(int /*bcmGpio*/) {
  }

  virtual GpioValue read(int /*bcmGpio*/) {
    return GpioValue::low;
  }

  virtual void write(int /*bcmGpio*/, GpioValue /*value*/) {
  }

  virtual void delayMilliseconds(unsigned int duration) {
    std::this_thread::sleep_for(std::chrono::milliseconds(duration));
  }
};

}

std::unique_ptr<Gpio> createGpio() {
  return std::unique_ptr<Gpio>(new FakeGpio());
}
