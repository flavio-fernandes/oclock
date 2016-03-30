#ifndef __THREADS_MAIN_H
#define __THREADS_MAIN_H

#include <mutex>

typedef enum ThreadId_t {
  threadIdDisplay = 0,
  threadIdLedStrip,
  threadIdLightSensor,
  threadIdMotionSensor,
  threadIdTimerTick,
  threadIdCount,  // must be last
  threadIdIgnore = threadIdCount
} ThreadId;

typedef struct ThreadParam_t {
  std::recursive_mutex* gpioLockMutexP;
} ThreadParam;


#endif  // __THREADS_MAIN_H
