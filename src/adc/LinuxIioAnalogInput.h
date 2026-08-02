#ifndef OCLOCK_ADC_LINUX_IIO_ANALOG_INPUT_H
#define OCLOCK_ADC_LINUX_IIO_ANALOG_INPUT_H

#include <memory>
#include <string>

class AnalogInput;

// The custom root is used by deterministic sysfs-fixture tests. Production
// callers use createAnalogInput(), which selects /sys/bus/iio/devices.
std::unique_ptr<AnalogInput>
createLinuxIioAnalogInput(const std::string& iioDevicesRoot);

#endif
