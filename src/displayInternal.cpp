#include "displayTypes.h"
#include "displayInternal.h"

#include "font_5x4.h"
#include "font_8x4.h"
#include "font_7x5.h"
#include "font_8x6.h"
#include "font_16x8.h"
#include "HT1632.h"
#include "images.h"
#include "fortunes.h"

#include "commonUtils.h"
#include "stdTypes.h"
#include "motionSensor.h"
#include "lightSensor.h"

#include <time.h>
#include <string.h>

// Local types

class DisplayInternalInfo {
public:
  DisplayInternalInfo(HT1632Class& ht1632);
  ~DisplayInternalInfo();

  HT1632Class& ht1632;
  MotionSensor& motionSensor;
  LightSensor& lightSensor;

private:
  DisplayInternalInfo() = delete;
  DisplayInternalInfo(const DisplayInternalInfo& other) = delete;
  DisplayInternalInfo& operator=(const DisplayInternalInfo& other) = delete;
};

typedef void (*modeFunctionParamPtr)(DisplayInternalInfo& info, const void* param);
typedef void (*modeFunctionPtr)(DisplayInternalInfo& info);

typedef enum {
  displayModeNothing = 0,
  displayModeBasicClock,
  displayModeMessage
} DisplayMode;

typedef struct {
  AnimationStep animationStep;
  Int8U animationStepPhase;
  Int8U animationStepPhaseValue;
} Animation;

typedef struct {
  bool enabled;
  Font font;
  DisplayColor displayColor;
  char msg[BACKGROUND_MESSAGE_MSG_SIZE];
  int x;
  int y;
  Animation animation;
} BackgroundMessage;

typedef struct {
  bool enabled;
  DisplayColor displayColor;
  ImgArt imgArt;
  int x;
  int y;
  Animation animation;
} BackgroundImg;

typedef struct {
  const char* img;
  Int8U width;
  Int8U height;
} ImgArtInfo;

#define INCREMENT_Y_SCALE 1000
#define INCREMENT_Y_VALUE 800
struct ModeMessageData {
  char msg[MESSAGE_MAX_SIZE]; // ignored if modeMsgsIndex is valid
  ModeMsgsIndex modeMsgsIndex;  // offset of modeMessageMsg (-1 => none)
  bool incrementX;  // dictate whether text will scroll
  int confetti;  // max number of confetti (0 => none)
  bool alternateFont;
  DisplayColor displayColor;
  bool blink;
  modeFunctionParamPtr completedCallback;
  const void* completedCallbackParam;

  int incrementY; // increment Y factor every fastTick (can be negative)
  int repeats;    // number of times message will be displayed, before calling 'completed'  (~0 => forever)
                  // only used when incrementX is 'true'
  Int32U timeout;   // max number of seconds before calling 'completed'  (~0 => forever)
  Font font;

  int currX;
  int currYFactor;
  bool blinkBlankInEffect;

  int wd;
};

// ----

// Forward declarations

static const char* getFontData(Font font);
static const char* getFontWidth(Font font);
static char getFontHeight(Font font);
static int getFontGlyphStep(Font font);
static int getAdjustedTextY(Font font, int wantedY);
static Font getNextFont(Font font);

static void updateMotionDetectedPixel(DisplayInternalInfo& displayInternalInfo, bool invokeRender = true);

// not to be confused with initModeMessage()
static void initModeMessageData(int currX = 0, int currY = 0, bool incrementX = true,
				bool incrementY = false, int confetti = 0, bool blink = false,
				Font font = font5x4, Int32U timeout = ~0);
static DisplayMode getDisplayMode();

static void changeDisplayMode(DisplayMode displayMode, DisplayInternalInfo& displayInternalInfo, const void* param = 0);
static void changeDisplayModeBasicClock(DisplayInternalInfo& displayInternalInfo, const void* param);

static void initBackgroundMessage(int offset);
static void drawBackgroundMessages(DisplayInternalInfo& displayInternalInfo);

static void initBackgroundImg(int offset);
static void drawBackgroundImgs(DisplayInternalInfo& displayInternalInfo);

static ImgArtInfo getImgArtInfo(ImgArt imgArt);

// ----

// Locals

static BackgroundMessage backgroundMessage[BACKGROUND_MESSAGE_COUNT];
static BackgroundImg backgroundImg[BACKGROUND_IMG_COUNT];

static int modeMessageDataColorEffect = 0;
static ModeMessageData modeMessageData;

static int currModeIndex = 0;

static Int8U animationStepTickerFast = 0;
static Int8U animationStepTicker250ms = 0;
static Int8U animationStepTicker500ms = 0;
static Int8U animationStepTicker1sec = 0;
static Int8U animationStepTicker5sec = 0;
static Int8U animationStepTicker10sec = 0;

struct Mode {
  DisplayMode displayMode;
  const char* const displayModeStr;
  modeFunctionParamPtr handlerInit;
  modeFunctionPtr handlerTickFast;
  modeFunctionPtr handlerTick250ms;
  modeFunctionPtr handlerTick500ms;
  modeFunctionPtr handlerTick1Sec;
  modeFunctionPtr handlerTick5Sec;
  modeFunctionPtr handlerTick10Sec;
  modeFunctionPtr handlerTick25Sec;
  modeFunctionPtr handlerTick1Min;
};

// ----------------------------------------

