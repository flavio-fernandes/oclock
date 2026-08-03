#include "LPD8806.h"
#include "gpio/FakeGpio.h"
#include "spi/SpiOutput.h"
#include "spi/StripSpeed.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

namespace {

const Int16U ledCount = 240;
const std::size_t bytesPerLed = 3;
const std::size_t dataBytes =
    static_cast<std::size_t>(ledCount) * bytesPerLed;
const std::size_t latchBytes =
    (static_cast<std::size_t>(ledCount) + 31) / 32;

// Half of the LPD8806's 7-bit per-channel range. The operator approved half
// brightness for this gate; the application itself uses comparable levels.
const Int8U halfBrightness = 0x3F;

const unsigned int holdMilliseconds = 3000;

struct Step {
  const char *name;
  Int8U r;
  Int8U g;
  Int8U b;
  bool hold;
};

// The final all-off step is mandatory, not decorative. LPD8806 pixels latch
// and retain their last value, so leaving a lit frame on the strip would
// outlive the runtime spidev binding this gate is required to remove.
const Step steps[] = {
    {"red", halfBrightness, 0, 0, true},
    {"green", 0, halfBrightness, 0, true},
    {"blue", 0, 0, halfBrightness, true},
    {"off", 0, 0, 0, false},
};

const std::size_t stepCount = sizeof(steps) / sizeof(steps[0]);

// Confirm the assembled frame really carries the requested color before it is
// clocked out. A dark strip cannot distinguish "correct data" from "no data",
// so the colored gate verifies the buffer as well as the wire timing.
bool readbackMatches(const LPD8806 &strip, const Step &step) {
  const Int32U expected = LPD8806::Color(step.r, step.g, step.b);
  const Int16U probes[] = {0, 1, ledCount / 2, ledCount - 1};
  for (std::size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
    if (strip.getPixelColor(probes[i]) != (expected & 0x7f7f7fu)) {
      return false;
    }
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 1) {
    std::cerr << "usage: " << argv[0] << "\n";
    return 2;
  }

  try {
    std::unique_ptr<SpiOutput> output = createStripSpiOutput();
    if (!output.get() || !output->initialize()) {
      std::cerr << "color transfer: SPI output initialization failed\n";
      return 1;
    }

    // The SPI-mode LPD8806 path never calls GPIO. FakeGpio makes that safety
    // boundary explicit while reusing the application's exact frame assembly.
    std::recursive_mutex mutex;
    FakeGpio gpio(false);
    LPD8806 strip(&mutex, gpio, *output, ledCount);

    std::cout << "led_count=" << ledCount << "\n"
              << "data_bytes=" << dataBytes << "\n"
              << "latch_bytes=" << latchBytes << "\n"
              << "payload_bytes=" << dataBytes + latchBytes << "\n"
              << "speed_hz=" << oclockSpi::stripSpeedHz << "\n"
              << "brightness=" << static_cast<unsigned>(halfBrightness) << "\n"
              << "hold_milliseconds=" << holdMilliseconds << "\n"
              << "sequence=red,green,blue,off\n";

    long long maxElapsedMicroseconds = 0;
    for (std::size_t i = 0; i < stepCount; ++i) {
      const Step &step = steps[i];
      for (Int16U pixel = 0; pixel < ledCount; ++pixel) {
        strip.setPixelColor(pixel, step.r, step.g, step.b);
      }

      if (!readbackMatches(strip, step)) {
        std::cerr << "color transfer: readback mismatch for " << step.name
                  << "\n";
        return 1;
      }

      const std::chrono::steady_clock::time_point start =
          std::chrono::steady_clock::now();
      strip.show();
      const std::chrono::steady_clock::time_point finish =
          std::chrono::steady_clock::now();
      const long long elapsedMicroseconds =
          std::chrono::duration_cast<std::chrono::microseconds>(finish - start)
              .count();
      if (elapsedMicroseconds > maxElapsedMicroseconds) {
        maxElapsedMicroseconds = elapsedMicroseconds;
      }

      std::cout << "frame_" << step.name
                << "_microseconds=" << elapsedMicroseconds << "\n"
                << std::flush;

      // Deliberately not LPD8806::delayMilliseconds: that routes through the
      // Gpio object and would violate the no-GPIO boundary asserted below.
      if (step.hold) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(holdMilliseconds));
      }
    }

    if (!gpio.operations().empty()) {
      std::cerr << "color transfer: unexpected GPIO operation\n";
      return 1;
    }

    std::cout << "frames_sent=" << stepCount << "\n"
              << "max_frame_microseconds=" << maxElapsedMicroseconds << "\n"
              << "final_state=off\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "color transfer failed: " << error.what() << "\n";
    return 1;
  }
}
