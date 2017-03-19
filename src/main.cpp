#include <stdio.h>
#include <stdlib.h>

#ifdef FAKE_WIRING
#include "fakeWiringPi.h"
#else
#include <wiringPi.h>
#endif // ifdef FAKE_WIRING

#include <thread>         // std::thread

#include "threadsMain.h"
#include "timerTick.h"
#include "lightSensor.h"
#include "motionSensor.h"
#include "dictionary.h"
#include "inbox.h"
#include "webHandlerInternal.h"
#include "display.h"
#include "ledStrip.h"

extern void displayMain(const ThreadParam& threadParam);
extern void timerTickMain(const ThreadParam& threadParam);
extern void lightSensorMain(const ThreadParam& threadParam);
extern void motionSensorMain(const ThreadParam& threadParam);
extern void dictionaryMain(const ThreadParam& threadParam);
extern void ledStripMain(const ThreadParam& threadParam);

extern int pulsar_main(int argc, char *argv[]);

typedef void (*ThreadMainFunction)(const ThreadParam& threadParam);
typedef struct {
  std::thread* threadP;
  ThreadMainFunction threadMainFunction;
} ThreadInfo;

void allocThreadInfoArray(ThreadInfo*& threadInfo) {
  threadInfo = (ThreadInfo*) malloc(threadIdCount * sizeof(*threadInfo));
  for (int i=0; i < threadIdCount; ++i) {
    threadInfo[i].threadP = 0;
    switch ( (ThreadId) i ) {
      case threadIdDisplay:
	threadInfo[i].threadMainFunction = displayMain;
	break;
      case threadIdLedStrip:
	threadInfo[i].threadMainFunction = ledStripMain;
        break;
    case threadIdTimerTick:
      threadInfo[i].threadMainFunction = timerTickMain;
      break;
    case threadIdLightSensor:
      threadInfo[i].threadMainFunction = lightSensorMain;
      break;
    case threadIdMotionSensor:
      threadInfo[i].threadMainFunction = motionSensorMain;
      break;
    case threadIdDictionary:
      threadInfo[i].threadMainFunction = dictionaryMain;
      break;
    default:
	throw std::runtime_error( "allocThreadInfoArray got bad ThreadId: " + std::to_string(i) );
        break;
    }
  }
}

void deAllocThreadInfoArray(ThreadInfo*& threadInfo) {
  free(threadInfo);
  threadInfo = 0;
}

void broadcastTerminateMessage(InboxRegistry& inboxRegistry, ThreadId threadIdException) {
  const InboxMsg msg(inboxMsgTypeTerminate);
  inboxRegistry.broadcast(msg, threadIdException);
}

void sendTerminateMessage(InboxRegistry& inboxRegistry, ThreadId threadId) {
  const InboxMsg msg(inboxMsgTypeTerminate);
  inboxRegistry.getInbox(threadId).addMessage(msg);
}

int main (int argc, char* argv[])
{
  InboxRegistry& inboxRegistry = InboxRegistry::bind();
  std::recursive_mutex gpioLockMutex;
  ThreadInfo* threadInfo = 0;
  ThreadParam threadParam = {0};
  
  threadParam.gpioLockMutexP = &gpioLockMutex;

  wiringPiSetupGpio();
  WebHandlerInternal::bind().start();
  
  allocThreadInfoArray(threadInfo);
  for (int i=0; i < threadIdCount; ++i) {
    threadInfo[i].threadP = new std::thread(threadInfo[i].threadMainFunction, threadParam);
  }

  pulsar_main(argc, argv);
  
  /* if we made it here, pulsar server is done and its time to stop
   * all remaining threads.
   */

  /* Synchronize the completion of each thread. But make threadIdTimerTick stop last */
  broadcastTerminateMessage(inboxRegistry, threadIdTimerTick);
  for (int i=0; i < threadIdCount; ++i) {
    if (i == threadIdTimerTick) continue;
    threadInfo[i].threadP->join();
    delete threadInfo[i].threadP;
  }
  sendTerminateMessage(inboxRegistry, threadIdTimerTick);
  threadInfo[threadIdTimerTick].threadP->join();
  delete threadInfo[threadIdTimerTick].threadP;

  LightSensor::shutdown();
  MotionSensor::shutdown();
  WebHandlerInternal::shutdown();
  Display::shutdown();
  LedStrip::shutdown();
  Dictionary::shutdown();
  TimerTick::shutdown();
  InboxRegistry::shutdown();

  deAllocThreadInfoArray(threadInfo);

  return 0;
}