static bool willAnimate(const Animation* animation) {
  if (animation == nullptr || animation->animationStepPhase == 0) return true;
  switch (animation->animationStep) {
     case animationStepNone:
       return true;
     case animationStepFast:
       return animationStepTickerFast % animation->animationStepPhase == animation->animationStepPhaseValue; 
     case animationStep250ms:
       return animationStepTicker250ms % animation->animationStepPhase == animation->animationStepPhaseValue; 
     case animationStep500ms:
       return animationStepTicker500ms % animation->animationStepPhase == animation->animationStepPhaseValue; 
     case animationStep1sec:
       return animationStepTicker1sec % animation->animationStepPhase == animation->animationStepPhaseValue; 
     case animationStep5sec:
       return animationStepTicker5sec % animation->animationStepPhase == animation->animationStepPhaseValue; 
     case animationStep10sec:
       return animationStepTicker10sec % animation->animationStepPhase == animation->animationStepPhaseValue; 
     default:
       break;
  }
  return true;
}

static struct tm* getTimeInfo(struct tm& timeInfo) {
  time_t rawTime;
  time(&rawTime);
  return localtime_r(&rawTime, &timeInfo);
}

// ----------------------------------------
// ----------------------------------------

// modeBasicClock

static void modeBasicClockMain(DisplayInternalInfo& displayInternalInfo) {
  HT1632Class& HT1632 = displayInternalInfo.ht1632;
  char localDisplayBuffer[MESSAGE_MAX_SIZE];
  struct tm timeInfo;

  getTimeInfo(timeInfo);
  const bool isPm = timeInfo.tm_hour > 11;
  int hour = timeInfo.tm_hour - (timeInfo.tm_hour > 12 ? 12 : 0);
  if (hour == 0) hour = 12; // midnight

  HT1632.drawTarget(BUFFER_BOARD(1)); HT1632.clear();

  snprintf(localDisplayBuffer, sizeof(localDisplayBuffer), "%2d:%02d", hour, timeInfo.tm_min);
  HT1632.drawText(localDisplayBuffer, 0 /*x*/, 0 /*y*/, FONT_16X8, FONT_16X8_WIDTH, FONT_16X8_HEIGHT, FONT_16X8_STEP_GLYPH);

  snprintf(localDisplayBuffer, sizeof(localDisplayBuffer), "%s", isPm ? "pm":"am");
  HT1632.drawText(localDisplayBuffer, 39 /*x*/, 10 /*y*/, FONT_5X4, FONT_5X4_WIDTH, FONT_5X4_HEIGHT, FONT_5X4_STEP_GLYPH);

  updateMotionDetectedPixel(displayInternalInfo);
}

const char daySun[] = "Sunday"; const char dayMon[] = "Monday"; const char dayTue[] = "Tuesday";
const char dayWed[] = "Wednesday"; const char dayThu[] = "Thursday"; const char dayFri[] = "Friday";
const char daySat[] = "Saturday";
static const char* const daysOfWeek[] = {daySun, dayMon, dayTue, dayWed, dayThu, dayFri, daySat};

const char monthJan[] = "January"; const char monthFeb[] = "February"; const char monthMar[] = "March";
const char monthApr[] = "April";   const char monthMay[] = "May";      const char monthJun[] = "June";
const char monthJul[] = "July";    const char monthAug[] = "August";   const char monthSep[] = "September";
const char monthOct[] = "October"; const char monthNov[] = "November"; const char monthDec[] = "Dec";
static const char* const months[] = {monthJan, monthFeb, monthMar, monthApr, monthMay, monthJun, monthJul,
				     monthAug, monthSep, monthOct, monthNov, monthDec};

static void modeBasicClockCheckIdle(DisplayInternalInfo& displayInternalInfo) {
  MotionInfo motionInfo;
  if (!displayInternalInfo.motionSensor.getMotionValue(&motionInfo) && 
      (motionInfo.lastChangedMin > 15 || motionInfo.lastChangedHour > 0)) {
    changeDisplayMode(displayModeNothing, displayInternalInfo);
    return;
  }

  updateMotionDetectedPixel(displayInternalInfo);
}

static char* getDayOfMonthStr(int day, char* const bufferOut, size_t bufferOutSize) {
  if (day == 1) {
    snprintf(bufferOut, bufferOutSize, "1st");
  } else if (day == 2) {
    snprintf(bufferOut, bufferOutSize, "2nd");
  } else if (day == 3) {
    snprintf(bufferOut, bufferOutSize, "3rd");
  } else if (day == 21) {
    snprintf(bufferOut, bufferOutSize, "21st");
  } else if (day == 22) {
    snprintf(bufferOut, bufferOutSize, "22nd");
  } else if (day == 23) {
      snprintf(bufferOut, bufferOutSize, "23rd");
  } else if (day == 31) {
    snprintf(bufferOut, bufferOutSize, "31st");
  } else {
    snprintf(bufferOut, bufferOutSize, "%dth", day);
  }
  return bufferOut;
}

