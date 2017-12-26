#include "motionSensor.h"

#include <string.h>

#ifdef FAKE_WIRING
#include "fakeWiringPi.h"
#else
#include <wiringPi.h>
#endif // ifdef FAKE_WIRING

#include "threadsMain.h"
#include "timerTick.h"
#include "inbox.h"

std::thread::id MotionSensor::mainThreadId;  // default 'invalid' value 
const int MotionSensor::sensorGpioPin = 10; // 18;
std::recursive_mutex MotionSensor::instanceMutex;
MotionSensor* MotionSensor::instance = 0;

MotionSensor::MotionSensor() : inboxRegistry(InboxRegistry::bind()), gpioLockMutexP(0) {
  memset(&motionInfo, 0, sizeof(motionInfo));
}

MotionSensor::~MotionSensor() {
}

MotionSensor& MotionSensor::bind() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (instance == 0) {
    instance = new MotionSensor();
  }
  return *instance;
}

void MotionSensor::shutdown() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  delete instance;
  instance = 0;
}

void MotionSensor::registerMainThread() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  const std::thread::id expectedInitialValue;
  const std::thread::id caller(std::this_thread::get_id());
  
  if (mainThreadId != expectedInitialValue && mainThreadId != caller) {
    throw std::runtime_error( "double register or invalid main timer thread" );
    return;
  }

  mainThreadId = caller;
}

void MotionSensor::runThreadLoop(std::recursive_mutex* gpioLockMutexPParam) {
  TimerTickServiceCv doSensorRead(1000); // expected to be 1 second (motionInfo update)

  TimerTick& timerTick = TimerTick::bind();
  timerTick.registerTimerTickService(doSensorRead);

  Inbox& inbox = inboxRegistry.getInbox(threadIdMotionSensor);
  InboxMsg msg;

  gpioLockMutexP = gpioLockMutexPParam;
  {
    std::lock_guard<std::recursive_mutex> guard(*gpioLockMutexP);
    pinMode(sensorGpioPin, INPUT);
  }

  while (true) {
    if (inbox.getMessage(msg)) {
      if (msg.inboxMsgType == inboxMsgTypeTerminate) break;
    }
    doSensorRead.wait();
    checkMotionSensor();
  }

  timerTick.unregisterTimerTickService(doSensorRead.getCookie());
}

bool MotionSensor::getMotionValue(MotionInfo* out) const {
  bool rc;
  {
    std::lock_guard<std::recursive_mutex> guard(instanceMutex);
    if (out != 0) *out = motionInfo;
    rc = motionInfo.currMotionDetected;
  }
  return rc;
}

bool MotionSensor::checkMotionSensor() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  const bool currMotionDetected = digitalRead(sensorGpioPin) == HIGH;

  if (motionInfo.currMotionDetected == currMotionDetected) {
    if (++motionInfo.lastChangedSec > 59) {
      motionInfo.lastChangedSec = 0;
      if (++motionInfo.lastChangedMin > 59) {
	motionInfo.lastChangedMin = 0;
	if (motionInfo.lastChangedHour < 254) ++motionInfo.lastChangedHour;
      }
    }
  } else {
    motionInfo.currMotionDetected = currMotionDetected;

    // notify world before resetting time counter
    notifyMotionSensorChange();

    // reset time counter
    motionInfo.lastChangedSec = 0;
    motionInfo.lastChangedMin = 0;
    motionInfo.lastChangedHour = 0;
    return true;
  }       
  return false;
}

void MotionSensor::notifyMotionSensorChange() {
  InboxMsg msg;
  if (sizeof(msg.payload) < sizeof(motionInfo)) {
    throw std::runtime_error( "motionInfo too big for mailbox msg payload" );
  } else {
    msg.inboxMsgType = motionInfo.currMotionDetected ? inboxMsgTypeMotionOn : inboxMsgTypeMotionOff;
    memcpy(msg.payload, &motionInfo, sizeof(motionInfo));
  }
  inboxRegistry.broadcast(msg, threadIdMotionSensor);
}

void motionSensorMain(const ThreadParam& threadParam) {
  MotionSensor::registerMainThread();
  MotionSensor& motionSensor = MotionSensor::bind();
  motionSensor.runThreadLoop(threadParam.gpioLockMutexP);
}

