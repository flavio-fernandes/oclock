#ifndef __THREAD_LIGHT_SENSOR_H
#define __THREAD_LIGHT_SENSOR_H

#include <mutex>
#include <thread>
#include <list>

#include "stdTypes.h"

class Mcp3002; // FWD

class LightSensor
{
public:
  static LightSensor& bind();
  static void shutdown();

  Int32U getLightValue() const;
  static const Int32U darkRoomThresholdLowWaterMark;
  static const Int32U darkRoomThresholdHighWaterMark;
  
  static void registerMainThread();  // only needed by one thread
  void runThreadLoop(std::recursive_mutex* gpioLockMutexPParam);  // to be ran by main thread only

private:
  static std::thread::id mainThreadId; // http://en.cppreference.com/w/cpp/thread/thread/id

  typedef std::list<int> LightValues;
  LightValues lightValues;

  static const size_t maxLightValuesSize;
  static const int pinClock;
  static const int pinDigitalOut;
  static const int pinDigitalIn;
  static const int pinChipSelect;
  
  static std::recursive_mutex instanceMutex;
  static LightSensor* instance;

  void doSensorRead(const Mcp3002& mcp);
  
  LightSensor();
  ~LightSensor();

  // not implemented
  LightSensor(const LightSensor& other) = delete;
  LightSensor& operator=(const LightSensor& other) = delete; 
};

#endif // __THREAD_LIGHT_SENSOR_H