static void modeBasicClockDate(DisplayInternalInfo& displayInternalInfo) {
  static Int8U phaseCounter = 0;
  struct tm timeInfo = {0};
  const Int8U currPhase = phaseCounter % 6;
  char localDisplayBuffer[MESSAGE_MAX_SIZE];
  HT1632Class& HT1632 = displayInternalInfo.ht1632;

  ++phaseCounter;

  if (currPhase == 0) {
    getTimeInfo(timeInfo);
    const char* const weekDayStr =
      (timeInfo.tm_wday >=0 && timeInfo.tm_wday <= 6) ? daysOfWeek[timeInfo.tm_wday] : "DDD";
    strncpy(localDisplayBuffer, weekDayStr, sizeof(localDisplayBuffer));
  } else if (currPhase == 3) {
    getTimeInfo(timeInfo);
    const char* const monthStr =
      (timeInfo.tm_mon >=0 && timeInfo.tm_mon <= 11) ? months[timeInfo.tm_mon] : "MMM";
    strncpy(localDisplayBuffer, monthStr, sizeof(localDisplayBuffer));
  } else {
    return;  // leave it be (do nothing here in this phase)
  }

  HT1632.drawTarget(BUFFER_BOARD(2));
  HT1632.clear();
  // HT1632.drawText(localDisplayBuffer, 39 /*x*/, 2 /*y*/, FONT_5X4, FONT_5X4_WIDTH, FONT_5X4_HEIGHT, FONT_5X4_STEP_GLYPH);
  HT1632.drawText(localDisplayBuffer, 39 /*x*/, 1 /*y*/, FONT_7X5, FONT_7X5_WIDTH, FONT_7X5_HEIGHT, FONT_7X5_STEP_GLYPH);
  
#if 0
  // FIXME: grab temperature from web?
  snprintf(localDisplayBuffer, sizeof(localDisplayBuffer), "%02d F", getCachedTemperatureValue());
  HT1632.drawText(localDisplayBuffer, 45 /*x*/, 10 /*y*/, FONT_5X4, FONT_5X4_WIDTH, FONT_5X4_HEIGHT, FONT_5X4_STEP_GLYPH);
#else
  getDayOfMonthStr(timeInfo.tm_mday, localDisplayBuffer, sizeof(localDisplayBuffer));
  HT1632.drawText(localDisplayBuffer, 51 /*x*/, 10 /*y*/, FONT_5X4, FONT_5X4_WIDTH, FONT_5X4_HEIGHT, FONT_5X4_STEP_GLYPH);
#endif
  
  updateMotionDetectedPixel(displayInternalInfo);
}

static void modeBasicClockCommon(DisplayInternalInfo& displayInternalInfo) {
  modeBasicClockDate(displayInternalInfo);
  modeBasicClockCheckIdle(displayInternalInfo);
}

static void modeBasicClockInit(DisplayInternalInfo& displayInternalInfo, const void* /*param*/) {
  modeBasicClockMain(displayInternalInfo); 
  modeBasicClockDate(displayInternalInfo);
}

static const Mode modeBasicClock = {displayModeBasicClock, "basicClock",
				    modeBasicClockInit /*init*/, 0 /*fast*/, 0 /*250ms*/, 0 /*500ms*/,
				    modeBasicClockCommon /*1sec*/, 0 /*5sec*/,
				    modeBasicClockMain /*10sec*/, 0 /*25sec*/, 0 /*1min*/};

// ----------------------------------------

// modeMessage

static void initModeMessageData(int currX, int currY, bool incrementX, bool incrementY,
				int confetti, bool blink, Font font, Int32U timeout) {
  // config (only changed by user)
  modeMessageData.msg[0] = 0;
  modeMessageData.incrementX = incrementX;
  modeMessageData.modeMsgsIndex = modeMsgsIndexLast;
  modeMessageData.confetti = confetti;
  modeMessageData.alternateFont = false;
  modeMessageData.displayColor = displayColorAlternate; // displayColorGreen, displayColorRed, ...
  modeMessageData.blink = blink;
  modeMessageData.completedCallback = changeDisplayModeBasicClock; // default: go back to basic clock
  modeMessageData.completedCallbackParam = 0;

  // config/runtime (user set, changes as it runs)
  modeMessageData.incrementY = incrementY ? INCREMENT_Y_VALUE : 0;
  modeMessageData.repeats = ~0;  // repeat forever, until timeout
  modeMessageData.timeout = timeout;  // seconds until next mode
  modeMessageData.font = font;

  // runtime
  modeMessageData.currX = currX;
  modeMessageData.currYFactor = currY * INCREMENT_Y_SCALE;
  modeMessageData.blinkBlankInEffect = modeMessageData.blink;
  ++modeMessageDataColorEffect;
}

static void modeMessageInit(DisplayInternalInfo& displayInternalInfo, const void* param) {
  HT1632Class& HT1632 = displayInternalInfo.ht1632;

  if (param != nullptr) {
    snprintf(modeMessageData.msg, sizeof(modeMessageData.msg), "%s", (const char*) param);
  } else if (modeMessageData.modeMsgsIndex >= (ModeMsgsIndex) 0 && modeMessageData.modeMsgsIndex < modeMsgsIndexLast) {
    strncpy(modeMessageData.msg, modeMessageMsgs[modeMessageData.modeMsgsIndex], sizeof(modeMessageData.msg));
  }

  // runtime
  modeMessageData.wd = HT1632.getTextWidth(modeMessageData.msg,
					   getFontWidth(modeMessageData.font),
					   getFontHeight(modeMessageData.font));
}

