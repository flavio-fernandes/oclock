#include "motionInput.h"

#include "gpio/Gpio.h"

void configureMotionInput(Gpio& gpio, int bcmGpio) {
  gpio.configureInput(bcmGpio);
}

bool readMotionDetected(Gpio& gpio, int bcmGpio) {
  return gpio.read(bcmGpio) == GpioValue::high;
}
