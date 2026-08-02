#ifndef OCLOCK_ADC_ANALOG_INPUT_H
#define OCLOCK_ADC_ANALOG_INPUT_H

#include <memory>

class AnalogInput {
public:
  virtual ~AnalogInput() {}

  virtual bool initialize() = 0;

  // Return a 10-bit sample, or -1 when the channel or read is invalid.
  virtual int readAnalog(int channel) const = 0;
};

std::unique_ptr<AnalogInput> createAnalogInput();

#endif
