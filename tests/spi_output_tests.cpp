#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "LPD8806.h"
#include "gpio/FakeGpio.h"
#include "spi/FakeSpiOutput.h"

namespace {

void fail(const char *expression, int line) {
  std::cerr << "SPI output check failed at line " << line
            << ": " << expression << "\n";
  std::exit(1);
}

#define CHECK(expression) \
  do { if (!(expression)) fail(#expression, __LINE__); } while (false)

void testFakeInitializationBoundary() {
  FakeSpiOutput output;
  output.setInitializeResult(false);
  CHECK(!output.initialize());
  CHECK(!output.initialized());

  bool rejected = false;
  try {
    const std::uint8_t byte = 0;
    output.transfer(&byte, 1);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  CHECK(rejected);

  output.setInitializeResult(true);
  CHECK(output.initialize());
  CHECK(output.initialized());
}

void testLpd8806SingleTransferFrame() {
  const std::size_t ledCount = 240;
  const std::size_t dataBytes = ledCount * 3;
  const std::size_t latchBytes = (ledCount + 31) / 32;

  std::recursive_mutex gpioMutex;
  FakeGpio gpio(false);
  FakeSpiOutput output;
  CHECK(output.initialize());

  LPD8806 strip(&gpioMutex, gpio, output,
                static_cast<Int16U>(ledCount));
  strip.begin();

  std::vector<std::vector<std::uint8_t> > transfers = output.transfers();
  CHECK(transfers.size() == 1);
  CHECK(transfers[0].size() == latchBytes);
  CHECK(std::count(transfers[0].begin(), transfers[0].end(), 0) ==
        static_cast<int>(latchBytes));
  CHECK(gpio.operations().empty());

  output.clearTransfers();
  strip.setPixelColor(0, 1, 2, 3);
  strip.setPixelColor(239, 4, 5, 6);
  strip.show();

  transfers = output.transfers();
  CHECK(transfers.size() == 1);
  const std::vector<std::uint8_t> &frame = transfers[0];
  CHECK(frame.size() == dataBytes + latchBytes);
  CHECK(frame[0] == 0x82);
  CHECK(frame[1] == 0x81);
  CHECK(frame[2] == 0x83);
  CHECK(std::count(frame.begin() + 3, frame.begin() + dataBytes - 3, 0x80) ==
        static_cast<int>(dataBytes - 6));
  CHECK(frame[dataBytes - 3] == 0x85);
  CHECK(frame[dataBytes - 2] == 0x84);
  CHECK(frame[dataBytes - 1] == 0x86);
  CHECK(std::count(frame.begin() + dataBytes, frame.end(), 0) ==
        static_cast<int>(latchBytes));
  CHECK(gpio.operations().empty());

  bool rejected = false;
  try {
    strip.updatePins(23, 24);
  } catch (const std::logic_error &) {
    rejected = true;
  }
  CHECK(rejected);

  strip.delayMilliseconds(7);
  const std::vector<GpioOperation> delayOperations = gpio.operations();
  CHECK(delayOperations.size() == 1);
  CHECK(delayOperations[0].kind == GpioOperationKind::delayMilliseconds);
  CHECK(delayOperations[0].duration == 7);
}

void testZeroLengthStripDoesNotTransfer() {
  std::recursive_mutex gpioMutex;
  FakeGpio gpio(false);
  FakeSpiOutput output;
  CHECK(output.initialize());
  LPD8806 strip(&gpioMutex, gpio, output, 0);
  strip.begin();
  strip.show();
  CHECK(output.transfers().empty());
}

} // namespace

int main() {
  testFakeInitializationBoundary();
  testLpd8806SingleTransferFrame();
  testZeroLengthStripDoesNotTransfer();
  std::cout << "SPI output tests passed\n";
  return 0;
}
