#include "mcp300x.h"
#include "gpio/Gpio.h"

Mcp300x::Mcp300x(std::recursive_mutex& gpioLockMutex, Gpio& gpio, int pinClock,
                 int pinDigitalOut, int pinDigitalIn, int pinChipSelect) :
  gpioLockMutex(gpioLockMutex), gpio(gpio),
  pinClock(pinClock), pinDigitalOut(pinDigitalOut), pinDigitalIn(pinDigitalIn), pinChipSelect(pinChipSelect) {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  gpio.configureOutput(pinClock, GpioValue::low);
  gpio.configureInput(pinDigitalOut);
  gpio.configureOutput(pinChipSelect, GpioValue::high);
  gpio.configureOutput(pinDigitalIn, GpioValue::low);
}

Mcp300x::~Mcp300x() {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  // nitpick: restore pins as input
  gpio.configureInput(pinClock);
  // pinDigitalOut is already an input.
  gpio.configureInput(pinChipSelect);
  gpio.configureInput(pinDigitalIn);
}

int Mcp300x::readAnalog(int pinChannel) const {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  Int8U cmdOut;
  int cmdOutBits;
  int valueOut = 0;
  
  if (pinChannel < 0 || pinChannel > lastChannelPin()) {
    return -1;
  }

  // obtain command out bits + pinChannel, based on chip
  getCmdOutInfo(pinChannel, cmdOut, cmdOutBits);

  // initiate communication with device
  // toggle cs and start clock low  
  gpio.write(pinChipSelect, GpioValue::high);
  gpio.write(pinClock, GpioValue::low);
  gpio.write(pinChipSelect, GpioValue::low);
  
  for (int i = 0; i < cmdOutBits; ++i) {
    gpio.write(pinDigitalIn,
               (cmdOut & 0x80) ? GpioValue::high : GpioValue::low);
    cmdOut <<= 1; // shift out bit just used
    _tickClock();
  }

  _tickClock(); // read (skip) one empty bit  

  // read 10 ADC bits
  for (int i = 0; i < 10; ++i) {
    _tickClock();
    valueOut <<= 1; // make room for next bit
    if (gpio.read(pinDigitalOut) == GpioValue::high) {
      valueOut |= 1;
    }
  }

  _tickClock(); // read (skip) null bit
  gpio.write(pinChipSelect, GpioValue::high);
  
  return valueOut;
}

void Mcp300x::_tickClock() const {

  gpio.write(pinClock, GpioValue::high);
  gpio.write(pinClock, GpioValue::low);
}