static void modeMessageFast(DisplayInternalInfo& displayInternalInfo) {
  int boardId, i;

  HT1632Class& HT1632 = displayInternalInfo.ht1632;
  HT1632.clearAll();

  const int currColorEffect = modeMessageDataColorEffect % 3; 
  for (i=0; i < BUFFER_SECONDARY; ++i) {
    if (modeMessageData.blinkBlankInEffect) break;  // blinkBlankInEffect in effect? If so, write no chars

    switch (modeMessageData.displayColor) {
        case displayColorGreen: boardId = 0; break;
        case displayColorRed: boardId = 1; break;
        case displayColorYellow: boardId = i; break;
        case displayColorAlternate:  // fall thru
        default: boardId = currColorEffect == 2 ? i : currColorEffect; break;
    }

    HT1632.drawTarget(BUFFER_BOARD(boardId+1));

    // if bouncing, make sure sure y is within range
    const int wantedY = modeMessageData.currYFactor / INCREMENT_Y_SCALE;
    const int adjustedY = modeMessageData.incrementY ? getAdjustedTextY(modeMessageData.font, wantedY) : wantedY;

    HT1632.drawText(modeMessageData.msg,
		    modeMessageData.incrementX ? (OUT_SIZE - modeMessageData.currX) : modeMessageData.currX /*x*/,
		    adjustedY /*y*/,
		    getFontData(modeMessageData.font),
		    getFontWidth(modeMessageData.font),
		    getFontHeight(modeMessageData.font),
		    getFontGlyphStep(modeMessageData.font));

    if (modeMessageData.displayColor == displayColorAlternate && currColorEffect == 2) continue;
    if (modeMessageData.displayColor != displayColorYellow) break;
  }

  drawBackgroundMessages(displayInternalInfo);
  drawBackgroundImgs(displayInternalInfo);

  // confetti
  if (modeMessageData.confetti > 0) {
    for (boardId=0; boardId < BUFFER_SECONDARY; ++boardId) {
      HT1632.drawTarget(BUFFER_BOARD(boardId+1));
      for (int xx = 0, yy = (int) getRandomNumber(modeMessageData.confetti); xx <= yy ; ++xx) {
	HT1632.setPixel( (int) getRandomNumber(OUT_SIZE), (int) getRandomNumber(COM_SIZE));
      }
    }
  }

  HT1632.renderAll();

  // text wrap
  if (modeMessageData.incrementX && ++modeMessageData.currX >= (modeMessageData.wd + OUT_SIZE)) {
    modeMessageData.currX = 0;
    ++modeMessageDataColorEffect;

    if (modeMessageData.alternateFont) {
      modeMessageData.font = getNextFont(modeMessageData.font);
      modeMessageData.wd = HT1632.getTextWidth(modeMessageData.msg,
					       getFontWidth(modeMessageData.font),
					       getFontHeight(modeMessageData.font));
    }

    // repeats?
    if (modeMessageData.repeats != ~0) {
      // mode exit (1 of 2)
      if (--modeMessageData.repeats < 0) {
	modeMessageData.repeats = ~0;  // avoid getting here again...
	if (modeMessageData.completedCallback != nullptr) {
	  (*(modeMessageData.completedCallback))(displayInternalInfo, modeMessageData.completedCallbackParam);
	  return;
	}
      }
    }
  }

  // Y (bounce effect)
  if (modeMessageData.incrementY != 0) {
    modeMessageData.currYFactor += modeMessageData.incrementY;

    const int maxY = COM_SIZE - getFontHeight(modeMessageData.font);
    if (modeMessageData.incrementY < 0 && modeMessageData.currYFactor < 0) {
      modeMessageData.incrementY *= -1;
    } else if (modeMessageData.incrementY > 0 && (modeMessageData.currYFactor >= maxY * INCREMENT_Y_SCALE)) {
      modeMessageData.incrementY *= -1;

      if (!modeMessageData.incrementX) {
	++modeMessageDataColorEffect;
	if (modeMessageData.alternateFont) {
	  modeMessageData.font = getNextFont(modeMessageData.font);
	  // modeMessageData.wd = HT1632.getTextWidth(modeMessageData.msg,
	  //				  	   getFontWidth(modeMessageData.font),
	  //					   getFontHeight(modeMessageData.font));
	}
      }
    }
  }
}

static void modeMessageCheckBlink(DisplayInternalInfo& /*displayInternalInfo*/) {
  if (modeMessageData.blink) modeMessageData.blinkBlankInEffect = !modeMessageData.blinkBlankInEffect;
}

static void modeMessageCheckTimeout(DisplayInternalInfo& displayInternalInfo) {
  // timeout?
  if (modeMessageData.timeout != (Int32U) ~0) {
    // mode exit (2 of 2)
    if (--modeMessageData.timeout <= 0) {
      modeMessageData.timeout = ~0;  // avoid getting here again...
      if (modeMessageData.completedCallback != nullptr) {
	(*(modeMessageData.completedCallback))(displayInternalInfo, modeMessageData.completedCallbackParam);
	return;
      }
    }
  }
}

static const Mode modeMessage = {displayModeMessage, "message",
				 modeMessageInit /*init*/, modeMessageFast /*fast*/,
				 modeMessageCheckBlink /*250ms*/, 0 /*500ms*/, modeMessageCheckTimeout /*1sec*/,
				 0 /*5sec*/, 0 /*10sec*/, 0 /*25sec*/, 0 /*1min*/};

// ----------------------------------------

// modeNothing

static void modeNothingInit(DisplayInternalInfo& /*displayInternalInfo*/, const void* /*param*/) {
}

