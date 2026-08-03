#include "adc/AnalogInput.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
  if (argc != 1) {
    std::cerr << "usage: " << argv[0] << "\n";
    return 2;
  }

  try {
    std::unique_ptr<AnalogInput> input = createAnalogInput();
    if (!input.get() || !input->initialize()) {
      std::cerr << "MCP3002 read: IIO input initialization failed\n";
      return 1;
    }

    const std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();
    const int channel0 = input->readAnalog(0);
    const int channel1 = input->readAnalog(1);
    const std::chrono::steady_clock::time_point finish =
        std::chrono::steady_clock::now();
    if (channel0 < 0 || channel1 < 0) {
      std::cerr << "MCP3002 read: raw channel read failed\n";
      return 1;
    }

    const long long elapsedMicroseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(finish - start)
            .count();
    std::cout << "channel0_raw=" << channel0 << "\n"
              << "channel1_raw=" << channel1 << "\n"
              << "read_pair_elapsed_microseconds=" << elapsedMicroseconds
              << "\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "MCP3002 read failed: " << error.what() << "\n";
    return 1;
  }
}
