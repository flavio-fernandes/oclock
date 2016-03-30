#include <stdlib.h>
#include <string.h>

#include <mutex>

#include "stdTypes.h"

class LPD8806 {

 public:
  LPD8806(std::recursive_mutex* gpioLockMutexP, Int16U n, Int8U dpin, Int8U cpin); // Configurable pins
  ~LPD8806();
  void
    begin(),
    setPixelColor(Int16U n, Int8U r, Int8U g, Int8U b),
    setPixelColor(Int16U n, Int32U c),
    clearPixelColors(),
    getPixelColor(Int16U n, Int8U& r, Int8U& g, Int8U& b) const,
    show(),
    updatePins(Int8U dpin, Int8U cpin), // Change pins, configurable
    updateLength(Int16U n);               // Change strip length
  Int16U
    numPixels() const;
  static Int32U Color(Int8U r, Int8U g, Int8U b) /*const*/;
  Int32U getPixelColor(Int16U n) const;

  static const Int32U nullColor;
 private:
  std::recursive_mutex& gpioLockMutex;

  Int16U numLEDs;    // Number of RGB LEDs in strip (each led needs 3 bytes)
  Int16U largestChangedLed; // last LED changed after show().
  
  Int8U
    *pixels,    // Holds LED color values (3 bytes each) + latch bytes
    clkpin, datapin;     // Clock & data pin numbers

  void startBitbang() const;
  void _bitBangLatchSignal() const;
  bool begun;       // If 'true', begin() method was previously invoked

  // not implemented
  LPD8806() = delete;
  LPD8806(const LPD8806& other) = delete;
  LPD8806& operator=(const LPD8806& other) = delete;
};
