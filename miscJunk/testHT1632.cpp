#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef FAKE_WIRING
#include "fakeWiringPi.h"
#else
#include <wiringPi.h>
#endif // ifdef FAKE_WIRING

#include "font_16x8.h"
#include "font_5x4.h"
#include "font_7x5.h"
#include "font_8x4.h"
#include "font_8x6.h"
#include "images.h"
#include "HT1632.h"

#include "threadsMain.h"
#include "timerTick.h"
#include "inbox.h"
#include "lightSensor.h"
#include "motionSensor.h"

int i = 0;
int wd;
int boardFlip = 0;

static bool motionBit = false;

// const char message[] = "Have a nice day? Yes, always! 123:456";
// const char message[] = "hello thuy do you like this test? hit me with the seven digits! 1234567";
const char message[] = \
  "The broccoli says 'I am like a tree', the mushroom says 'I am like an umbrella' and the banana says 'Let's change subject!'";

int get_random_number(int min, int max) {
  long int result = random() % max;
  return (result < (long int) min) ? min : (int) result;
}

static void setup(HT1632Class& ht1632) {
  ht1632.begin(6 /*cs*/,  13 /*wr*/, 19 /*data*/, 26 /*clk*/);
}

static void loopOnce(HT1632Class& ht1632) {
  static bool loopedOnce = false;

  if (loopedOnce) return;
  loopedOnce = true;
  
  for (int y=0; y < COM_SIZE; ++y) {
    ht1632.drawTarget(BUFFER_BOARD(1));
    for (int x = 0; x < OUT_SIZE; ++x) ht1632.setPixel(x, y);
    ht1632.render();
    delay(250);
    ht1632.clear();
  }
  
  // Multiple screen control
  ht1632.drawTarget(BUFFER_BOARD(1));
  ht1632.drawText( "Hello!", 0, 0, FONT_5X4, FONT_5X4_WIDTH, FONT_5X4_HEIGHT, FONT_5X4_STEP_GLYPH);
  ht1632.render();

  ht1632.drawTarget(BUFFER_BOARD(2));
  ht1632.drawText("Foo", 1, 6, FONT_5X4, FONT_5X4_WIDTH, FONT_5X4_HEIGHT, FONT_5X4_STEP_GLYPH);
  ht1632.render();
  
  ht1632.drawTarget(BUFFER_SECONDARY);
  ht1632.drawText("Bar", 1, 6, FONT_5X4, FONT_5X4_WIDTH, FONT_5X4_HEIGHT, FONT_5X4_STEP_GLYPH);
  ht1632.render();
  delay(1000);

  ht1632.drawTarget(BUFFER_BOARD(2));
  ht1632.transition(TRANSITION_FADE, 5000);
}

