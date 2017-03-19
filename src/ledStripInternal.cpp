#ifdef FAKE_WIRING
#include "fakeWiringPi.h"
#else
#include <wiringPi.h>
#endif // ifdef FAKE_WIRING

#include <string>

#include "LPD8806.h"
#include "commonTypes.h"
#include "ledStripTypes.h"
#include "ledStripInternal.h"
#include "commonUtils.h"
#include "timerTick.h"

// Local types

class LedStripInternalInfo {
public:
  LedStripInternalInfo(LPD8806& lpd8806);
  ~LedStripInternalInfo();
  
  LPD8806& lpd8806;

  int currModeIndex;
  int secsUntilGoingToManualMode;  // -1 = never
  StringMap params;
  
private:

  LedStripInternalInfo() = delete;
  LedStripInternalInfo(const LedStripInternalInfo& other) = delete;
  LedStripInternalInfo& operator=(const LedStripInternalInfo& other) = delete;
};

// ----

// Forward declarations

static Int32U wheel(LPD8806& lpd8806, Int16U wheelPos);
static void clearPixelColorsAndShow(LPD8806& lpd8806);
static void checkIfModeTimedOut(LedStripInternalInfo& info);
static void changeLedStripMode(LedStripMode wantedLedStripMode, LedStripInternalInfo& ledStripInternalInfo, const StringMap& params);
static Int32U getPixelColorParam(const StringMap& params);

// ----

// Locals

typedef void (*modeFunctionPtr)(LedStripInternalInfo& info);

typedef struct {
  LedStripMode ledStripMode;
  const char* const ledStripModeStr;
  modeFunctionPtr handlerInit;
  modeFunctionPtr handlerTickFast;
  modeFunctionPtr handlerTick1Sec;
  modeFunctionPtr handlerTick10Sec;
  modeFunctionPtr handlerTick1Min;
} Mode;

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

// ledStripModeManual

static void modeManualInit(LedStripInternalInfo& info) {
  // FIXME: finish this!
  // What we need to do is to parse the "rawFormat" and "getPixelColorParam" info.params
}

static const Mode modeManual = { ledStripModeManual, "manual", modeManualInit /*init*/, 0 /*fast*/, 0 /*1sec*/, 0 /*10sec*/, 0 /*1min*/};

// ledStripModePastel

static void modePastelMain(LedStripInternalInfo& info) {
  static const int pastelShiftPixels = 1;
  static int currPixel = 0;

  Int8U r=63, g=63, b=127;
  int repeat = 0;
  LPD8806& lpd8806 = info.lpd8806;
  for (Int16U i=0; i < lpd8806.numPixels(); i++) {
    if (repeat < 63) {
      ++r;
    } else if (repeat < 63 * 2) {
      --b;
    } else if (repeat < 63 * 3) {
      ++g;
    } else {
      r=63; g=63; b=127;
      repeat = -1;
    }
    lpd8806.setPixelColor(currPixel, r, g, b);
    ++repeat;

    // wrap around
    if (++currPixel >= lpd8806.numPixels()) currPixel = 0;
  }
  
  lpd8806.show();

  // next time, start in a different location
  currPixel += pastelShiftPixels;
  if (currPixel >= lpd8806.numPixels()) currPixel = 0;
}

static void modePastelFast(LedStripInternalInfo& info) {
  if (isParamSet(info.params, ledStripParamExtra, "fast")) modePastelMain(info);
}

static void modePastel1Sec(LedStripInternalInfo& info) {
  if (isParamSet(info.params, ledStripParamExtra, "1sec")) modePastelMain(info);
}

static void modePastel10Sec(LedStripInternalInfo& info) {
  if (isParamSet(info.params, ledStripParamExtra, "10sec")) modePastelMain(info);
}

static void modePastel1Min(LedStripInternalInfo& info) {
  if (isParamSet(info.params, ledStripParamExtra, "1min")) modePastelMain(info);
}

static const Mode modePastel = { ledStripModePastel, "pastel", modePastelMain /*init*/,
				 modePastelFast /*fast*/, modePastel1Sec /*1sec*/, modePastel10Sec /*10sec*/, modePastel1Min /*1min*/};

// ledStripModeFill

static void modeFillInit(LedStripInternalInfo& info) {
  Int32U color =
    getPixelColorParam(info.params) == LPD8806::nullColor ? getRandomNumber(0x00fffffe) + 1 : getPixelColorParam(info.params);
  LPD8806& lpd8806 = info.lpd8806;
  for (Int16U i=0; i < lpd8806.numPixels(); i++) {
    lpd8806.setPixelColor(i, color);
    if (isParamSet(info.params, ledStripParamExtra, "randomColor")) color = getRandomNumber(0x00fffffe) + 1;
  }
  lpd8806.show();
}

