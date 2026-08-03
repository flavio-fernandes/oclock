#include "adc/AnalogInput.h"

namespace {

class FakeAnalogInput : public AnalogInput {
public:
  bool initialize() { return true; }

  int readAnalog(int channel) const {
    return channel >= 0 && channel <= 1 ? 512 : -1;
  }
};

} // namespace

std::unique_ptr<AnalogInput> createAnalogInput() {
  return std::unique_ptr<AnalogInput>(new FakeAnalogInput());
}
