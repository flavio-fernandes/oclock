#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "HT1632.h"
#include "LPD8806.h"
#include "gpio/FakeGpio.h"
#include "mcp300x.h"
#include "motionInput.h"

namespace {

void fail(const char* expression, int line) {
  std::cerr << "GPIO protocol check failed at line " << line
            << ": " << expression << "\n";
  std::exit(1);
}

#define CHECK(expression) \
  do { if (!(expression)) fail(#expression, __LINE__); } while (false)

bool isOperation(const GpioOperation& operation, GpioOperationKind kind,
                 int bcmGpio) {
  return operation.kind == kind && operation.bcmGpio == bcmGpio;
}

bool isOperation(const GpioOperation& operation, GpioOperationKind kind,
                 int bcmGpio, GpioValue value) {
  return isOperation(operation, kind, bcmGpio) &&
         operation.value == value;
}

std::vector<int> sampledBits(const std::vector<GpioOperation>& operations,
                             int dataPin, int clockPin,
                             GpioValue initialData = GpioValue::low) {
  GpioValue data = initialData;
  std::vector<int> bits;
  for (const GpioOperation& operation : operations) {
    if (operation.kind == GpioOperationKind::configureOutput &&
        operation.bcmGpio == dataPin) {
      data = operation.value;
    } else if (operation.kind == GpioOperationKind::write &&
               operation.bcmGpio == dataPin) {
      data = operation.value;
    } else if (isOperation(operation, GpioOperationKind::write, clockPin,
                           GpioValue::high)) {
      bits.push_back(data == GpioValue::high ? 1 : 0);
    }
  }
  return bits;
}

std::vector<unsigned int> bytesFromBits(const std::vector<int>& bits) {
  CHECK(bits.size() % 8 == 0);
  std::vector<unsigned int> bytes;
  for (std::size_t offset = 0; offset < bits.size(); offset += 8) {
    unsigned int byte = 0;
    for (std::size_t bit = 0; bit < 8; ++bit) {
      byte = (byte << 1) | static_cast<unsigned int>(bits[offset + bit]);
    }
    bytes.push_back(byte);
  }
  return bytes;
}

std::size_t countWrites(const std::vector<GpioOperation>& operations,
                        int bcmGpio, GpioValue value) {
  return static_cast<std::size_t>(std::count_if(
    operations.begin(), operations.end(),
    [bcmGpio, value](const GpioOperation& operation) {
      return isOperation(operation, GpioOperationKind::write,
                         bcmGpio, value);
    }));
}

void testFakeConfigurationAndHistory() {
  FakeGpio gpio(false);
  gpio.setInitializeResult(false);
  CHECK(!gpio.initialize());
  gpio.setInitializeResult(true);
  CHECK(gpio.initialize());

  gpio.setInputValue(10, GpioValue::high);
  gpio.queueInputValues(
    10, std::vector<GpioValue>{GpioValue::low, GpioValue::high});
  CHECK(gpio.read(10) == GpioValue::low);
  CHECK(gpio.read(10) == GpioValue::high);
  CHECK(gpio.read(10) == GpioValue::high);

  const std::vector<GpioOperation> operations = gpio.operations();
  CHECK(operations.size() == 5);
  CHECK(operations[0].kind == GpioOperationKind::initialize);
  CHECK(operations[1].kind == GpioOperationKind::initialize);
  CHECK(isOperation(operations[2], GpioOperationKind::read, 10,
                    GpioValue::low));
  CHECK(isOperation(operations[3], GpioOperationKind::read, 10,
                    GpioValue::high));
  CHECK(isOperation(operations[4], GpioOperationKind::read, 10,
                    GpioValue::high));
}

void testHt1632InitializationCommandsAndRender() {
  const int chipSelect = 6;
  const int writeClock = 13;
  const int data = 19;
  const int selectClock = 26;

  std::recursive_mutex gpioMutex;
  FakeGpio gpio(false);
  HT1632Class display(&gpioMutex, gpio);
  display.begin(chipSelect, writeClock, data, selectClock);

  std::vector<GpioOperation> operations = gpio.operations();
  CHECK(operations.size() > 4);
  CHECK(isOperation(operations[0], GpioOperationKind::configureOutput,
                    chipSelect, GpioValue::high));
  CHECK(isOperation(operations[1], GpioOperationKind::configureOutput,
                    writeClock, GpioValue::low));
  CHECK(isOperation(operations[2], GpioOperationKind::configureOutput,
                    data, GpioValue::low));
  CHECK(isOperation(operations[3], GpioOperationKind::configureOutput,
                    selectClock, GpioValue::low));

  gpio.clearOperations();
  display.setBrightness(5);
  operations = gpio.operations();

  const std::vector<int> commandBits =
    sampledBits(operations, data, writeClock);
  const std::vector<int> expectedCommand =
    {1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0};
  CHECK(commandBits.size() == expectedCommand.size() * NUM_ACTIVE_CHIPS);
  for (int chip = 0; chip < NUM_ACTIVE_CHIPS; ++chip) {
    CHECK(std::equal(expectedCommand.begin(), expectedCommand.end(),
                     commandBits.begin() + chip * expectedCommand.size()));
  }

  CHECK(countWrites(operations, chipSelect, GpioValue::low) ==
        NUM_ACTIVE_CHIPS);
  CHECK(countWrites(operations, selectClock, GpioValue::high) == 408);

  gpio.clearOperations();
  display.clear();
  display.render();
  operations = gpio.operations();

  const std::vector<int> renderBits =
    sampledBits(operations, data, writeClock);
  const std::size_t bitsPerChip = 3 + 7 + 32 * 4;
  CHECK(renderBits.size() == bitsPerChip * NUM_ACTIVE_CHIPS);
  for (int chip = 0; chip < NUM_ACTIVE_CHIPS; ++chip) {
    const std::size_t start = chip * bitsPerChip;
    CHECK(renderBits[start] == 1);
    CHECK(renderBits[start + 1] == 0);
    CHECK(renderBits[start + 2] == 1);
    CHECK(std::count(renderBits.begin() + start + 3,
                     renderBits.begin() + start + bitsPerChip, 1) == 0);
  }
  CHECK(countWrites(operations, chipSelect, GpioValue::low) ==
        NUM_ACTIVE_CHIPS);
  CHECK(countWrites(operations, selectClock, GpioValue::high) == 424);
}

void testLpd8806ProtocolAndPinReplacement() {
  const int data = 21;
  const int clock = 20;

  std::recursive_mutex gpioMutex;
  FakeGpio gpio(false);
  LPD8806 strip(&gpioMutex, gpio, 240, data, clock);
  strip.begin();

  std::vector<GpioOperation> operations = gpio.operations();
  CHECK(operations.size() > 2);
  CHECK(isOperation(operations[0], GpioOperationKind::configureOutput,
                    data, GpioValue::low));
  CHECK(isOperation(operations[1], GpioOperationKind::configureOutput,
                    clock, GpioValue::low));

  gpio.clearOperations();
  strip.setPixelColor(0, 1, 2, 3);
  strip.setPixelColor(239, 4, 5, 6);
  strip.show();
  operations = gpio.operations();

  const std::vector<int> wireBits = sampledBits(operations, data, clock);
  const std::size_t frameBits = 240 * 3 * 8;
  const std::size_t latchBits = ((240 + 31) / 32) * 8;
  CHECK(wireBits.size() == frameBits + latchBits);

  const std::vector<unsigned int> frame = bytesFromBits(
    std::vector<int>(wireBits.begin(), wireBits.begin() + frameBits));
  CHECK(frame.size() == 240 * 3);
  CHECK(frame[0] == 0x82);
  CHECK(frame[1] == 0x81);
  CHECK(frame[2] == 0x83);
  CHECK(std::count(frame.begin() + 3, frame.end() - 3, 0x80) ==
        static_cast<int>(frame.size() - 6));
  CHECK(frame[frame.size() - 3] == 0x85);
  CHECK(frame[frame.size() - 2] == 0x84);
  CHECK(frame[frame.size() - 1] == 0x86);
  CHECK(std::count(wireBits.begin() + frameBits, wireBits.end(), 0) ==
        static_cast<int>(latchBits));

  gpio.clearOperations();
  strip.updatePins(23, 24);
  operations = gpio.operations();
  CHECK(operations.size() > 4);
  CHECK(isOperation(operations[0], GpioOperationKind::configureInput, data));
  CHECK(isOperation(operations[1], GpioOperationKind::configureInput, clock));
  CHECK(isOperation(operations[2], GpioOperationKind::configureOutput,
                    23, GpioValue::low));
  CHECK(isOperation(operations[3], GpioOperationKind::configureOutput,
                    24, GpioValue::low));
}

void testMcp3002ProtocolAndCleanup() {
  const int clock = 17;
  const int digitalOut = 27;
  const int digitalIn = 22;
  const int chipSelect = 4;
  const int expectedValue = 0x2a6;

  std::recursive_mutex gpioMutex;
  FakeGpio gpio(false);
  {
    Mcp3002 adc(gpioMutex, gpio, clock, digitalOut, digitalIn, chipSelect);
    std::vector<GpioOperation> operations = gpio.operations();
    CHECK(operations.size() == 4);
    CHECK(isOperation(operations[0], GpioOperationKind::configureOutput,
                      clock, GpioValue::low));
    CHECK(isOperation(operations[1], GpioOperationKind::configureInput,
                      digitalOut));
    CHECK(isOperation(operations[2], GpioOperationKind::configureOutput,
                      chipSelect, GpioValue::high));
    CHECK(isOperation(operations[3], GpioOperationKind::configureOutput,
                      digitalIn, GpioValue::low));

    std::vector<GpioValue> inputBits;
    for (int bit = 9; bit >= 0; --bit) {
      inputBits.push_back(
        (expectedValue & (1 << bit)) ? GpioValue::high : GpioValue::low);
    }
    gpio.queueInputValues(digitalOut, inputBits);
    gpio.clearOperations();
    CHECK(adc.readAnalog(1) == expectedValue);
    operations = gpio.operations();

    CHECK(operations.size() > 4);
    CHECK(isOperation(operations[0], GpioOperationKind::write,
                      chipSelect, GpioValue::high));
    CHECK(isOperation(operations[1], GpioOperationKind::write,
                      clock, GpioValue::low));
    CHECK(isOperation(operations[2], GpioOperationKind::write,
                      chipSelect, GpioValue::low));
    CHECK(isOperation(operations.back(), GpioOperationKind::write,
                      chipSelect, GpioValue::high));

    const std::vector<int> clockedOutput =
      sampledBits(operations, digitalIn, clock);
    CHECK(clockedOutput.size() == 15);
    CHECK(clockedOutput[0] == 1);
    CHECK(clockedOutput[1] == 1);
    CHECK(clockedOutput[2] == 1);

    bool active = false;
    int reads = 0;
    for (const GpioOperation& operation : operations) {
      if (isOperation(operation, GpioOperationKind::write,
                      chipSelect, GpioValue::low)) {
        CHECK(!active);
        active = true;
      } else if (isOperation(operation, GpioOperationKind::write,
                             chipSelect, GpioValue::high) && active) {
        active = false;
      } else if (operation.kind == GpioOperationKind::read) {
        CHECK(active);
        CHECK(operation.bcmGpio == digitalOut);
        ++reads;
      }
    }
    CHECK(!active);
    CHECK(reads == 10);

    gpio.clearOperations();
  }

  const std::vector<GpioOperation> cleanup = gpio.operations();
  CHECK(cleanup.size() == 3);
  CHECK(isOperation(cleanup[0], GpioOperationKind::configureInput, clock));
  CHECK(isOperation(cleanup[1], GpioOperationKind::configureInput,
                    chipSelect));
  CHECK(isOperation(cleanup[2], GpioOperationKind::configureInput,
                    digitalIn));
}

void testMotionInputMapping() {
  FakeGpio gpio(false);
  configureMotionInput(gpio, 10);
  gpio.setInputValue(10, GpioValue::low);
  CHECK(!readMotionDetected(gpio, 10));
  gpio.setInputValue(10, GpioValue::high);
  CHECK(readMotionDetected(gpio, 10));

  const std::vector<GpioOperation> operations = gpio.operations();
  CHECK(operations.size() == 3);
  CHECK(isOperation(operations[0], GpioOperationKind::configureInput, 10));
  CHECK(isOperation(operations[1], GpioOperationKind::read, 10,
                    GpioValue::low));
  CHECK(isOperation(operations[2], GpioOperationKind::read, 10,
                    GpioValue::high));
}

void testTransactionsAreSerialized() {
  std::recursive_mutex gpioMutex;
  FakeGpio gpio(false);
  LPD8806 strip(&gpioMutex, gpio, 4, 21, 20);
  strip.begin();
  Mcp3002 adc(gpioMutex, gpio, 17, 27, 22, 4);
  gpio.queueInputValues(27, std::vector<GpioValue>(10, GpioValue::low));
  gpio.clearOperations();
  gpio.setYieldAfterOperation(true);

  std::atomic<bool> start(false);
  std::thread stripThread([&strip, &start]() {
    while (!start.load()) std::this_thread::yield();
    strip.show();
  });
  std::thread adcThread([&adc, &start]() {
    while (!start.load()) std::this_thread::yield();
    CHECK(adc.readAnalog(0) == 0);
  });
  start.store(true);
  stripThread.join();
  adcThread.join();
  gpio.setYieldAfterOperation(false);

  const std::vector<GpioOperation> operations = gpio.operations();
  int previousOwner = 0;
  int ownerChanges = 0;
  bool sawStrip = false;
  bool sawAdc = false;
  for (const GpioOperation& operation : operations) {
    int owner = 0;
    if (operation.bcmGpio == 20 || operation.bcmGpio == 21) {
      owner = 1;
      sawStrip = true;
    } else if (operation.bcmGpio == 4 || operation.bcmGpio == 17 ||
               operation.bcmGpio == 22 || operation.bcmGpio == 27) {
      owner = 2;
      sawAdc = true;
    }
    if (owner != 0 && previousOwner != 0 && owner != previousOwner) {
      ++ownerChanges;
    }
    if (owner != 0) previousOwner = owner;
  }
  CHECK(sawStrip);
  CHECK(sawAdc);
  CHECK(ownerChanges == 1);
}

}  // namespace

int main() {
  testFakeConfigurationAndHistory();
  testHt1632InitializationCommandsAndRender();
  testLpd8806ProtocolAndPinReplacement();
  testMcp3002ProtocolAndCleanup();
  testMotionInputMapping();
  testTransactionsAreSerialized();
  std::cout << "GPIO protocol tests passed\n";
  return 0;
}
