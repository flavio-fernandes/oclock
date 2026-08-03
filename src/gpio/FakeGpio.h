#ifndef OCLOCK_GPIO_FAKE_GPIO_H
#define OCLOCK_GPIO_FAKE_GPIO_H

#include <deque>
#include <map>
#include <mutex>
#include <vector>

#include "gpio/Gpio.h"

enum class GpioOperationKind {
  initialize,
  configureInput,
  configureOutput,
  read,
  write,
  delayMilliseconds
};

struct GpioOperation {
  GpioOperationKind kind;
  int bcmGpio;
  GpioValue value;
  unsigned int duration;
};

class FakeGpio : public Gpio {
public:
  explicit FakeGpio(bool realTimeDelays = true);

  virtual bool initialize();
  virtual void configureInput(int bcmGpio);
  virtual void configureOutput(int bcmGpio, GpioValue initialValue);
  virtual GpioValue read(int bcmGpio);
  virtual void write(int bcmGpio, GpioValue value);
  virtual void delayMilliseconds(unsigned int duration);

  void setInitializeResult(bool result);
  void setInputValue(int bcmGpio, GpioValue value);
  void queueInputValues(int bcmGpio,
                        const std::vector<GpioValue>& values);
  void setYieldAfterOperation(bool enabled);

  std::vector<GpioOperation> operations() const;
  void clearOperations();

private:
  mutable std::mutex mutex;
  std::map<int, GpioValue> inputValues;
  std::map<int, std::deque<GpioValue> > queuedInputValues;
  std::vector<GpioOperation> operationHistory;
  bool initializeResult;
  bool realTimeDelays;
  bool yieldAfterOperation;

  bool record(const GpioOperation& operation);

  FakeGpio(const FakeGpio&) = delete;
  FakeGpio& operator=(const FakeGpio&) = delete;
};

#endif
