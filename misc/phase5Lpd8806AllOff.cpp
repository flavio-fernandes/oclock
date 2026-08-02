#include "LPD8806.h"
#include "gpio/FakeGpio.h"
#include "spi/SpiOutput.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>

namespace {

const Int16U ledCount = 240;
const std::size_t bytesPerLed = 3;
const std::size_t dataBytes =
    static_cast<std::size_t>(ledCount) * bytesPerLed;
const std::size_t latchBytes =
    (static_cast<std::size_t>(ledCount) + 31) / 32;
const unsigned int speedHz = 1000000;

} // namespace

int main(int argc, char **argv) {
  if (argc != 1) {
    std::cerr << "usage: " << argv[0] << "\n";
    return 2;
  }

  try {
    std::unique_ptr<SpiOutput> output = createStripSpiOutput();
    if (!output.get() || !output->initialize()) {
      std::cerr << "all-off transfer: SPI output initialization failed\n";
      return 1;
    }

    // The SPI-mode LPD8806 path never calls GPIO. FakeGpio makes that safety
    // boundary explicit while reusing the application's exact frame assembly.
    std::recursive_mutex mutex;
    FakeGpio gpio(false);
    LPD8806 strip(&mutex, gpio, *output, ledCount);

    const std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();
    strip.show();
    const std::chrono::steady_clock::time_point finish =
        std::chrono::steady_clock::now();
    const long long elapsedMicroseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(finish - start)
            .count();

    if (!gpio.operations().empty()) {
      std::cerr << "all-off transfer: unexpected GPIO operation\n";
      return 1;
    }

    std::cout << "led_count=" << ledCount << "\n"
              << "data_bytes=" << dataBytes << "\n"
              << "latch_bytes=" << latchBytes << "\n"
              << "payload_bytes=" << dataBytes + latchBytes << "\n"
              << "speed_hz=" << speedHz << "\n"
              << "elapsed_microseconds=" << elapsedMicroseconds << "\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "all-off transfer failed: " << error.what() << "\n";
    return 1;
  }
}
