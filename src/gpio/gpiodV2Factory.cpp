#include "gpiodValueIo.h"

std::unique_ptr<Gpio> createGpio() {
  return createGpiodV2Gpio(createLibgpiodValueIo());
}
