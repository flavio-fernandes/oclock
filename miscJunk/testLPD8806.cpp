
#include <math.h>
#include <random>

#ifdef FAKE_WIRING
#include "fakeWiringPi.h"
#else
#include <wiringPi.h>
#endif // ifdef FAKE_WIRING

#include "threadsMain.h"
#include "timerTick.h"
#include "LPD8806.h"

#include "stdTypes.h"
#include "inbox.h"

// Example to control LPD8806-based RGB LED Modules in a strip!
// NOTE: WILL NOT WORK ON TRINKET OR GEMMA due to floating-point math
/*****************************************************************************/

static int currStripMode = 0;

const int dataPin = 21;   // 2;
const int clockPin = 20;  // 3;

// Set the first variable to the NUMBER of pixels. 32 = 32 pixels in a row
// The LED strips are 32 LEDs per meter but you can extend/cut the strip
LPD8806* stripP = 0;

static void setup() {

  // Start up the LED strip
  stripP->begin();

  // Update the strip, to start they are all 'off'
  stripP->show();
}

// function prototypes, do not remove these!
void colorChase(Int32U c, Int8U wait);
void colorWipe(Int32U c, Int8U wait);
void dither(Int32U c, Int8U wait);
void scanner(Int8U r, Int8U g, Int8U b, Int8U wait);
void wave(Int32U c, int cycles, Int8U wait);
void rainbowCycle(Int8U wait);
Int32U Wheel(Int16U WheelPos);

// http://en.cppreference.com/w/cpp/numeric/random
// Choose a random mean between 1 and 0xffffff
std::random_device randomDevice;
std::default_random_engine randomEngine(randomDevice());
std::uniform_int_distribution<Int32U> uniform_dist(1, 0x00ffffff);

void scannerFrame() {
  const Int32U c = uniform_dist(randomEngine);
  int i = 0;
  int i_increment = 1;
  
  while (true) {
      stripP->setPixelColor(i, c); // set one pixel
      stripP->show();              // refresh strip display
      stripP->setPixelColor(i, 0); // erase pixel (but don't refresh yet)

      i += i_increment;
      if (i == -1) break;
      if (i == stripP->numPixels()) { i -= 2; i_increment = -1; }
      delay(2);
  }
}

void colorChaseFrame() {
  static Int32U c = 0x5d; // up to 127
  const int shiftBits = 2;
  
  for (int i=0; i < stripP->numPixels(); i++) {
      stripP->setPixelColor(i, c); // set one pixel
      stripP->show();              // refresh strip display
      stripP->setPixelColor(i, 0); // erase pixel (but don't refresh yet)
      delay(4);               // hold image for a moment
  }

  c = (c << shiftBits) & 0xffffff;
  if (c == 0) c = 127;
}

void rainbowCycleFrame() {
  static Int16U j = 0;
  Int16U i;

  for (i=0; i < stripP->numPixels(); i++) {
    // tricky math! we use each pixel as a fraction of the full 384-color
    // wheel (thats the i / stripP->numPixels() part)
    // Then add in j which makes the colors go around per pixel
    // the % 384 is to make the wheel cycle around
    stripP->setPixelColor(i, Wheel(((i * 384 / stripP->numPixels()) + j) % 384));
  }
  stripP->show();   // write all the pixels out

  if (++j >= 384) j = 0;
}

void doStripTick() {

  switch (currStripMode) {
  case 0:
    scannerFrame(); break;
  case 1:
    colorChaseFrame(); break;
  default:
    rainbowCycleFrame();
  }
}


/* Helper functions */

//Input a value 0 to 384 to get a color value.
//The colours are a transition r - g - b - back to r

Int32U Wheel(Int16U WheelPos)
{
  Int8U r, g, b;
  switch(WheelPos / 128)
  {
    case 0:
      r = 127 - WheelPos % 128; // red down
      g = WheelPos % 128;       // green up
      b = 0;                    // blue off
      break;
    case 1:
      g = 127 - WheelPos % 128; // green down
      b = WheelPos % 128;       // blue up
      r = 0;                    // red off
      break;
    case 2:
      b = 127 - WheelPos % 128; // blue down
      r = WheelPos % 128;       // red up
      g = 0;                    // green off
      break;
  }
  return(stripP->Color(r,g,b));
}

void lpd8806Main(const ThreadParam& threadParam) {
  // Set the first variable to the NUMBER of pixels. 32 = 32 pixels in a row
  // The LED strips are 32 LEDs per meter but you can extend/cut the strip
  stripP = new LPD8806(threadParam.gpioLockMutexP, 240, dataPin, clockPin);
  
  TimerTickServiceCv refreshTimeService(TimerTick::millisPerTick);
  TimerTick& timerTick = TimerTick::bind();
  timerTick.registerTimerTickService(refreshTimeService);

  InboxRegistry& inboxRegistry = InboxRegistry::bind();
  Inbox& inbox = inboxRegistry.getInbox(threadIdLpd8806);
  InboxMsg msg;

  setup();
  for (;;) {
    if (inbox.getMessage(msg)) {
      if (msg.inboxMsgType == inboxMsgTypeTerminate) break;
      if (msg.inboxMsgType == InboxMsgTypeMotionOn) {
	currStripMode = (currStripMode + 1) % 3;
	stripP->clearPixelColors();
      }
    }

    refreshTimeService.wait();
    doStripTick();
  }

  stripP->clearPixelColors();
  stripP->show();
  
  timerTick.unregisterTimerTickService(refreshTimeService.getCookie());
  
  delete stripP;
  stripP = 0;
}