static void modeNothingCheckMotion(DisplayInternalInfo& displayInternalInfo) {
  static int row = 0; static int col = 0; static Int8U color = 0;

  if (displayInternalInfo.motionSensor.getMotionValue()) {
    changeDisplayMode(displayModeBasicClock, displayInternalInfo);
    return;
  }

  HT1632Class& HT1632 = displayInternalInfo.ht1632;
  HT1632.clearAll();
  for (int boardId=0; boardId < BUFFER_SECONDARY; ++boardId) {
    if (color != (Int8U) displayColorYellow && color != (Int8U) boardId) continue;

    HT1632.drawTarget(BUFFER_BOARD(boardId+1));
    HT1632.setPixel(row, col, true);
  }
  HT1632.renderAll();

  // Prepare statics for next iteration
  if (++color > (Int8U) displayColorYellow) {
    color = 0;
    if (++row >= OUT_SIZE) { if (++col >= COM_SIZE) col = 0; row = 0; }
  }
}

static const Mode modeNothing = {displayModeNothing, "nothing",
				 modeNothingInit /*init*/, 0 /*fast*/, 0 /*250ms*/, 0 /*500ms*/,
				 modeNothingCheckMotion /*1sec*/, 0 /*5sec*/, 0 /*10sec*/, 0 /*25sec*/, 0 /*1min*/};

// ----------------------------------------

static void updateMotionDetectedPixel(DisplayInternalInfo& displayInternalInfo, bool invokeRender) {
  HT1632Class& HT1632 = displayInternalInfo.ht1632;

  // Update motion detection pixel
  const bool currMotionDetected = displayInternalInfo.motionSensor.getMotionValue();
  for (int i = 1; i <= 2; ++i) {
    displayInternalInfo.ht1632.drawTarget(BUFFER_BOARD(i));
    displayInternalInfo.ht1632.setPixel(OUT_SIZE - 1 /*loc_x*/, COM_SIZE - 1 /*loc_y*/, currMotionDetected);
  }
  if (invokeRender) HT1632.renderAll();
}

static void updateDim(DisplayInternalInfo& displayInternalInfo) {
  static bool lastRoomIsDark = false;
  const LightSensor& lightSensor = displayInternalInfo.lightSensor;
  const Int32U lightValue = lightSensor.getLightValue();

  bool currRoomIsDark;
    
  if (lastRoomIsDark) {
    // room is currently declared dark. Change it to bright if its above high water mark
    currRoomIsDark = lightValue < lightSensor.darkRoomThresholdHighWaterMark;
  } else {
    // room is currently declared bright. Change it to dark if its below low water mark
    currRoomIsDark = lightValue < lightSensor.darkRoomThresholdLowWaterMark;
  }

  if (lastRoomIsDark == currRoomIsDark) return;  // no change: done

  displayInternalInfo.ht1632.setBrightness(currRoomIsDark ? 1 : 16);
  lastRoomIsDark = currRoomIsDark;

  char localDisplayBuffer[MESSAGE_MAX_SIZE];
  snprintf(localDisplayBuffer, sizeof(localDisplayBuffer), "room is %s",
	   currRoomIsDark ? "dark" : "bright");
  (void) netNotifyEvent( (const Int8U*) localDisplayBuffer, strlen(localDisplayBuffer) );
}

// ----------------------------------------

static struct Mode const allModes[] = { modeNothing, modeBasicClock, modeMessage };
static const int allModesCount = sizeof(allModes) / sizeof(allModes[0]);

static void initDisplay(DisplayInternalInfo& displayInternalInfo) {
  int i;

  for (i=0; i < BACKGROUND_MESSAGE_COUNT; ++i) initBackgroundMessage(i);
  for (i=0; i < BACKGROUND_IMG_COUNT; ++i) initBackgroundImg(i);

  initModeMessageData(1 /*currX*/, 5 /*currY*/, false /*incrementX*/, false /*incrementY*/,
		      1 /*confetti*/, false /*blink*/, font5x4 /*font*/, 5 /*timeout*/);
  modeMessageData.modeMsgsIndex = modeMsgsIndexBoot;
  modeMessageData.displayColor = displayColorYellow;
  changeDisplayMode(displayModeMessage, displayInternalInfo);
}

static DisplayMode getDisplayMode() {
  return allModes[currModeIndex].displayMode;
}

static void changeDisplayMode(DisplayMode displayMode, DisplayInternalInfo& displayInternalInfo, const void* param) {
  HT1632Class& HT1632 = displayInternalInfo.ht1632;

  for (int i=0; i < allModesCount; ++i) {
    if (allModes[i].displayMode == displayMode) {
      currModeIndex = i;

      char localDisplayBuffer[MESSAGE_MAX_SIZE];
      snprintf(localDisplayBuffer, sizeof(localDisplayBuffer), "display mode is %s", allModes[currModeIndex].displayModeStr);
      (void) netNotifyEvent( (const Int8U*) localDisplayBuffer, strlen(localDisplayBuffer) );

      HT1632.clearAll(); HT1632.renderAll();
      if (allModes[currModeIndex].handlerInit != nullptr) (*(allModes[currModeIndex].handlerInit))(displayInternalInfo, param);
      break;
    }
  }
}

static void changeDisplayModeBasicClock(DisplayInternalInfo& displayInternalInfo, const void* param) {
  changeDisplayMode(displayModeBasicClock, displayInternalInfo, param);
}

// ======================================================================

static void initBackgroundMessage(int offset) {
  if (offset < 0 || offset >= BACKGROUND_MESSAGE_COUNT) return;
  backgroundMessage[offset].enabled = false;
  backgroundMessage[offset].font = font5x4;
  backgroundMessage[offset].displayColor = displayColorYellow;
  memset( backgroundMessage[offset].msg, 0, BACKGROUND_MESSAGE_MSG_SIZE );
  backgroundMessage[offset].x = 0;
  backgroundMessage[offset].y = 0;
}

