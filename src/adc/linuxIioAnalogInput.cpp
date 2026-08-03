#include "adc/AnalogInput.h"
#include "adc/LinuxIioAnalogInput.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

const char defaultIioDevicesRoot[] = "/sys/bus/iio/devices";
const char adcDeviceTreeSuffix[] = "/oclock-adc-spi/mcp3002@0";
const char adcDriverName[] = "mcp3002";
const int lastAdcChannel = 1;
const int maxAdcValue = 1023;

bool endsWith(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}

std::string readSingleLine(const std::string& path) {
  std::ifstream input(path.c_str());
  std::string value;
  if (!input || !std::getline(input, value)) return std::string();
  while (!value.empty() &&
         (value[value.size() - 1] == '\r' || value[value.size() - 1] == '\n' ||
          value[value.size() - 1] == ' ' || value[value.size() - 1] == '\t')) {
    value.erase(value.size() - 1);
  }
  return value;
}

class LinuxIioAnalogInput : public AnalogInput {
public:
  explicit LinuxIioAnalogInput(const std::string& iioDevicesRoot)
      : iioDevicesRoot_(iioDevicesRoot), devicePath_(), mutex_() {}

  bool initialize() {
    std::lock_guard<std::mutex> guard(mutex_);
    devicePath_.clear();

    DIR* directory = opendir(iioDevicesRoot_.c_str());
    if (directory == NULL) {
      std::fprintf(stderr, "IIO ADC discovery failed: cannot open %s: %s\n",
                   iioDevicesRoot_.c_str(), std::strerror(errno));
      return false;
    }

    std::vector<std::string> matches;
    int readDirectoryError = 0;
    while (true) {
      errno = 0;
      dirent* entry = readdir(directory);
      if (entry == NULL) {
        readDirectoryError = errno;
        break;
      }
      const std::string name(entry->d_name);
      if (name.compare(0, 10, "iio:device") != 0) continue;

      const std::string candidate = iioDevicesRoot_ + "/" + name;
      const std::string ofNode = candidate + "/of_node";
      char resolved[PATH_MAX];
      if (realpath(ofNode.c_str(), resolved) == NULL) continue;
      if (endsWith(resolved, adcDeviceTreeSuffix)) matches.push_back(candidate);
    }
    closedir(directory);
    if (readDirectoryError != 0) {
      std::fprintf(stderr, "IIO ADC discovery failed while reading %s: %s\n",
                   iioDevicesRoot_.c_str(),
                   std::strerror(readDirectoryError));
      return false;
    }

    if (matches.size() != 1) {
      std::fprintf(stderr,
                   "IIO ADC discovery failed: expected one Office Clock "
                   "MCP3002, found %zu\n",
                   matches.size());
      return false;
    }

    if (readSingleLine(matches[0] + "/name") != adcDriverName) {
      std::fprintf(stderr,
                   "IIO ADC discovery failed: Device Tree match is not "
                   "mcp3002\n");
      return false;
    }

    for (int channel = 0; channel <= lastAdcChannel; ++channel) {
      const std::string attribute =
          matches[0] + "/in_voltage" + std::to_string(channel) + "_raw";
      if (access(attribute.c_str(), R_OK) != 0) {
        std::fprintf(stderr,
                     "IIO ADC discovery failed: channel %d is not readable\n",
                     channel);
        return false;
      }
    }

    devicePath_ = matches[0];
    return true;
  }

  int readAnalog(int channel) const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (channel < 0 || channel > lastAdcChannel || devicePath_.empty()) return -1;

    const std::string attribute =
        devicePath_ + "/in_voltage" + std::to_string(channel) + "_raw";
    const std::string text = readSingleLine(attribute);
    if (text.empty()) return -1;

    errno = 0;
    char* end = NULL;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0' || value < 0 ||
        value > maxAdcValue) {
      return -1;
    }
    return static_cast<int>(value);
  }

private:
  const std::string iioDevicesRoot_;
  std::string devicePath_;
  mutable std::mutex mutex_;
};

} // namespace

std::unique_ptr<AnalogInput>
createLinuxIioAnalogInput(const std::string& iioDevicesRoot) {
  return std::unique_ptr<AnalogInput>(new LinuxIioAnalogInput(iioDevicesRoot));
}

std::unique_ptr<AnalogInput> createAnalogInput() {
  return createLinuxIioAnalogInput(defaultIioDevicesRoot);
}
