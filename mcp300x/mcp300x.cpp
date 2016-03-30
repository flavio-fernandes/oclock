#ifdef FAKE_WIRING
#include "fakeWiringPi.h"
#else
#include <wiringPi.h>
#endif // ifdef FAKE_WIRING

#include "mcp300x.h"

Mcp300x::Mcp300x(std::recursive_mutex& gpioLockMutex, int pinClock, int pinDigitalOut, int pinDigitalIn, int pinChipSelect) :
  gpioLockMutex(gpioLockMutex),
  pinClock(pinClock), pinDigitalOut(pinDigitalOut), pinDigitalIn(pinDigitalIn), pinChipSelect(pinChipSelect) {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  pinMode(pinClock, OUTPUT);
  pinMode(pinDigitalOut, INPUT);
  pinMode(pinChipSelect, OUTPUT);
  pinMode(pinDigitalIn, OUTPUT);
}

Mcp300x::~Mcp300x() {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  // nitpick: restore pins as input
  pinMode(pinClock, INPUT);
  // pinMode(pinDigitalOut, INPUT);
  pinMode(pinChipSelect, INPUT);
  pinMode(pinDigitalIn, INPUT);
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
  digitalWrite(pinChipSelect, HIGH);
  digitalWrite(pinClock, LOW);
  digitalWrite(pinChipSelect, LOW);
  
  for (int i = 0; i < cmdOutBits; ++i) {
    digitalWrite(pinDigitalIn, (cmdOut & 0x80) ? HIGH : LOW);
    cmdOut <<= 1; // shift out bit just used
    _tickClock();
  }

  _tickClock(); // read (skip) one empty bit  

  // read 10 ADC bits
  for (int i = 0; i < 10; ++i) {
    _tickClock();
    valueOut <<= 1; // make room for next bit
    if (digitalRead(pinDigitalOut) == HIGH) {
      valueOut |= 1;
    }
  }

  _tickClock(); // read (skip) null bit
  digitalWrite(pinChipSelect, HIGH);
  
  return valueOut;
}

void Mcp300x::_tickClock() const {

  digitalWrite(pinClock, HIGH);
  digitalWrite(pinClock, LOW);
}