static void drawBackgroundMessages(DisplayInternalInfo& displayInternalInfo) {
  HT1632Class& HT1632 = displayInternalInfo.ht1632;
  int boardId, i, j;
  for (i=0; i < BACKGROUND_MESSAGE_COUNT; ++i) {
    if (backgroundMessage[i].enabled == false) continue;
    if (willAnimate(&backgroundMessage[i].animation) == false) continue;
    for (j=0; j < BUFFER_SECONDARY; ++j) {
      switch (backgroundMessage[i].displayColor) {
        case displayColorGreen: boardId = 0; break;
        case displayColorRed: boardId = 1; break;
        case displayColorYellow:  // fall thru...
        default: boardId = j; break;
      }
      HT1632.drawTarget(BUFFER_BOARD(boardId+1));
      HT1632.drawText(backgroundMessage[i].msg, backgroundMessage[i].x, backgroundMessage[i].y,
		      getFontData(backgroundMessage[i].font),
		      getFontWidth(backgroundMessage[i].font),
		      getFontHeight(backgroundMessage[i].font),
		      getFontGlyphStep(backgroundMessage[i].font));
      if (backgroundMessage[i].displayColor != displayColorYellow) break;
    }
  }
}

// ======================================================================

static void initBackgroundImg(int offset) {
  if (offset < 0 || offset >= BACKGROUND_IMG_COUNT) return;
  backgroundImg[offset].enabled = false;
  backgroundImg[offset].displayColor = displayColorYellow;
  backgroundImg[offset].imgArt = imgArtLast;
  backgroundImg[offset].x = 0;
  backgroundImg[offset].y = 0;
}

static void drawBackgroundImgs(DisplayInternalInfo& displayInternalInfo) {
  HT1632Class& HT1632 = displayInternalInfo.ht1632;
  int boardId, i, j;
  ImgArtInfo imgArtInfo;

  for (i=0; i < BACKGROUND_IMG_COUNT; ++i) {
    if (backgroundImg[i].enabled == false) continue;
    if (willAnimate(&backgroundImg[i].animation) == false) continue;
    imgArtInfo = getImgArtInfo(backgroundImg[i].imgArt);
    for (j=0; j < BUFFER_SECONDARY; ++j) {
      switch (backgroundImg[i].displayColor) {
        case displayColorGreen: boardId = 0; break;
        case displayColorRed: boardId = 1; break;
        case displayColorYellow:  // fall thru...
        default: boardId = j; break;
      }
      HT1632.drawTarget(BUFFER_BOARD(boardId+1));
      HT1632.drawImage(imgArtInfo.img, imgArtInfo.width, imgArtInfo.height,
		       backgroundImg[i].x, backgroundImg[i].y);
      if (backgroundImg[i].displayColor != displayColorYellow) break;
    }
  }
}

// ======================================================================

#define IMG_CONCATENATE_DETAIL(a, b) a##b
#define IMG_CONCATENATE(a, b) IMG_CONCATENATE_DETAIL(a, b)
#define GET_ART_INFO(x) do { imgArtInfo.img = IMG_CONCATENATE(IMG_, x); \
                             imgArtInfo.width = IMG_CONCATENATE(IMG_, IMG_CONCATENATE(x, _WIDTH)); \
                             imgArtInfo.height = IMG_CONCATENATE(IMG_, IMG_CONCATENATE(x, _HEIGHT)); \
                           } while (0)

static ImgArtInfo getImgArtInfo(ImgArt imgArt) {
  ImgArtInfo imgArtInfo;
  switch (imgArt) {
  case imgArtSmiley:    GET_ART_INFO(SMILEY); break;
  case imgArtWink:      GET_ART_INFO(WINK); break;
  case imgArtBigHeart:  GET_ART_INFO(BIG_HEART); break;
  case imgArtCat:       GET_ART_INFO(CAT); break;
  case imgArtOwls:      GET_ART_INFO(OWLS); break;
  //
  case imgArtMail:      GET_ART_INFO(MAIL); break;
  case imgArtMusic:     GET_ART_INFO(MUSIC); break;
  case imgArtMusicNote: GET_ART_INFO(MUSICNOTE); break;
  case imgArtHeart:     GET_ART_INFO(HEART); break;
  case imgArtSpeakerA:  GET_ART_INFO(SPEAKER_A); break;
  case imgArtSpeakerB:  GET_ART_INFO(SPEAKER_B); break;
  //
  case imgArt8x8:      GET_ART_INFO(8X8); break;

  default: GET_ART_INFO(SMILEY); break;
  }
  return imgArtInfo;
}

// ======================================================================

static const char* getFontData(Font font) {
  switch (font) {
      case font5x4: return FONT_5X4; break;
      case font8x4: return FONT_8X4; break;
      case font7x5: return FONT_7X5; break;
      case font8x6: return FONT_8X6; break;
      case font16x8: return FONT_16X8; break;
      default: break;
  }
  return FONT_5X4;
}

static const char* getFontWidth(Font font) {
  switch (font) {
      case font5x4: return FONT_5X4_WIDTH; break;
      case font8x4: return FONT_8X4_WIDTH; break;
      case font7x5: return FONT_7X5_WIDTH; break;
      case font8x6: return FONT_8X6_WIDTH; break;
      case font16x8: return FONT_16X8_WIDTH; break;
      default: break;
  }
  return FONT_5X4_WIDTH;
}

