#ifndef OCLOCK_GPIO_GPIOD_VALUE_IO_H
#define OCLOCK_GPIO_GPIOD_VALUE_IO_H

#include <gpiod.h>

#include <memory>
#include <string>

#include "Gpio.h"

// Internal value-path boundary used by the two libgpiod-backed hardware
// builds. Both builds leave chip discovery, line validation, direction,
// initial values, ownership, and cleanup in the common GpiodV2Gpio class.
class GpiodValueIo {
public:
  virtual ~GpiodValueIo() {}

  virtual bool initialize(std::string &error) = 0;
  virtual int read(gpiod_line_request *request, int bcmGpio,
                   GpioValue &value) = 0;
  virtual int write(gpiod_line_request *request, int bcmGpio,
                    GpioValue value) = 0;
  virtual const char *description() const = 0;

  // Only the mapped backend can expose value registers directly. The plain
  // libgpiod backend keeps the default and forces callers onto write().
  virtual bool burstRegisters(volatile std::uint32_t *&setRegister,
                              volatile std::uint32_t *&clearRegister) {
    (void)setRegister;
    (void)clearRegister;
    return false;
  }
};

std::unique_ptr<Gpio>
createGpiodV2Gpio(std::unique_ptr<GpiodValueIo> valueIo);
std::unique_ptr<GpiodValueIo> createLibgpiodValueIo();
std::unique_ptr<GpiodValueIo> createBcm2835MmapValueIo();

#endif
