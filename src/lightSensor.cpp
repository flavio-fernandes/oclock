#include "lightSensor.h"

#include <stdexcept>

#include "threadsMain.h"
#include "timerTick.h"
#include "inbox.h"
#include "adc/AnalogInput.h"

std::thread::id LightSensor::mainThreadId;  // default 'invalid' value 
std::recursive_mutex LightSensor::instanceMutex;
LightSensor* LightSensor::instance = nullptr;
const size_t LightSensor::maxLightValuesSize = 10;
// Measured on the Zero W/Trixie unit on 2026-08-02 with the real room light
// switched off, which is the actual condition the clock should dim in rather
// than a hand or cover over the sensor:
//
//   room light on   ~1022
//   room light off   452 to 478, sustained and fully settled
//
// The original 360 was therefore unreachable: the darkest the room ever got
// still read above it, so dimming could never engage. The operator selected
// 460 for the low-water mark.
//
// The high-water mark had to move too. Entering dark needs one sample below
// the low-water mark and the plateau dips to 452, so 460 engages. But leaving
// dark needs a sample at or above the high-water mark, and the old 500 sat
// only 22 counts above the observed dark maximum of 478 — close enough that a
// slightly brighter night could oscillate between dim and bright. 700 keeps a
// wide band while staying far below the ~1022 lit-room reading.
const Int32U LightSensor::darkRoomThresholdLowWaterMark = 460;  // TWEAK ME!
const Int32U LightSensor::darkRoomThresholdHighWaterMark = 700; // TWEAK ME!

LightSensor::LightSensor() : lightValues() {
}

LightSensor::~LightSensor() {
  lightValues.clear();
}

LightSensor& LightSensor::bind() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (instance == nullptr) {
    instance = new LightSensor();
  }
  return *instance;
}

void LightSensor::shutdown() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  delete instance;
  instance = nullptr;
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

void LightSensor::doSensorRead(const AnalogInput& analogInput) {
  const int currRead0 = analogInput.readAnalog(0);
  const int currRead1 = analogInput.readAnalog(1);

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
  // printf("light: %d  %d\n", currRead0, currRead1);  // DEBUG
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

void LightSensor::runThreadLoop(AnalogInput& analogInput) {
  TimerTickServiceCv sensorReadTimer(600); // 0.6 seconds

  TimerTick& timerTick = TimerTick::bind();
  timerTick.registerTimerTickService(sensorReadTimer);

  InboxRegistry& inboxRegistry = InboxRegistry::bind();
  Inbox& inbox = inboxRegistry.getInbox(threadIdLightSensor);
  InboxMsg msg;

  while (true) {

    if (inbox.getMessage(msg)) {
      if (msg.inboxMsgType == inboxMsgTypeTerminate) break;
    }

    sensorReadTimer.wait();
    doSensorRead(analogInput);
  }

  timerTick.unregisterTimerTickService(sensorReadTimer.getCookie());
}

void lightSensorMain(const ThreadParam& threadParam) {
  LightSensor::registerMainThread();
  LightSensor& lightSensor = LightSensor::bind();
  lightSensor.runThreadLoop(*threadParam.analogInputP);
}
