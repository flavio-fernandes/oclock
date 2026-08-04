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
// Set from 60 days of published sensor telemetry rather than from a bench
// observation. What the feed records is getLightValue() itself — the ten-sample
// moving average — so the history is directly comparable to these constants.
//
// Zero W/Trixie unit, measured over 2026-08-03..04:
//
//   night, 01-05h    median 196, p95 408, max 476
//   day,   09-17h    min 587, p05 614, median 781
//   room light on    1022, i.e. clipped at the 10-bit ceiling
//
// The two bands are cleanly separated, so the hysteresis pair belongs inside
// the ~180-count gap between them. 460 sits above the night p95, so nights
// reliably engage dimming, and 127 counts below the daytime minimum, so
// daylight never trips it.
//
// The high-water mark was 700 until 2026-08-04. That was above the daytime
// floor of 587, so a naturally lit morning crossed it only slowly: replaying
// 08-04 shows the clock held dim until 09:52, hours after the room was plainly
// bright. 600 brightens at 07:24 on that same data while still leaving 124
// counts of margin above the brightest night sample.
//
// An earlier revision claimed the original 360 was unreachable because the room
// never got that dark. That was wrong: across 15,357 pre-migration samples,
// 32.9% were below 360, and replaying the old 360/500 pair over them produces
// 106 dim events in 57 days. The migration moved the top of the range, not the
// bottom — the bright plateau went from ~640 to a clipped 1022 while the dark
// floor stayed near 150. See docs/wiringpi-phase6-dimming-recalibration.md.
const Int32U LightSensor::darkRoomThresholdLowWaterMark = 460;  // TWEAK ME!
const Int32U LightSensor::darkRoomThresholdHighWaterMark = 600; // TWEAK ME!

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