static char getFontHeight(Font font) {
  switch (font) {
      case font5x4: return FONT_5X4_HEIGHT; break;
      case font8x4: return FONT_8X4_HEIGHT; break;
      case font7x5: return FONT_7X5_HEIGHT; break;
      case font8x6: return FONT_8X6_HEIGHT; break;
      case font16x8: return FONT_16X8_HEIGHT; break;
      default: break;
  }
  return FONT_5X4_HEIGHT;
}

static int getFontGlyphStep(Font font) {
  switch (font) {
      case font5x4: return FONT_5X4_STEP_GLYPH; break;
      case font8x4: return FONT_8X4_STEP_GLYPH; break;
      case font7x5: return FONT_7X5_STEP_GLYPH; break;
      case font8x6: return FONT_8X6_STEP_GLYPH; break;
      case font16x8: return FONT_16X8_STEP_GLYPH; break;
      default: break;
  }
  return FONT_5X4_STEP_GLYPH;
}

static int getAdjustedTextY(Font font, int wantedY) {
  if (wantedY < 0) return 0;
  const int fontHeight = (int) getFontHeight(font);
  if (fontHeight + wantedY >= COM_SIZE) {
    return COM_SIZE - fontHeight;
  }
  return wantedY;
}

static Font getNextFont(Font font) {
  Font nextFont = (Font) (((int) modeMessageData.font) + 1);
  return nextFont >= fontLast ? (Font) 0 : nextFont;
}

static void checkHT1632(HT1632Class& ht1632) {
  static int checkCount = 0;
  static const int checkCountMax = 13;  // TWEAK ME

  if (++checkCount < checkCountMax && getDisplayMode() != displayModeMessage) return;

  // HACK: ensure all chips are enabled and kicking, even if some
  //       noise in the pins interface made them go out to lunch
  ht1632.reinit();
  checkCount = 0;
}

// ----------------------------------------
// ----------------------------------------

DisplayInternalInfo::DisplayInternalInfo(HT1632Class& ht1632) :
  ht1632(ht1632), motionSensor(MotionSensor::bind()), lightSensor(LightSensor::bind()) {
  initDisplay(*this);
}

DisplayInternalInfo::~DisplayInternalInfo() {
  ht1632.clearAll();
  ht1632.renderAll();
  ht1632.blank();
}



void DisplayInternal::displayTickFast() {
  ++animationStepTickerFast;
  if (allModes[currModeIndex].handlerTickFast != nullptr) (*(allModes[currModeIndex].handlerTickFast))(*info);
}

void DisplayInternal::displayTick250ms() {
  ++animationStepTicker250ms;
  if (allModes[currModeIndex].handlerTick250ms != nullptr) (*(allModes[currModeIndex].handlerTick250ms))(*info);
}

void DisplayInternal::displayTick500ms() {
  checkHT1632(info->ht1632);
  ++animationStepTicker500ms;
  if (allModes[currModeIndex].handlerTick500ms != nullptr) (*(allModes[currModeIndex].handlerTick500ms))(*info);
}

void DisplayInternal::displayTick1sec() {
  ++animationStepTicker1sec;
  if (allModes[currModeIndex].handlerTick1Sec != nullptr) (*(allModes[currModeIndex].handlerTick1Sec))(*info);
  // updateMotionDetectedPixel(*info);
  updateDim(*info);
}

void DisplayInternal::displayTick5sec() {
  ++animationStepTicker5sec;
  if (allModes[currModeIndex].handlerTick5Sec != nullptr) (*(allModes[currModeIndex].handlerTick5Sec))(*info);
}

void DisplayInternal::displayTick10sec() {
  ++animationStepTicker10sec;
  if (allModes[currModeIndex].handlerTick10Sec != nullptr) (*(allModes[currModeIndex].handlerTick10Sec))(*info);
}

void DisplayInternal::displayTick25sec() {
  if (allModes[currModeIndex].handlerTick25Sec != nullptr) (*(allModes[currModeIndex].handlerTick25Sec))(*info);
}

void DisplayInternal::displayTick1min() {
  if (allModes[currModeIndex].handlerTick1Min != nullptr) (*(allModes[currModeIndex].handlerTick1Min))(*info);
}

void DisplayInternal::doHandleMsgModePost(const StringMap& postValues) {
  initModeMessageData(0 /*currX*/, 5 /*currY*/, true /*incrementX*/, false /*incrementY*/,
		      0 /*confetti*/, false /*blink*/, font5x4 /*font*/, 60 /*timeout*/);

  for (auto& kvp : postValues) {
    const std::string& k = kvp.first;
    const std::string& v = kvp.second;
    const char* const key = k.c_str();
    const char* const value = v.c_str(); 

    if (strncasecmp(key, "msg", strlen(key)) == 0) {
      if (strncasecmp(value, "#cookie", 7) == 0) {
	if (value[7] == 0) {
	  modeMessageData.modeMsgsIndex = (ModeMsgsIndex) getRandomNumber(modeMsgsIndexLast);
	} else {
	  modeMessageData.modeMsgsIndex = (ModeMsgsIndex) strtoul(&value[7], NULL, 10);
	}
	// nitpick: skip 0
	if (modeMessageData.modeMsgsIndex == modeMsgsIndexBoot) modeMessageData.modeMsgsIndex = modeMsgsIndexFortunate1;
      } else {
	strncpy(modeMessageData.msg, value, sizeof(modeMessageData.msg));
      }
    } else if (strncasecmp(key, "x", strlen(key)) == 0) {
      modeMessageData.currX = strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "y", strlen(key)) == 0) {
      modeMessageData.currYFactor = strtoul(value, NULL, 10) * INCREMENT_Y_SCALE;
    } else if (strncasecmp(key, "font", strlen(key)) == 0) {
      modeMessageData.font = (Font) strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "alternateFont", strlen(key)) == 0) {
      modeMessageData.alternateFont = true;
    } else if (strncasecmp(key, "confetti", strlen(key)) == 0) {
      modeMessageData.confetti = 15;
    } else if (strncasecmp(key, "bounce", strlen(key)) == 0) {
      modeMessageData.incrementY = INCREMENT_Y_VALUE;
      } else if (strncasecmp(key, "noScroll", strlen(key)) == 0) {
      modeMessageData.incrementX = false;
    } else if (strncasecmp(key, "blink", strlen(key)) == 0) {
      modeMessageData.blink = true;
    } else if (strncasecmp(key, "color", strlen(key)) == 0) {
      modeMessageData.displayColor = (DisplayColor) strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "repeats", strlen(key)) == 0) {
	modeMessageData.repeats = strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "timeout", strlen(key)) == 0) {
      modeMessageData.timeout = strtoul(value, NULL, 10);
    }
  } // for
  changeDisplayMode(displayModeMessage, *info);
}

