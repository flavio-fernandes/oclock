#include "HT1632.h"
#include "gpio/Gpio.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

// Matching src/display.cpp exactly. No rewiring is authorized for this gate.
const int pinCS = 6;
const int pinWR = 13;
const int pinDATA = 19;
const int pinCLK = 26;

// src/timerTick.cpp: the display thread renders on this cadence.
const long long tickBudgetMicroseconds = 12000;

const int panelWidth = OUT_SIZE;   // 128
const int panelHeight = COM_SIZE;  // 16

const int stripePeriod = 8;
const unsigned int holdMilliseconds = 3000;
const int timedRenders = 20;

// Vertical stripes every eight columns exercise addressing across all sixteen
// chips and make an addressing fault obvious to the eye. A fully lit panel
// could not reveal one, and draws far more current.
void drawStripes(HT1632Class &matrix, int phase) {
  for (int x = 0; x < panelWidth; ++x) {
    const bool on = ((x + phase) % stripePeriod) == 0;
    for (int y = 0; y < panelHeight; ++y) {
      matrix.setPixel(x, y, on);
    }
  }
}

long long renderMicroseconds(HT1632Class &matrix) {
  const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
  matrix.render();
  const std::chrono::steady_clock::time_point finish =
      std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(finish - start)
      .count();
}

void blankEverything(HT1632Class &matrix) {
  matrix.clearAll();
  matrix.renderAll();
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 1) {
    std::cerr << "usage: " << argv[0] << "\n";
    return 2;
  }

  try {
    std::unique_ptr<Gpio> gpio = createGpio();
    if (!gpio.get() || !gpio->initialize()) {
      std::cerr << "ht1632 render: GPIO initialization failed\n";
      return 1;
    }

    std::recursive_mutex mutex;
    HT1632Class matrix(&mutex, *gpio);
    matrix.begin(pinCS, pinWR, pinDATA, pinCLK);

    std::cout << "panel_width=" << panelWidth << "\n"
              << "panel_height=" << panelHeight << "\n"
              << "active_chips=" << NUM_ACTIVE_CHIPS << "\n"
              << "address_space=" << ADDR_SPACE_SIZE << "\n"
              << "tick_budget_microseconds=" << tickBudgetMicroseconds << "\n"
              << "stripe_period=" << stripePeriod << "\n"
              << "hold_milliseconds=" << holdMilliseconds << "\n"
              << "timed_renders=" << timedRenders << "\n"
              << "sequence=green,red,benchmark,off\n"
              << std::flush;

    blankEverything(matrix);

    // Visual phase: prove real content reaches the panel on each color board.
    const char *const colorNames[NUM_COLORS] = {"green", "red"};
    for (int board = 0; board < NUM_COLORS; ++board) {
      matrix.drawTarget(BUFFER_BOARD(board + 1));
      matrix.clear();
      drawStripes(matrix, 0);
      const long long elapsed = renderMicroseconds(matrix);
      std::cout << "frame_" << colorNames[board]
                << "_microseconds=" << elapsed << "\n"
                << std::flush;
      std::this_thread::sleep_for(
          std::chrono::milliseconds(holdMilliseconds));
      matrix.clear();
      matrix.render();
    }

    // Timing phase: repeated worst-case renders. clear() marks the whole
    // buffer dirty, so each measured render rewrites the entire address space
    // with real content rather than relying on dirty-chunk tracking.
    matrix.drawTarget(BUFFER_BOARD(1));
    std::vector<long long> samples;
    samples.reserve(timedRenders);
    for (int i = 0; i < timedRenders; ++i) {
      matrix.clear();
      drawStripes(matrix, i % stripePeriod);
      samples.push_back(renderMicroseconds(matrix));
    }

    blankEverything(matrix);

    long long minimum = samples[0];
    long long maximum = samples[0];
    long long total = 0;
    int withinBudget = 0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
      const long long value = samples[i];
      if (value < minimum) minimum = value;
      if (value > maximum) maximum = value;
      total += value;
      if (value <= tickBudgetMicroseconds) ++withinBudget;
      std::cout << "sample_" << (i + 1) << "_microseconds=" << value << "\n";
    }

    std::cout << "valid_renders=" << samples.size() << "\n"
              << "minimum_microseconds=" << minimum << "\n"
              << "maximum_microseconds=" << maximum << "\n"
              << "mean_microseconds=" << (total / static_cast<long long>(samples.size()))
              << "\n"
              << "renders_within_budget=" << withinBudget << "\n"
              << "final_state=off\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "ht1632 render failed: " << error.what() << "\n";
    return 1;
  }
}