static const Mode modeFill = { ledStripModeFill, "fill", modeFillInit /*init*/, 0 /*fast*/, 0 /*1sec*/, 0 /*10sec*/, 0 /*1min*/};

// ledStripModeRainbow

static void modeRainbowMain(LedStripInternalInfo& info) {
  static Int16U j = 0;

  LPD8806& lpd8806 = info.lpd8806; 
  for (Int16U i=0; i < lpd8806.numPixels(); i++) {
    // tricky math! we use each pixel as a fraction of the full 384-color
    // wheel (thats the i / lpd8806.numPixels() part)
    // Then add in j which makes the colors go around per pixel
    // the % 384 is to make the wheel cycle around
    lpd8806.setPixelColor(i, wheel(lpd8806, ((i * 384 / lpd8806.numPixels()) + j) % 384));
  }
  lpd8806.show();   // write all the pixels out

  if (++j >= 384) j = 0;
}

static void modeRainbowFast(LedStripInternalInfo& info) {
  if (isParamSet(info.params, ledStripParamExtra, "fast")) modeRainbowMain(info);
}

static void modeRainbow1sec(LedStripInternalInfo& info) {
  if (isParamSet(info.params, ledStripParamExtra, "1sec")) modeRainbowMain(info);
}

static void modeRainbow10sec(LedStripInternalInfo& info) {
  if (isParamSet(info.params, ledStripParamExtra, "10sec")) modeRainbowMain(info);
}

static const Mode modeRainbow = {ledStripModeRainbow, "rainbow", modeRainbowMain /*init*/,
				 modeRainbowFast, modeRainbow1sec , modeRainbow10sec,
				 modeRainbowMain /*1min*/};


// ledStripModeScan

static void modeScanMain(LedStripInternalInfo& info) {
  static Int32U color;
  static int i = 0;
  static int i_increment = 1;
  static const int ledsVisitedPerFrame = 11;
  
  LPD8806& lpd8806 = info.lpd8806; 
  
  int ledsVisited = 0;

  if (i == 0) {
    color = getPixelColorParam(info.params) == LPD8806::nullColor ? getRandomNumber(0x00fffffe) + 1 : getPixelColorParam(info.params);
  }
  
  while (true) {
      lpd8806.setPixelColor(i, color); // set one pixel
      lpd8806.show();              // refresh led strip
      lpd8806.setPixelColor(i, 0); // erase pixel (but don't refresh yet)

      i += i_increment;

      if (i == -1) {
	i = 0;
	i_increment = 1;
	clearPixelColorsAndShow(lpd8806);
	break;
      }
      if (i == lpd8806.numPixels()) { i -= 2; i_increment = -1; }

      if (++ledsVisited > ledsVisitedPerFrame) break;
      delay(TimerTick::millisPerTick / ledsVisitedPerFrame);
  }
}

static void modeScanFast(LedStripInternalInfo& info) {
  if (!isParamSet(info.params, ledStripParamExtra) || isParamSet(info.params, ledStripParamExtra, "")) modeScanMain(info);
}

static void modeScan1Sec(LedStripInternalInfo& info) {
  if (isParamSet(info.params, ledStripParamExtra, "1sec")) modeScanMain(info);
}

static void modeScan10Sec(LedStripInternalInfo& info) {
  if (isParamSet(info.params, ledStripParamExtra, "10sec")) modeScanMain(info);
}

static void modeScan1Min(LedStripInternalInfo& info) {
  if (isParamSet(info.params, ledStripParamExtra, "1min")) modeScanMain(info);
}

static const Mode modeScan = {ledStripModeScan, "scan", 0 /*init*/, modeScanFast /*fast*/, modeScan1Sec /*1sec*/, modeScan10Sec /*10sec*/, modeScan1Min /*1min*/};


// ledStripModeBinaryCounter