void DisplayInternal::doHandleImgBackgroundPost(const StringMap& postValues) {
  int backgroundImgIndex = 0;
  BackgroundImg img;

  memset(&img, 0, sizeof(img));
  for (auto& kvp : postValues) {
    const std::string& k = kvp.first;
    const std::string& v = kvp.second;
    const char* const key = k.c_str();
    const char* const value = v.c_str(); 

    if (strncasecmp(key, "index", strlen(key)) == 0) {
      backgroundImgIndex = strtoul(value, NULL, 10);
      if (backgroundImgIndex < 0 || backgroundImgIndex >= BACKGROUND_IMG_COUNT) {
	backgroundImgIndex = 0;
      }
    } else if (strncasecmp(key, "enabled", strlen(key)) == 0) {
      img.enabled = true;
    } else if (strncasecmp(key, "clearAll", strlen(key)) == 0) {
      for (int i=0; i < BACKGROUND_IMG_COUNT; ++i) initBackgroundImg(i);
    } else if (strncasecmp(key, "imgArt", strlen(key)) == 0) {
      img.imgArt = (ImgArt) strtoul(value, NULL, 10);
      if (img.imgArt < 0 || img.imgArt >= imgArtLast) {
	img.imgArt = (ImgArt) 0;
      }
    } else if (strncasecmp(key, "x", strlen(key)) == 0) {
      img.x = strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "y", strlen(key)) == 0) {
      img.y = strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "color", strlen(key)) == 0) {
      img.displayColor = (DisplayColor) strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "animationStep", strlen(key)) == 0) {
      img.animation.animationStep = (AnimationStep) strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "animationPhase", strlen(key)) == 0) {
      img.animation.animationStepPhase = (uint8_t) strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "animationPhaseValue", strlen(key)) == 0) {
      img.animation.animationStepPhaseValue = (uint8_t) strtoul(value, NULL, 10);
    }
  } // for

  backgroundImg[backgroundImgIndex] = img;
}

void DisplayInternal::doHandleMsgBackgroundPost(const StringMap& postValues) {
  int msgIndex = 0;
  BackgroundMessage msg;

  memset(&msg, 0, sizeof(msg));
  for (auto& kvp : postValues) {
    const std::string& k = kvp.first;
    const std::string& v = kvp.second;
    const char* const key = k.c_str();
    const char* const value = v.c_str(); 

    if (strncasecmp(key, "index", strlen(key)) == 0) {
      msgIndex = strtoul(value, NULL, 10);
      if (msgIndex < 0 || msgIndex >= BACKGROUND_MESSAGE_COUNT) {
	msgIndex = 0;
      }
    } else if (strncasecmp(key, "msg", strlen(key)) == 0) {
      strncpy(msg.msg, value, sizeof(msg.msg));
    } else if (strncasecmp(key, "enabled", strlen(key)) == 0) {
      msg.enabled = true;
    } else if (strncasecmp(key, "clearAll", strlen(key)) == 0) {
      for (int i=0; i < BACKGROUND_MESSAGE_COUNT; ++i) initBackgroundMessage(i);
    } else if (strncasecmp(key, "font", strlen(key)) == 0) {
      msg.font = (Font) strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "x", strlen(key)) == 0) {
      msg.x = strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "y", strlen(key)) == 0) {
      msg.y = strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "color", strlen(key)) == 0) {
      msg.displayColor = (DisplayColor) strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "animationStep", strlen(key)) == 0) {
      msg.animation.animationStep = (AnimationStep) strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "animationPhase", strlen(key)) == 0) {
      msg.animation.animationStepPhase = (uint8_t) strtoul(value, NULL, 10);
    } else if (strncasecmp(key, "animationPhaseValue", strlen(key)) == 0) {
      msg.animation.animationStepPhaseValue = (uint8_t) strtoul(value, NULL, 10);
    }
  } // for

  backgroundMessage[msgIndex] = msg;
}

const char* DisplayInternal::getDisplayModeStr() const {
  return allModes[currModeIndex].displayModeStr;
}

DisplayInternal::DisplayInternal(HT1632Class& ht1632) {
  info = new DisplayInternalInfo(ht1632);
}

DisplayInternal::~DisplayInternal() {
  delete info;
  // info = 0;
}