static void loop(HT1632Class& ht1632) {

  ht1632.drawTarget(BUFFER_BOARD(1)); ht1632.clear();
  ht1632.drawTarget(BUFFER_BOARD(2)); ht1632.clear();
  ht1632.drawTarget(BUFFER_SECONDARY); ht1632.clear();

  ht1632.drawTarget(BUFFER_BOARD( (boardFlip%2) ? 1 : 2 ));

  if (boardFlip % 5 == 0) {
    wd = ht1632.getTextWidth(message, FONT_16X8_WIDTH, FONT_16X8_HEIGHT);
    ht1632.drawText(message, OUT_SIZE - i, 0, FONT_16X8, FONT_16X8_WIDTH, FONT_16X8_HEIGHT, FONT_16X8_STEP_GLYPH);
  } else if (boardFlip % 5 == 1) {
    wd = ht1632.getTextWidth(message, FONT_5X4_WIDTH, FONT_5X4_HEIGHT);
    ht1632.drawText(message, OUT_SIZE - i, 9, FONT_5X4, FONT_5X4_WIDTH, FONT_5X4_HEIGHT, FONT_5X4_STEP_GLYPH);
  } else if (boardFlip % 5 == 2) {
    wd = ht1632.getTextWidth(message, FONT_7X5_WIDTH, FONT_7X5_HEIGHT);
    ht1632.drawText(message, OUT_SIZE - i, 4, FONT_7X5, FONT_7X5_WIDTH, FONT_7X5_HEIGHT, FONT_7X5_STEP_GLYPH);
  } else if (boardFlip % 5 == 3) {
    wd = ht1632.getTextWidth(message, FONT_8X4_WIDTH, FONT_8X4_HEIGHT);
    ht1632.drawText(message, OUT_SIZE - i, 7, FONT_8X4, FONT_8X4_WIDTH, FONT_8X4_HEIGHT, FONT_8X4_STEP_GLYPH);
  } else {
    wd = ht1632.getTextWidth(message, FONT_8X6_WIDTH, FONT_8X6_HEIGHT);
    ht1632.drawText(message, OUT_SIZE - i, 7, FONT_8X6, FONT_8X6_WIDTH, FONT_8X6_HEIGHT, FONT_8X6_STEP_GLYPH);
  }
  
  i = (i+1) % (wd + OUT_SIZE);
  if (i == 1) ++boardFlip;

#if 1
  ht1632.drawTarget(BUFFER_BOARD(1));

  // Simple rendering example
  // ht1632.clear();
  ht1632.drawImage((i%14 < 7) ? IMG_SPEAKER_A:IMG_SPEAKER_B, IMG_SPEAKER_WIDTH,  IMG_SPEAKER_HEIGHT, 0, 0);
  ht1632.drawImage(IMG_MUSICNOTE, IMG_MUSICNOTE_WIDTH,  IMG_MUSICNOTE_HEIGHT, 8, (i % 18 < 9) ? 1:0);
  ht1632.drawImage(IMG_MUSIC, IMG_MUSIC_WIDTH,  IMG_MUSIC_HEIGHT, 13, (i % 40 < 20)?1:0);
  ht1632.drawImage(IMG_MUSICNOTE, IMG_MUSICNOTE_WIDTH,  IMG_MUSICNOTE_HEIGHT, 23, (i % 6 < 3)?1:0);
  ht1632.drawImage(IMG_MUSICNOTE, IMG_MUSICNOTE_WIDTH,  IMG_MUSICNOTE_HEIGHT, 28, (i % 6 < 3)?0:1);
  ht1632.transition(TRANSITION_BUFFER_SWAP);
#endif

  //
  // Example of automatic clipping and data preservation.
  //
  ht1632.drawTarget(BUFFER_BOARD(2));
  ht1632.drawImage(IMG_HEART, IMG_HEART_WIDTH, IMG_HEART_HEIGHT, 1 /* x */, 1 /* y */);
  ht1632.drawImage(IMG_HEART, IMG_HEART_WIDTH, IMG_HEART_HEIGHT, 11 /* x */, 4 /* y */);
  if (i % 30 < 10) {
    ht1632.drawImage(IMG_HEART, IMG_HEART_WIDTH, IMG_HEART_HEIGHT, 20 /* x */, 7 /* y */);
  } else {
    ht1632.drawImage(IMG_BIG_HEART, IMG_BIG_HEART_WIDTH, IMG_BIG_HEART_HEIGHT, 22 /* x */, 6 /* y */);
  }
  if (i % 40 < 7) {
    if (boardFlip % 2) {
      ht1632.drawImage(IMG_CAT, IMG_CAT_WIDTH, IMG_CAT_HEIGHT, 44 /* x */, 3 /* y */);
    } else {
      ht1632.drawImage(IMG_OWLS, IMG_OWLS_WIDTH, IMG_OWLS_HEIGHT, 44 /* x */, 3 /* y */);
    }
  }

  ht1632.drawImage((i % 48 < 24)?IMG_SMILEY:IMG_WINK, IMG_SMILEY_WIDTH, IMG_SMILEY_HEIGHT, 33 /* x */, 1 /* y */);

#if 1
  for (int brd=0; brd < BUFFER_SECONDARY; ++brd) {
    ht1632.drawTarget(BUFFER_BOARD(brd+1));
    for (int xx=0, yy = get_random_number(1, 16); xx < yy ; ++xx) {
      ht1632.setPixel(get_random_number(0,OUT_SIZE), get_random_number(0,COM_SIZE));
    }
  }
#endif

  char msg[80];
  ht1632.drawTarget(BUFFER_BOARD( (boardFlip%2) ? 2 : 1 ));
  const Int32U lightSensor = LightSensor::bind().getLightValue();
  snprintf(msg, sizeof(msg), "light %lu Motion %d", lightSensor, motionBit?1:0);
  ht1632.drawText( msg, 44, 0, FONT_7X5, FONT_7X5_WIDTH, FONT_7X5_HEIGHT, FONT_7X5_STEP_GLYPH);

  MotionInfo motionInfo;
  MotionSensor::bind().getMotionValue(&motionInfo);
  snprintf(msg, sizeof(msg), "%d %02d:%02d:%02d",
	   motionInfo.currMotionDetected?1:0,
	   (int) motionInfo.lastChangedHour,
	   (int) motionInfo.lastChangedMin,
	   (int) motionInfo.lastChangedSec);
  ht1632.drawText( msg, 44, 8, FONT_7X5, FONT_7X5_WIDTH, FONT_7X5_HEIGHT, FONT_7X5_STEP_GLYPH);
  
  for (int brd=0; brd < BUFFER_SECONDARY; ++brd) {
    ht1632.drawTarget(BUFFER_BOARD(brd+1));
    ht1632.render();
  }
}

void ht1632main(const ThreadParam& threadParam) {
  TimerTickServiceBool reinitTimeService(5000);  // 5 seconds
  TimerTickServiceCv refreshTimeService(26);
  
  TimerTick& timerTick = TimerTick::bind();
  timerTick.registerTimerTickService(reinitTimeService);
  timerTick.registerTimerTickService(refreshTimeService);

  InboxRegistry& inboxRegistry = InboxRegistry::bind();
  Inbox& inbox = inboxRegistry.getInbox(threadIdHt1632);
  InboxMsg msg;
  
  HT1632Class ht1632(threadParam.gpioLockMutexP);
  setup(ht1632);

  while (true) {
    if (inbox.getMessage(msg)) {
      if (msg.inboxMsgType == inboxMsgTypeTerminate) break;

      if (msg.inboxMsgType == InboxMsgTypeMotionOn) motionBit = true;
      if (msg.inboxMsgType == InboxMsgTypeMotionOff) motionBit = false;
    }

    if (reinitTimeService.getAndResetExpired()) ht1632.reinit();
    refreshTimeService.wait();

    loopOnce(ht1632);
    loop(ht1632);
  }

  ht1632.blank();
  
  timerTick.unregisterTimerTickService(refreshTimeService.getCookie());
  timerTick.unregisterTimerTickService(reinitTimeService.getCookie());
}

