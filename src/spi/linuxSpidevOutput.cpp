#include "spi/SpiOutput.h"
#include "spi/StripSpeed.h"

#include <linux/spi/spidev.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <mutex>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

const char *const spiDevicesPattern = "/sys/bus/spi/devices/spi*.*";
const char *const stripDeviceTreeSuffix = "/oclock-strip-spi/lpd8806@0";
const std::uint8_t stripBitsPerWord = 8;

std::runtime_error spiError(const std::string &operation,
                            const std::string &path, int errorNumber) {
  std::string message = "spidev " + operation + " failed";
  if (!path.empty()) message += " on " + path;
  if (errorNumber != 0)
    message += ": " + std::string(std::strerror(errorNumber));
  return std::runtime_error(message);
}

bool endsWith(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}

std::string resolvedPath(const std::string &path) {
  char resolved[PATH_MAX];
  errno = 0;
  if (realpath(path.c_str(), resolved) == NULL)
    throw spiError("Device Tree discovery", path, errno);
  return resolved;
}

std::string discoverStripDevice() {
  glob_t paths;
  std::memset(&paths, 0, sizeof(paths));
  const int result = glob(spiDevicesPattern, GLOB_NOSORT, NULL, &paths);
  if (result != 0) {
    globfree(&paths);
    throw spiError("discovery (no SPI children)", spiDevicesPattern,
                   result == GLOB_NOMATCH ? ENOENT : EIO);
  }

  std::vector<std::string> matches;
  for (std::size_t index = 0; index < paths.gl_pathc; ++index) {
    const std::string child(paths.gl_pathv[index]);
    try {
      if (endsWith(resolvedPath(child + "/of_node"),
                   stripDeviceTreeSuffix)) {
        matches.push_back(child);
      }
    } catch (const std::exception &) {
      // Ignore unrelated SPI children that do not have a live Device Tree
      // node. The exact Office Clock child is required below.
    }
  }
  globfree(&paths);

  if (matches.size() != 1)
    throw spiError("discovery (expected exactly one Office Clock strip child)",
                   spiDevicesPattern, matches.empty() ? ENODEV : EEXIST);

  const std::string &sysfsPath = matches.front();
  const std::string::size_type slash = sysfsPath.rfind('/');
  const std::string childName = sysfsPath.substr(slash + 1);
  if (childName.compare(0, 3, "spi") != 0 || childName.size() <= 3)
    throw spiError("discovery (unexpected SPI child name)", sysfsPath,
                   EINVAL);
  return "/dev/spidev" + childName.substr(3);
}

class LinuxSpidevOutput : public SpiOutput {
public:
  LinuxSpidevOutput() : fd_(-1) {}

  ~LinuxSpidevOutput() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (fd_ >= 0) close(fd_);
  }

  bool initialize() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (fd_ >= 0) return true;

    try {
      devicePath_ = discoverStripDevice();
      struct stat deviceStat;
      if (stat(devicePath_.c_str(), &deviceStat) < 0)
        throw spiError("device lookup", devicePath_, errno);
      if (!S_ISCHR(deviceStat.st_mode))
        throw spiError("device validation (not a character device)",
                       devicePath_, ENODEV);

      errno = 0;
      fd_ = open(devicePath_.c_str(), O_RDWR | O_CLOEXEC);
      if (fd_ < 0) throw spiError("open", devicePath_, errno);
      configure();
      return true;
    } catch (const std::exception &error) {
      std::fprintf(stderr, "SPI output initialization failed: %s\n",
                   error.what());
      if (fd_ >= 0) close(fd_);
      fd_ = -1;
      devicePath_.clear();
      return false;
    }
  }

  void transfer(const std::uint8_t *data, std::size_t length) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (fd_ < 0)
      throw spiError("transfer (transport not initialized)", devicePath_,
                     ENODEV);
    if (data == NULL && length != 0)
      throw spiError("transfer (data is missing)", devicePath_, EINVAL);
    if (length > static_cast<std::size_t>(UINT32_MAX))
      throw spiError("transfer (payload is too large)", devicePath_, E2BIG);
    if (length == 0) return;

    struct spi_ioc_transfer transfer;
    std::memset(&transfer, 0, sizeof(transfer));
    transfer.tx_buf = static_cast<__u64>(
        reinterpret_cast<std::uintptr_t>(data));
    transfer.len = static_cast<__u32>(length);
    transfer.speed_hz = oclockSpi::stripSpeedHz;
    transfer.bits_per_word = stripBitsPerWord;

    int result;
    do {
      errno = 0;
      result = ioctl(fd_, SPI_IOC_MESSAGE(1), &transfer);
    } while (result < 0 && errno == EINTR);
    if (result < 0) throw spiError("transfer", devicePath_, errno);
    if (result != static_cast<int>(length))
      throw spiError("transfer (short write)", devicePath_, EIO);
  }

private:
  void configure() {
    // The strip's dedicated spi-gpio controller has num-chipselects = <0>, so
    // there is no chip-select signal for the kernel to drive. Do not also ask
    // spidev for SPI_NO_CS: this exact controller does not advertise that mode
    // bit and correctly rejects it before a transfer.
    std::uint32_t mode = SPI_MODE_0;
    std::uint8_t bits = stripBitsPerWord;
    std::uint8_t lsbFirst = 0;
    std::uint32_t speed = oclockSpi::stripSpeedHz;

    if (ioctl(fd_, SPI_IOC_WR_MODE32, &mode) < 0)
      throw spiError("mode configuration", devicePath_, errno);
    if (ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0)
      throw spiError("word-size configuration", devicePath_, errno);
    if (ioctl(fd_, SPI_IOC_WR_LSB_FIRST, &lsbFirst) < 0)
      throw spiError("bit-order configuration", devicePath_, errno);
    if (ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
      throw spiError("speed configuration", devicePath_, errno);

    std::uint32_t actualMode = 0;
    std::uint8_t actualBits = 0;
    std::uint8_t actualLsbFirst = 1;
    std::uint32_t actualSpeed = 0;
    if (ioctl(fd_, SPI_IOC_RD_MODE32, &actualMode) < 0 ||
        ioctl(fd_, SPI_IOC_RD_BITS_PER_WORD, &actualBits) < 0 ||
        ioctl(fd_, SPI_IOC_RD_LSB_FIRST, &actualLsbFirst) < 0 ||
        ioctl(fd_, SPI_IOC_RD_MAX_SPEED_HZ, &actualSpeed) < 0)
      throw spiError("configuration verification", devicePath_, errno);
    if (actualMode != mode || actualBits != bits ||
        actualLsbFirst != lsbFirst || actualSpeed != speed)
      throw spiError("configuration verification (unexpected values)",
                     devicePath_, EINVAL);
  }

  int fd_;
  std::string devicePath_;
  std::mutex mutex_;

  LinuxSpidevOutput(const LinuxSpidevOutput&) = delete;
  LinuxSpidevOutput& operator=(const LinuxSpidevOutput&) = delete;
};

} // namespace

std::unique_ptr<SpiOutput> createStripSpiOutput() {
  return std::unique_ptr<SpiOutput>(new LinuxSpidevOutput());
}
