#include "Gpio.h"

#include <gpiod.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <glob.h>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

const char *const expectedChipLabel = "pinctrl-bcm2835";
const unsigned int requiredOffsets[] = {4, 6, 10, 13, 17, 19,
                                        20, 21, 22, 26, 27};
const size_t requiredOffsetCount =
    sizeof(requiredOffsets) / sizeof(requiredOffsets[0]);
const unsigned int greatestRequiredOffset = 27;

std::runtime_error gpioError(const std::string &operation,
                             const std::string &chipPath, int bcmGpio,
                             int errorNumber) {
  std::string message = "libgpiod " + operation + " failed";
  if (!chipPath.empty())
    message += " on " + chipPath;
  if (bcmGpio >= 0)
    message += " BCM GPIO " + std::to_string(bcmGpio);
  if (errorNumber != 0)
    message += ": " + std::string(std::strerror(errorNumber));
  return std::runtime_error(message);
}

class GpiodV2Gpio : public Gpio {
public:
  GpiodV2Gpio() : chip_(NULL), lineCount_(0) {}

  ~GpiodV2Gpio() {
    std::lock_guard<std::mutex> lock(mutex_);
    releaseLines();
    if (chip_ != NULL)
      gpiod_chip_close(chip_);
  }

  bool initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (chip_ != NULL)
      return true;

    try {
      discoverChip();
      validateRequiredOffsets();
      return true;
    } catch (const std::exception &error) {
      std::fprintf(stderr, "GPIO initialization failed: %s\n", error.what());
      releaseLines();
      if (chip_ != NULL) {
        gpiod_chip_close(chip_);
        chip_ = NULL;
      }
      chipPath_.clear();
      lineCount_ = 0;
      return false;
    }
  }

  void configureInput(int bcmGpio) {
    std::lock_guard<std::mutex> lock(mutex_);
    configure(bcmGpio, GPIOD_LINE_DIRECTION_INPUT,
              GPIOD_LINE_VALUE_INACTIVE);
  }

  void configureOutput(int bcmGpio, GpioValue initialValue) {
    std::lock_guard<std::mutex> lock(mutex_);
    configure(bcmGpio, GPIOD_LINE_DIRECTION_OUTPUT,
              toGpiodValue(initialValue));
  }

  GpioValue read(int bcmGpio) {
    std::lock_guard<std::mutex> lock(mutex_);
    Line &line = getConfiguredLine(bcmGpio, GPIOD_LINE_DIRECTION_INPUT,
                                   "read");
    errno = 0;
    const gpiod_line_value value =
        gpiod_line_request_get_value(line.request, bcmGpio);
    if (value == GPIOD_LINE_VALUE_ERROR)
      throw gpioError("read", chipPath_, bcmGpio, errno);
    return value == GPIOD_LINE_VALUE_ACTIVE ? GpioValue::high
                                             : GpioValue::low;
  }

  void write(int bcmGpio, GpioValue value) {
    std::lock_guard<std::mutex> lock(mutex_);
    Line &line = getConfiguredLine(bcmGpio, GPIOD_LINE_DIRECTION_OUTPUT,
                                   "write");
    errno = 0;
    if (gpiod_line_request_set_value(line.request, bcmGpio,
                                     toGpiodValue(value)) < 0)
      throw gpioError("write", chipPath_, bcmGpio, errno);
  }

  void delayMilliseconds(unsigned int duration) {
    std::this_thread::sleep_for(std::chrono::milliseconds(duration));
  }

