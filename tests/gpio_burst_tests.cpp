// Equivalence tests for the narrow HT1632 burst value path.
//
// The burst path bypasses Gpio::write(), so the deterministic protocol tests
// that record FakeGpio operations do not cover it. A divergence between the two
// paths would render wrong content only on real hardware, which is exactly the
// kind of defect these gates exist to prevent.
//
// The specific hazard is the mask-to-pin assignment in HT1632Class::begin():
// the burst path addresses lines by bit mask rather than by BCM number, so
// swapping two masks would drive the wrong wire while still compiling, still
// passing every existing test, and still emitting a plausible-looking waveform.
//
// This test drives identical HT1632 work down both paths and requires the
// emitted pin/value sequences to match exactly.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "HT1632.h"
#include "gpio/FakeGpio.h"
#include "gpio/GpioBurst.h"

namespace {

void fail(const char* expression, int line) {
  std::cerr << "GPIO burst check failed at line " << line
            << ": " << expression << "\n";
  std::exit(1);
}

#define CHECK(expression) \
  do { if (!(expression)) fail(#expression, __LINE__); } while (false)

const int pinCS = 6;
const int pinWR = 13;
const int pinDATA = 19;
const int pinCLK = 26;

struct Edge {
  int bcmGpio;
  GpioValue value;
};

std::vector<Edge> capturedBurstEdges;
std::map<std::uint32_t, int> burstMaskToPin;

std::vector<Edge> edgesFromOperations(
    const std::vector<GpioOperation>& operations) {
  std::vector<Edge> edges;
  for (std::size_t i = 0; i < operations.size(); ++i) {
    if (operations[i].kind != GpioOperationKind::write) continue;
    Edge edge;
    edge.bcmGpio = operations[i].bcmGpio;
    edge.value = operations[i].value;
    edges.push_back(edge);
  }
  return edges;
}

std::string describe(const std::vector<Edge>& edges, std::size_t index) {
  if (index >= edges.size()) return "<past end>";
  return "pin " + std::to_string(edges[index].bcmGpio) + " -> " +
         (edges[index].value == GpioValue::high ? "high" : "low");
}

void compareEdges(const std::vector<Edge>& reference,
                  const std::vector<Edge>& burst, const char* what) {
  for (std::size_t i = 0; i < reference.size() && i < burst.size(); ++i) {
    if (reference[i].bcmGpio != burst[i].bcmGpio ||
        reference[i].value != burst[i].value) {
      std::cerr << what << ": burst path diverged at edge " << i
                << "\n  reference: " << describe(reference, i)
                << "\n  burst:     " << describe(burst, i) << "\n";
      std::exit(1);
    }
  }
  if (reference.size() != burst.size()) {
    std::cerr << what << ": burst emitted " << burst.size()
              << " edges but the reference emitted " << reference.size()
              << "\n";
    std::exit(1);
  }
}

// A Gpio that grants a burst. The registers are never dereferenced because the
// test hook intercepts every store, but they must be non-null for valid().
class BurstGrantingGpio : public FakeGpio {
public:
  BurstGrantingGpio() : FakeGpio(false), setWord_(0), clearWord_(0) {}

  virtual bool acquireBurst(const int* bcmGpios, std::size_t count,
                            GpioBurstPins& pins, std::uint32_t* masks) {
    if (bcmGpios == 0 || masks == 0 || count == 0) return false;
    for (std::size_t i = 0; i < count; ++i) {
      if (bcmGpios[i] < 0 || bcmGpios[i] >= 32) return false;
      masks[i] = std::uint32_t(1) << bcmGpios[i];
      burstMaskToPin[masks[i]] = bcmGpios[i];
    }
    pins = GpioBurstPins(&setWord_, &clearWord_);
    return true;
  }

private:
  volatile std::uint32_t setWord_;
  volatile std::uint32_t clearWord_;
};

// Exercise every HT1632 low-level shape: multi-bit forward writes, reversed
// writes, the single padding bit, and the select chain's clock pulses.
void driveMatrix(HT1632Class& matrix) {
  matrix.drawTarget(0);
  matrix.clear();
  for (int x = 0; x < 24; ++x) {
    matrix.setPixel(x, x % COM_SIZE, true);
  }
  matrix.render();
  matrix.drawTarget(1);
  matrix.clear();
  matrix.setPixel(0, 0, true);
  matrix.setPixel(OUT_SIZE - 1, COM_SIZE - 1, true);
  matrix.render();
}

} // namespace

void oclockGpioBurstTestHook(std::uint32_t mask, GpioValue value) {
  Edge edge;
  edge.bcmGpio = burstMaskToPin.count(mask) ? burstMaskToPin[mask] : -1;
  edge.value = value;
  capturedBurstEdges.push_back(edge);
}

int main() {
  // Reference: FakeGpio declines a burst, so HT1632 uses gpio.write().
  std::vector<Edge> referenceEdges;
  {
    std::recursive_mutex mutex;
    FakeGpio gpio(false);
    HT1632Class matrix(&mutex, gpio);
    matrix.begin(pinCS, pinWR, pinDATA, pinCLK);
    gpio.clearOperations();
    driveMatrix(matrix);
    referenceEdges = edgesFromOperations(gpio.operations());
  }

  CHECK(!referenceEdges.empty());

  // Burst: the backend grants a pre-resolved path and every store is captured.
  std::vector<Edge> burstEdges;
  {
    std::recursive_mutex mutex;
    BurstGrantingGpio gpio;
    HT1632Class matrix(&mutex, gpio);
    matrix.begin(pinCS, pinWR, pinDATA, pinCLK);
    capturedBurstEdges.clear();
    driveMatrix(matrix);
    burstEdges = capturedBurstEdges;

    // The burst path must not fall back to gpio.write() for any edge.
    const std::vector<GpioOperation> operations = gpio.operations();
    for (std::size_t i = 0; i < operations.size(); ++i) {
      CHECK(operations[i].kind != GpioOperationKind::write);
    }
  }

  CHECK(!burstEdges.empty());
  compareEdges(referenceEdges, burstEdges, "HT1632 render");

  // Every edge must resolve to one of the four configured matrix pins. An
  // unmapped mask would surface as -1 and means begin() assigned a stray mask.
  for (std::size_t i = 0; i < burstEdges.size(); ++i) {
    CHECK(burstEdges[i].bcmGpio == pinCS || burstEdges[i].bcmGpio == pinWR ||
          burstEdges[i].bcmGpio == pinDATA || burstEdges[i].bcmGpio == pinCLK);
  }

  // All four pins must actually be exercised, so a mask that is never used
  // cannot hide behind a passing comparison.
  bool sawCS = false, sawWR = false, sawDATA = false, sawCLK = false;
  for (std::size_t i = 0; i < burstEdges.size(); ++i) {
    if (burstEdges[i].bcmGpio == pinCS) sawCS = true;
    if (burstEdges[i].bcmGpio == pinWR) sawWR = true;
    if (burstEdges[i].bcmGpio == pinDATA) sawDATA = true;
    if (burstEdges[i].bcmGpio == pinCLK) sawCLK = true;
  }
  CHECK(sawCS && sawWR && sawDATA && sawCLK);

  std::cout << "burst equivalence passed over " << burstEdges.size()
            << " edges across all four matrix pins\n";
  return 0;
}
