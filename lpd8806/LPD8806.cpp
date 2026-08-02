#include "LPD8806.h"

#include "gpio/Gpio.h"
#include "spi/SpiOutput.h"

#include <stdexcept>
#include <vector>

#define BYTES_PER_LED 3

const Int32U LPD8806::nullColor = LPD8806::Color(0,0,0);

/*****************************************************************************/

// Constructor for use with arbitrary clock/data pins:
LPD8806::LPD8806(std::recursive_mutex* gpioLockMutexP, Gpio& gpio,
                 Int16U n, Int8U dpin, Int8U cpin) :
  gpioLockMutex(*gpioLockMutexP), gpio(gpio), spiOutput(NULL), numLEDs(0),
  largestChangedLed(0), pixels(0), clkpin(0), datapin(0), begun(false) {
  updateLength(n);
  updatePins(dpin, cpin);
}

LPD8806::LPD8806(std::recursive_mutex* gpioLockMutexP, Gpio& gpio,
                 SpiOutput& spiOutputParam, Int16U n) :
  gpioLockMutex(*gpioLockMutexP), gpio(gpio), spiOutput(&spiOutputParam),
  numLEDs(0), largestChangedLed(0), pixels(0), clkpin(0), datapin(0),
  begun(false) {
  updateLength(n);
}

LPD8806::~LPD8806() {
  updateLength(0);
}

void LPD8806::begin() {
  if (spiOutput != NULL) {
    std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);
    transferSpiLatch();
  } else {
    startBitbang();
  }
  begun = true;
}

// Change pin assignments post-constructor, using arbitrary pins:
void LPD8806::updatePins(Int8U dpin, Int8U cpin) {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  if (spiOutput != NULL)
    throw std::logic_error("LPD8806 SPI output has no configurable GPIO pins");

  if (begun) { // If begin() was previously invoked...
    gpio.configureInput(datapin); // Restore prior data and clock pins to inputs
    gpio.configureInput(clkpin);
  }
  datapin = dpin;
  clkpin = cpin;

  // If previously begun, enable 'soft' SPI outputs now
  if (begun) startBitbang();
}

// Enable software SPI pins and issue initial latch:
void LPD8806::startBitbang() const {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  gpio.configureOutput(datapin, GpioValue::low);
  gpio.configureOutput(clkpin, GpioValue::low);

  _bitBangLatchSignal();
}

void LPD8806::_bitBangLatchSignal() const {
  // std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  gpio.write(datapin, GpioValue::low);
  for (Int16U i=((numLEDs+31)/32)*8; i>0; --i) {
    gpio.write(clkpin, GpioValue::high);
    gpio.write(clkpin, GpioValue::low);
  }
}

std::size_t LPD8806::latchByteCount() const {
  return (static_cast<std::size_t>(numLEDs) + 31) / 32;
}

void LPD8806::transferSpiLatch() const {
  const std::vector<Int8U> latch(latchByteCount(), 0);
  if (!latch.empty()) spiOutput->transfer(&latch[0], latch.size());
}

// Change strip length (see notes with empty constructor, above):
void LPD8806::updateLength(Int16U n) {
  numLEDs = 0; largestChangedLed = 0;
  if (pixels) free(pixels); // Free existing data (if any)

  const int dataBytes = n * BYTES_PER_LED;
  if (n > 0 && (pixels = (Int8U *) malloc(dataBytes))) { // Alloc new data
    numLEDs = n;
    memset(pixels, 0x80, dataBytes);  // Init to RGB 'off' state
    largestChangedLed = numLEDs - 1;
  }
  // 'begun' state does not change -- pins retain prior modes
}

Int16U LPD8806::numPixels() const {
  return numLEDs;
}

