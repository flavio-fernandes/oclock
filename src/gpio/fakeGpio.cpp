#include "gpio/FakeGpio.h"

#include <chrono>
#include <thread>

FakeGpio::FakeGpio(bool realTimeDelaysParam) :
  initializeResult(true), realTimeDelays(realTimeDelaysParam),
  yieldAfterOperation(false) {
}

bool FakeGpio::record(const GpioOperation& operation) {
  std::lock_guard<std::mutex> guard(mutex);
  operationHistory.push_back(operation);
  return yieldAfterOperation;
}

bool FakeGpio::initialize() {
  bool result;
  bool shouldYield;
  {
    std::lock_guard<std::mutex> guard(mutex);
    operationHistory.push_back(
      {GpioOperationKind::initialize, -1, GpioValue::low, 0});
    result = initializeResult;
    shouldYield = yieldAfterOperation;
  }
  if (shouldYield) std::this_thread::yield();
  return result;
}

void FakeGpio::configureInput(int bcmGpio) {
  if (record({GpioOperationKind::configureInput, bcmGpio,
              GpioValue::low, 0})) {
    std::this_thread::yield();
  }
}

void FakeGpio::configureOutput(int bcmGpio, GpioValue initialValue) {
  if (record({GpioOperationKind::configureOutput, bcmGpio,
              initialValue, 0})) {
    std::this_thread::yield();
  }
}

GpioValue FakeGpio::read(int bcmGpio) {
  GpioValue value = GpioValue::low;
  bool shouldYield;
  {
    std::lock_guard<std::mutex> guard(mutex);
    std::deque<GpioValue>& queuedValues = queuedInputValues[bcmGpio];
    if (!queuedValues.empty()) {
      value = queuedValues.front();
      queuedValues.pop_front();
    } else {
      const std::map<int, GpioValue>::const_iterator configured =
        inputValues.find(bcmGpio);
      if (configured != inputValues.end()) value = configured->second;
    }
    operationHistory.push_back(
      {GpioOperationKind::read, bcmGpio, value, 0});
    shouldYield = yieldAfterOperation;
  }
  if (shouldYield) std::this_thread::yield();
  return value;
}

void FakeGpio::write(int bcmGpio, GpioValue value) {
  if (record({GpioOperationKind::write, bcmGpio, value, 0})) {
    std::this_thread::yield();
  }
}

void FakeGpio::delayMilliseconds(unsigned int duration) {
  const bool shouldYield =
    record({GpioOperationKind::delayMilliseconds, -1,
            GpioValue::low, duration});
  if (realTimeDelays) {
    std::this_thread::sleep_for(std::chrono::milliseconds(duration));
  }
  if (shouldYield) std::this_thread::yield();
}

void FakeGpio::setInitializeResult(bool result) {
  std::lock_guard<std::mutex> guard(mutex);
  initializeResult = result;
}

void FakeGpio::setInputValue(int bcmGpio, GpioValue value) {
  std::lock_guard<std::mutex> guard(mutex);
  inputValues[bcmGpio] = value;
}

void FakeGpio::queueInputValues(
    int bcmGpio, const std::vector<GpioValue>& values) {
  std::lock_guard<std::mutex> guard(mutex);
  std::deque<GpioValue>& queuedValues = queuedInputValues[bcmGpio];
  queuedValues.insert(queuedValues.end(), values.begin(), values.end());
}

void FakeGpio::setYieldAfterOperation(bool enabled) {
  std::lock_guard<std::mutex> guard(mutex);
  yieldAfterOperation = enabled;
}

std::vector<GpioOperation> FakeGpio::operations() const {
  std::lock_guard<std::mutex> guard(mutex);
  return operationHistory;
}

void FakeGpio::clearOperations() {
  std::lock_guard<std::mutex> guard(mutex);
  operationHistory.clear();
}

std::unique_ptr<Gpio> createGpio() {
  return std::unique_ptr<Gpio>(new FakeGpio());
}
