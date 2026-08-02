#include "gpiodValueIo.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "bcm2835GpioRegisters.h"

namespace {

const char *const gpioMemoryPath = "/dev/gpiomem";

class Bcm2835MmapValueIo : public GpiodValueIo {
public:
  Bcm2835MmapValueIo()
      : fileDescriptor_(-1), mapping_(MAP_FAILED), registers_() {}

  ~Bcm2835MmapValueIo() { closeMapping(); }

  bool initialize(std::string &error) {
    if (registers_)
      return true;

    fileDescriptor_ =
        open(gpioMemoryPath, O_RDWR | O_CLOEXEC | O_SYNC);
    if (fileDescriptor_ < 0) {
      error = std::string("open ") + gpioMemoryPath + ": " +
              std::strerror(errno);
      return false;
    }

    mapping_ = mmap(NULL, Bcm2835GpioRegisters::registerMapBytes,
                    PROT_READ | PROT_WRITE, MAP_SHARED, fileDescriptor_, 0);
    if (mapping_ == MAP_FAILED) {
      error = std::string("mmap ") + gpioMemoryPath + ": " +
              std::strerror(errno);
      close(fileDescriptor_);
      fileDescriptor_ = -1;
      return false;
    }

    registers_.reset(new Bcm2835GpioRegisters(
        static_cast<volatile std::uint32_t *>(mapping_)));
    return true;
  }

  int read(gpiod_line_request *request, int bcmGpio, GpioValue &value) {
    (void)request;
    if (!registers_)
      return ENODEV;
    if (!Bcm2835GpioRegisters::supportsOffset(bcmGpio))
      return ERANGE;
    value = registers_->read(bcmGpio);
    return 0;
  }

  int write(gpiod_line_request *request, int bcmGpio, GpioValue value) {
    (void)request;
    if (!registers_)
      return ENODEV;
    if (!Bcm2835GpioRegisters::supportsOffset(bcmGpio))
      return ERANGE;
    registers_->write(bcmGpio, value);
    return 0;
  }

  const char *description() const { return "BCM2835 /dev/gpiomem"; }

private:
  void closeMapping() {
    registers_.reset();
    if (mapping_ != MAP_FAILED) {
      munmap(mapping_, Bcm2835GpioRegisters::registerMapBytes);
      mapping_ = MAP_FAILED;
    }
    if (fileDescriptor_ >= 0) {
      close(fileDescriptor_);
      fileDescriptor_ = -1;
    }
  }

  int fileDescriptor_;
  void *mapping_;
  std::unique_ptr<Bcm2835GpioRegisters> registers_;
};

} // namespace

std::unique_ptr<GpiodValueIo> createBcm2835MmapValueIo() {
  return std::unique_ptr<GpiodValueIo>(new Bcm2835MmapValueIo());
}
