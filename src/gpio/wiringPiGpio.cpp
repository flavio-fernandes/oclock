#include "gpio/Gpio.h"

#include <wiringPi.h>

namespace {

class WiringPiGpio : public Gpio {
public:
  virtual bool initialize() {
    return wiringPiSetupGpio() == 0;
  }

  virtual void configureInput(int bcmGpio) {
    pinMode(bcmGpio, INPUT);
  }

  virtual void configureOutput(int bcmGpio, GpioValue initialValue) {
    pinMode(bcmGpio, OUTPUT);
    digitalWrite(bcmGpio, initialValue == GpioValue::high ? HIGH : LOW);
  }

  virtual GpioValue read(int bcmGpio) {
    return digitalRead(bcmGpio) == HIGH ? GpioValue::high : GpioValue::low;
  }

  virtual void write(int bcmGpio, GpioValue value) {
    digitalWrite(bcmGpio, value == GpioValue::high ? HIGH : LOW);
  }

  virtual void delayMilliseconds(unsigned int duration) {
    delay(duration);
  }
};

}

std::unique_ptr<Gpio> createGpio() {
  return std::unique_ptr<Gpio>(new WiringPiGpio());
}
