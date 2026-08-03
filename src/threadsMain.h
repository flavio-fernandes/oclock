#ifndef __THREADS_MAIN_H
#define __THREADS_MAIN_H

#include <mutex>

class Gpio;
class SpiOutput;
class AnalogInput;

typedef enum ThreadId_t {
  threadIdDisplay = 0,
  threadIdLedStrip,
  threadIdLightSensor,
  threadIdMotionSensor,
  threadIdMqttClient,
  threadIdDictionary,
  threadIdTimerTick,
  threadIdCount,  // must be last
  threadIdIgnore = threadIdCount
} ThreadId;

typedef struct ThreadParam_t {
  int argc;
  char** argv;
  std::recursive_mutex* gpioLockMutexP;
  Gpio* gpioP;
  SpiOutput* stripSpiOutputP;
  AnalogInput* analogInputP;
} ThreadParam;


#endif  // __THREADS_MAIN_H