private:
  struct Line {
    gpiod_line_request *request;
    gpiod_line_direction direction;
  };

  static gpiod_line_value toGpiodValue(GpioValue value) {
    return value == GpioValue::high ? GPIOD_LINE_VALUE_ACTIVE
                                    : GPIOD_LINE_VALUE_INACTIVE;
  }

  void discoverChip() {
    glob_t paths;
    std::memset(&paths, 0, sizeof(paths));
    const int globResult = glob("/dev/gpiochip*", GLOB_NOSORT, NULL, &paths);
    if (globResult != 0) {
      globfree(&paths);
      throw gpioError("chip discovery (no /dev/gpiochip* devices)", "", -1,
                      globResult == GLOB_NOMATCH ? ENOENT : EIO);
    }

    for (size_t index = 0; index < paths.gl_pathc; ++index) {
      const char *path = paths.gl_pathv[index];
      gpiod_chip *candidate = gpiod_chip_open(path);
      if (candidate == NULL)
        continue;

      gpiod_chip_info *info = gpiod_chip_get_info(candidate);
      if (info == NULL) {
        gpiod_chip_close(candidate);
        continue;
      }

      const char *label = gpiod_chip_info_get_label(info);
      const size_t lineCount = gpiod_chip_info_get_num_lines(info);
      const bool matches = label != NULL &&
                           expectedChipLabel == std::string(label) &&
                           lineCount > greatestRequiredOffset;
      gpiod_chip_info_free(info);
      if (!matches) {
        gpiod_chip_close(candidate);
        continue;
      }

      chip_ = candidate;
      chipPath_ = path;
      lineCount_ = lineCount;
      break;
    }
    globfree(&paths);

    if (chip_ == NULL)
      throw gpioError("chip discovery (label pinctrl-bcm2835 not found)", "",
                      -1, ENODEV);
  }

  void validateRequiredOffsets() {
    for (size_t index = 0; index < requiredOffsetCount; ++index) {
      const unsigned int offset = requiredOffsets[index];
      if (offset >= lineCount_)
        throw gpioError("line validation", chipPath_, offset, ERANGE);

      errno = 0;
      gpiod_line_info *info = gpiod_chip_get_line_info(chip_, offset);
      if (info == NULL)
        throw gpioError("line-info lookup", chipPath_, offset, errno);

      const char *name = gpiod_line_info_get_name(info);
      const std::string expectedName = "GPIO" + std::to_string(offset);
      const bool matches = name != NULL && expectedName == name;
      gpiod_line_info_free(info);
      if (!matches)
        throw gpioError("line-name validation (expected " + expectedName + ")",
                        chipPath_, offset, EINVAL);
    }
  }

  void configure(int bcmGpio, gpiod_line_direction direction,
                 gpiod_line_value initialValue) {
    ensureInitialized(bcmGpio, "configure");
    if (bcmGpio < 0 || static_cast<size_t>(bcmGpio) >= lineCount_)
      throw gpioError("configure", chipPath_, bcmGpio, ERANGE);

    std::map<int, Line>::iterator old = lines_.find(bcmGpio);
    if (old != lines_.end()) {
      gpiod_line_request_release(old->second.request);
      lines_.erase(old);
    }

    std::unique_ptr<gpiod_line_settings, void (*)(gpiod_line_settings *)>
        settings(gpiod_line_settings_new(), gpiod_line_settings_free);
    std::unique_ptr<gpiod_line_config, void (*)(gpiod_line_config *)> lineConfig(
        gpiod_line_config_new(), gpiod_line_config_free);
    std::unique_ptr<gpiod_request_config, void (*)(gpiod_request_config *)>
        requestConfig(gpiod_request_config_new(), gpiod_request_config_free);
    if (!settings || !lineConfig || !requestConfig)
      throw gpioError("request allocation", chipPath_, bcmGpio, errno);

    if (gpiod_line_settings_set_direction(settings.get(), direction) < 0)
      throw gpioError("direction setup", chipPath_, bcmGpio, errno);
    if (direction == GPIOD_LINE_DIRECTION_OUTPUT &&
        gpiod_line_settings_set_output_value(settings.get(), initialValue) < 0)
      throw gpioError("initial-value setup", chipPath_, bcmGpio, errno);

    const unsigned int offset = static_cast<unsigned int>(bcmGpio);
    if (gpiod_line_config_add_line_settings(lineConfig.get(), &offset, 1,
                                            settings.get()) < 0)
      throw gpioError("line configuration", chipPath_, bcmGpio, errno);

    const std::string consumer = "oclock-gpio" + std::to_string(bcmGpio);
    gpiod_request_config_set_consumer(requestConfig.get(), consumer.c_str());
    errno = 0;
    gpiod_line_request *request = gpiod_chip_request_lines(
        chip_, requestConfig.get(), lineConfig.get());
    if (request == NULL)
      throw gpioError("line request", chipPath_, bcmGpio, errno);

    Line line = {request, direction};
    lines_.insert(std::make_pair(bcmGpio, line));
  }

  Line &getConfiguredLine(int bcmGpio, gpiod_line_direction direction,
                          const char *operation) {
    ensureInitialized(bcmGpio, operation);
    std::map<int, Line>::iterator line = lines_.find(bcmGpio);
    if (line == lines_.end() || line->second.direction != direction)
      throw gpioError(std::string(operation) + " (line not configured for " +
                          (direction == GPIOD_LINE_DIRECTION_INPUT ? "input)"
                                                                   : "output)"),
                      chipPath_, bcmGpio, EINVAL);
    return line->second;
  }

  void ensureInitialized(int bcmGpio, const char *operation) {
    if (chip_ == NULL)
      throw gpioError(std::string(operation) + " (backend not initialized)",
                      chipPath_, bcmGpio, ENODEV);
  }

  void releaseLines() {
    for (std::map<int, Line>::iterator line = lines_.begin();
         line != lines_.end(); ++line)
      gpiod_line_request_release(line->second.request);
    lines_.clear();
  }

  gpiod_chip *chip_;
  std::string chipPath_;
  size_t lineCount_;
  std::map<int, Line> lines_;
  std::mutex mutex_;
};

} // namespace

std::unique_ptr<Gpio> createGpio() {
  return std::unique_ptr<Gpio>(new GpiodV2Gpio());
}