static void modeBinaryCounterMain(LedStripInternalInfo& info) {
  static Int64U currentCounterValue = 0;

  LPD8806& lpd8806 = info.lpd8806;

  // check and see if largest pixel is 'off'. We use that to determine if we need to reset
  // the value of static currentCounterValue or not. The trick here is based on the fact that
  // all pixels are cleared when the led strip 'mode' changes, so we will use that to our
  // advantage.
  const Int32U currentLastPixelColor = lpd8806.getPixelColor( lpd8806.numPixels() - 1 );
  if (currentLastPixelColor == 0) {
    currentCounterValue = 0;

    // if an extra param was given, try to use it as starting value
    try {
      std::string initialValueStr;
      if (getParamValue(info.params, ledStripParamExtra, initialValueStr)) {
	currentCounterValue = std::stoll(initialValueStr, nullptr, 0 /*auto*/);
      }
    } catch(...) { }

    // set largest pixel to indicate counter should not reset in the next iteration
    lpd8806.clearPixelColors();
    lpd8806.setPixelColor(lpd8806.numPixels() - 1, LPD8806::Color(1,1,1));
  } else {
    // if we made it here, then we know that largest pixel is set, and we can keep counting up
    ++currentCounterValue;
  }

  const Int32U color = getPixelColorParam(info.params) == LPD8806::nullColor ? getRandomNumber(0x00fffffe) + 1 : getPixelColorParam(info.params);
  Int64U tmpCounterValue = currentCounterValue;

  // visit all pixels (except for the last one) as bits in the value of our counter
  for (Int16U pixel=0; pixel < (lpd8806.numPixels() - 1) && tmpCounterValue; ++pixel) {
    if ( (tmpCounterValue & 0x1) ) {
      lpd8806.setPixelColor(pixel, color);  // bit represented by this pixel needs to be set
    } else {
      lpd8806.setPixelColor(pixel, 0);  // bit represented by this pixel needs to be cleared
    }
    tmpCounterValue >>= 1; // shift bits of temporary value (same as dividing it's value by 2)
  }

  lpd8806.show();
}

static void modeBinaryCounterFast(LedStripInternalInfo& info) {
  if (isParamSet(info.params, ledStripParamExtra, "fast")) modeBinaryCounterMain(info);
}

static const Mode modeBinaryCounter = {ledStripModeBinaryCounter, "binCnt", 0 /*init*/,
				       modeBinaryCounterFast /*fast*/,
				       modeBinaryCounterMain /*1sec*/, 0 /*10sec*/, 0 /*1min*/};

// ======================================================================

static Int32U wheel(LPD8806& lpd8806, Int16U wheelPos) {
  Int8U r, g, b;
  switch(wheelPos / 128)
  {
    case 0:
      r = 127 - wheelPos % 128; // red down
      g = wheelPos % 128;       // green up
      b = 0;                    // blue off
      break;
    case 1:
      g = 127 - wheelPos % 128; // green down
      b = wheelPos % 128;       // blue up
      r = 0;                    // red off
      break;
    case 2:
      b = 127 - wheelPos % 128; // blue down
      r = wheelPos % 128;       // red up
      g = 0;                    // green off
      break;
  }
  return(lpd8806.Color(r,g,b));
}

static void clearPixelColorsAndShow(LPD8806& lpd8806) {
  lpd8806.clearPixelColors();
  lpd8806.show();
}

static void checkIfModeTimedOut(LedStripInternalInfo& info) {
  if (info.secsUntilGoingToManualMode < 0) return;

  if (--info.secsUntilGoingToManualMode < 0) {
    // mode hit timeout: go to manual and explictly request all leds to be turned off
    StringMap params;
    params[ledStripParamClearAllPixels] = "yes";
    changeLedStripMode(ledStripModeManual, info, params);
  }
}


// ======================================================================

static const Mode allModes[] = { modeManual, modePastel, modeFill, modeRainbow, modeScan, modeBinaryCounter };
static const int allModesCount = sizeof(allModes) / sizeof(allModes[0]);

static LedStripMode getLedStripMode(const LedStripInternalInfo& info) {
  return allModes[ info.currModeIndex ].ledStripMode;
} 

static void changeLedStripMode(LedStripMode wantedLedStripMode, LedStripInternalInfo& ledStripInternalInfo, const StringMap& params) {
  const LedStripMode currentLedStripMode = getLedStripMode(ledStripInternalInfo);
  
  for (int i=0; i < allModesCount; ++i) {
    if (allModes[i].ledStripMode == wantedLedStripMode) {

      std::string clearAlPixelsValue;
      const bool shouldClearAlPixels =
	getParamValue(params, ledStripParamClearAllPixels, clearAlPixelsValue) &&
	!clearAlPixelsValue.empty() &&
	parseBooleanValue(clearAlPixelsValue.c_str());

      // Clear strip only if mode is changing, or we were explicitly asked to do so
      if (currentLedStripMode != wantedLedStripMode || shouldClearAlPixels) {
	LPD8806& lpd8806 = ledStripInternalInfo.lpd8806;
	clearPixelColorsAndShow(lpd8806);
      }

      std::string timeOutValue;
      if (getParamValue(params, ledStripParamTimeout, timeOutValue) && !timeOutValue.empty()) {
	// ref: http://www.cplusplus.com/reference/string/stoi/
	try {
	  ledStripInternalInfo.secsUntilGoingToManualMode = std::stoi(timeOutValue, nullptr, 0 /*auto*/);
	} catch(...) {
	  ledStripInternalInfo.secsUntilGoingToManualMode = 0;  // bad value: less than a second
	}
      } else {
	ledStripInternalInfo.secsUntilGoingToManualMode = -1;  // never
      }
      ledStripInternalInfo.currModeIndex = i;
      ledStripInternalInfo.params = params;
      
      if (allModes[i].handlerInit != nullptr) (*(allModes[i].handlerInit))(ledStripInternalInfo);
      break;
    }
  }
}