void LPD8806::show() {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  if (numLEDs == 0) return;

  if (spiOutput != NULL) {
    const std::size_t dataBytes =
        static_cast<std::size_t>(numPixels()) * BYTES_PER_LED;
    std::vector<Int8U> frame(pixels, pixels + dataBytes);
    frame.resize(dataBytes + latchByteCount(), 0);
    spiOutput->transfer(&frame[0], frame.size());
    largestChangedLed = 0;
    return;
  }

  Int8U  *ptr = pixels;
#if 0
  Int16U i    = (largestChangedLed + 1) * BYTES_PER_LED;
#else
  Int16U i    = numPixels() * BYTES_PER_LED;
#endif
  Int8U p, bit;
  int currDatapinValue = ~0;

  while (i--) {
    p = *ptr++;

    for (bit=0x80; bit; bit >>= 1) {
      if (p & bit) {
	if (currDatapinValue != 1) {
	  gpio.write(datapin, GpioValue::high);
	  currDatapinValue = 1;
	}
      } else {
	if (currDatapinValue != 0) {
	  gpio.write(datapin, GpioValue::low);
	  currDatapinValue = 0;
	}
      }

      gpio.write(clkpin, GpioValue::high);
      gpio.write(clkpin, GpioValue::low);
    }
  }
  _bitBangLatchSignal();

  largestChangedLed = 0;  // reset cached value
}

// Convert separate R,G,B into combined 32-bit GRB color:
Int32U LPD8806::Color(Int8U r, Int8U g, Int8U b) /*const*/ {
  return ((Int32U)(g | 0x80) << 16) |
         ((Int32U)(r | 0x80) <<  8) |
                     b | 0x80 ;
}

// Set pixel color from separate 7-bit R, G, B components:
void LPD8806::setPixelColor(Int16U n, Int8U r, Int8U g, Int8U b) {
  if (n < numLEDs) { // Arrays are 0-indexed, thus NOT '<='
    Int8U *p = &pixels[n * BYTES_PER_LED];
    *p++ = g | 0x80; // Strip color order is GRB,
    *p++ = r | 0x80; // not the more common RGB,
    *p++ = b | 0x80; // so the order here is intentional; don't "fix"

    if (n > largestChangedLed) largestChangedLed = n;
  }
}

// Set pixel color from 'packed' 32-bit GRB (not RGB) value:
void LPD8806::setPixelColor(Int16U n, Int32U c) {
  if (n < numLEDs) { // Arrays are 0-indexed, thus NOT '<='
    Int8U *p = &pixels[n * BYTES_PER_LED];
    *p++ = (c >> 16) | 0x80;
    *p++ = (c >>  8) | 0x80;
    *p++ =  c        | 0x80;

    if (n > largestChangedLed) largestChangedLed = n;
  }
}

void LPD8806::clearPixelColors() {
  if (numLEDs == 0 || pixels == nullptr) {
    largestChangedLed = 0;
    return;
  }
  const int dataBytes = numLEDs * BYTES_PER_LED;
  memset(pixels, 0x80, dataBytes);
  largestChangedLed = numLEDs - 1;
}

void LPD8806::delayMilliseconds(unsigned int duration) const {
  gpio.delayMilliseconds(duration);
}

// Query color from previously-set pixel (returns packed 32-bit GRB value)
Int32U LPD8806::getPixelColor(Int16U n) const {
  if(n < numLEDs) {
    Int16U ofs = n * BYTES_PER_LED;
    return ((Int32U)(pixels[ofs    ] & 0x7f) << 16) |
           ((Int32U)(pixels[ofs + 1] & 0x7f) <<  8) |
            (Int32U)(pixels[ofs + 2] & 0x7f);
  }

  return 0; // Pixel # is out of bounds
}

void LPD8806::getPixelColor(Int16U n, Int8U& r, Int8U& g, Int8U& b) const {
  if (n < numLEDs) {
    Int8U *p = &pixels[n * BYTES_PER_LED];
    g = *p++ & 0x7f; // Strip color order is GRB, 
    r = *p++ & 0x7f; // not the more common RGB, 
    b = *p++ & 0x7f; // so the order here is intentional; don't "fix"
  } else {
    r = g = b = 0; // Pixel # is out of bounds
  }
}
