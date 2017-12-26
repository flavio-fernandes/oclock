#ifndef __THREAD_MOTION_SENSOR_H
#define __THREAD_MOTION_SENSOR_H

#include <mutex>
#include <thread>

#include "stdTypes.h"

class InboxRegistry;  // FWD

typedef struct MotionInfo_t {
  bool currMotionDetected;
  Int8U lastChangedSec;  // 0 to 60
  Int8U lastChangedMin;  // 0 to 60
  Int8U lastChangedHour; // 0 to 255
} MotionInfo;

class MotionSensor
{
public:
  static MotionSensor& bind();
  static void shutdown();

  bool getMotionValue(MotionInfo* out = 0) const;
  
  static void registerMainThread();  // only needed by one thread
  void runThreadLoop(std::recursive_mutex* gpioLockMutexPParam);  // to be ran by main thread only

private:
  static std::thread::id mainThreadId; // http://en.cppreference.com/w/cpp/thread/thread/id

  MotionInfo motionInfo;
  static const int sensorGpioPin;
  InboxRegistry& inboxRegistry;
  
  std::recursive_mutex* gpioLockMutexP;
  static std::recursive_mutex instanceMutex;
  static MotionSensor* instance;

  MotionSensor();
  ~MotionSensor();
  bool checkMotionSensor();
  void notifyMotionSensorChange();
  
  // not implemented
  MotionSensor(const MotionSensor& other) = delete;
  MotionSensor& operator=(const MotionSensor& other) = delete; 
};

#endif // __THREAD_MOTION_SENSOR_H