static Int32U getPixelColorParam(const StringMap& params) {
  std::string wantedRedStr;
  std::string wantedGreenStr;
  std::string wantedBlueStr;

  if (!getParamValue(params, "red", wantedRedStr) || wantedRedStr.empty()) wantedRedStr = "0";
  if (!getParamValue(params, "green", wantedGreenStr) || wantedGreenStr.empty()) wantedGreenStr = "0";
  if (!getParamValue(params, "blue", wantedBlueStr) || wantedBlueStr.empty()) wantedBlueStr = "0";

  try {
    const Int8U red = (Int8U) std::stoi(wantedRedStr, nullptr, 0 /*auto*/);
    const Int8U green = (Int8U) std::stoi(wantedGreenStr, nullptr, 0 /*auto*/);
    const Int8U blue = (Int8U) std::stoi(wantedBlueStr, nullptr, 0 /*auto*/);

    return LPD8806::Color(red, green, blue);
  } catch(...) { }
  return LPD8806::nullColor;;
}


// ----------------------------------------
// ----------------------------------------

LedStripInternalInfo::LedStripInternalInfo(LPD8806& lpd8806) :
  lpd8806(lpd8806), currModeIndex(ledStripModeManual), secsUntilGoingToManualMode(-1), params() {
  lpd8806.begin();
  lpd8806.show();
}

LedStripInternalInfo::~LedStripInternalInfo() {
  clearPixelColorsAndShow(lpd8806);
  params.clear();
}

// --

void LedStripInternal::fastTick() {
  if (allModes[ info->currModeIndex ].handlerTickFast != nullptr) (*(allModes[ info->currModeIndex ].handlerTickFast))(*info);
}
void LedStripInternal::tick1sec() {
  if (allModes[ info->currModeIndex ].handlerTick1Sec != nullptr) (*(allModes[ info->currModeIndex ].handlerTick1Sec))(*info);

  checkIfModeTimedOut(*info);
}
void LedStripInternal::tick10sec() {
  if (allModes[ info->currModeIndex ].handlerTick10Sec != nullptr) (*(allModes[ info->currModeIndex ].handlerTick10Sec))(*info);
}
void LedStripInternal::tick1min() {
  if (allModes[ info->currModeIndex ].handlerTick1Min != nullptr) (*(allModes[ info->currModeIndex ].handlerTick1Min))(*info);
}

void LedStripInternal::doHandleModePost(const StringMap& postValues) {
  // extract mode from postValues
  std::string wantedModeStr;
  if (!getParamValue(postValues, ledStripParamLedStripMode, wantedModeStr) || wantedModeStr.empty()) return;
  try {
    const LedStripMode ledStripMode = (LedStripMode) std::stoi(wantedModeStr, nullptr, 10);
    changeLedStripMode(ledStripMode, *info, postValues);
  } catch(...) { }
}

const char* LedStripInternal::getLedStripModeStr() const {
  return allModes[ info->currModeIndex ].ledStripModeStr;
}

const char* LedStripInternal::getLedStripModeStr(LedStripMode ledStripMode) const {
  for (int i=0; i < allModesCount; ++i) {
    if (allModes[i].ledStripMode == ledStripMode) return allModes[i].ledStripModeStr;
  }
  return ""; // not found
}

LedStripInternal::LedStripInternal(LPD8806& lpd8806) {
  info = new LedStripInternalInfo(lpd8806);

  StringMap initialParams;
  initialParams[ledStripParamTimeout] = "3";  // seconds
  changeLedStripMode(ledStripModeRainbow, *info, initialParams);
}

LedStripInternal::~LedStripInternal() {
  delete info;
  // info = 0;
}
