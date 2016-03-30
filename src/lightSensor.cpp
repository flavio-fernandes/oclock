#include "lightSensor.h"

#include "threadsMain.h"
#include "timerTick.h"
#include "inbox.h"
#include "mcp300x.h"

std::thread::id LightSensor::mainThreadId;  // default 'invalid' value 
std::recursive_mutex LightSensor::instanceMutex;
LightSensor* LightSensor::instance = 0;
const size_t LightSensor::maxLightValuesSize = 13;
const int LightSensor::pinClock = 17;
const int LightSensor::pinDigitalOut = 27;
const int LightSensor::pinDigitalIn = 22;
const int LightSensor::pinChipSelect = 4;

const Int32U LightSensor::darkRoomThresholdLowWaterMark = 350;  // TWEAK ME!
const Int32U LightSensor::darkRoomThresholdHighWaterMark = 480; // TWEAK ME!

LightSensor::LightSensor() : lightValues() {
}

LightSensor::~LightSensor() {
  lightValues.clear();
}

LightSensor& LightSensor::bind() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (instance == 0) {
    instance = new LightSensor();
  }
  return *instance;
}

void LightSensor::shutdown() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  delete instance;
  instance = 0;
}

void LightSensor::registerMainThread() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  const std::thread::id expectedInitialValue;
  const std::thread::id caller(std::this_thread::get_id());
  
  if (mainThreadId != expectedInitialValue && mainThreadId != caller) {
    throw std::runtime_error( "double register or invalid main timer thread" );
    return;
  }

  mainThreadId = caller;
}

void LightSensor::doSensorRead(const Mcp3002& mcp) {
  const int currRead0 = mcp.readAnalog(0);
  const int currRead1 = mcp.readAnalog(1);

  if (currRead0 < 0 || currRead1 < 0) {
    throw std::runtime_error( "failed to read analog value for light sensor" );
    return;
  }

  {
    std::lock_guard<std::recursive_mutex> guard(instanceMutex);

    // trim off older entry
    while (lightValues.size() >= maxLightValuesSize) lightValues.pop_front();

    // add new entry
    lightValues.push_back( (currRead0 + currRead1) / 2 );
  }
  // printf("light: %d  %d\n", currRead0, currRead1);  // FIXME
}

Int32U LightSensor::getLightValue() const {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  // calculate average
  int lightValueEntries = 0;
  int lightValueSum = 0;
  for (int currValue : lightValues) {
    ++lightValueEntries;
    lightValueSum += currValue;
  }
  
  return lightValueEntries == 0 ? 0 : lightValueSum / lightValueEntries;
}

void LightSensor::runThreadLoop(std::recursive_mutex* gpioLockMutexPParam) {
  TimerTickServiceCv sensorReadTimer(600); // 0.6 seconds

  TimerTick& timerTick = TimerTick::bind();
  timerTick.registerTimerTickService(sensorReadTimer);

  InboxRegistry& inboxRegistry = InboxRegistry::bind();
  Inbox& inbox = inboxRegistry.getInbox(threadIdLightSensor);
  InboxMsg msg;

  const Mcp3002 mcp(*gpioLockMutexPParam, pinClock, pinDigitalOut, pinDigitalIn, pinChipSelect);
  while (true) {

    if (inbox.getMessage(msg)) {
      if (msg.inboxMsgType == inboxMsgTypeTerminate) break;
    }

    sensorReadTimer.wait();
    doSensorRead(mcp);
  }

  timerTick.unregisterTimerTickService(sensorReadTimer.getCookie());
}

void lightSensorMain(const ThreadParam& threadParam) {
  LightSensor::registerMainThread();
  LightSensor& lightSensor = LightSensor::bind();
  lightSensor.runThreadLoop(threadParam.gpioLockMutexP);
}
