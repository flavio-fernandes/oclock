#ifndef _LED_STRIP_H

#define _LED_STRIP_H

#include <mutex>
#include <thread>
#include <list>

#include "stdTypes.h"
#include "commonTypes.h"

class LedStripTodo;  // FWD
class LedStripInternal;  // FWD

class LedStrip {
public:
  static LedStrip& bind();
  static void shutdown();

  static const Int16U numberOfLeds;

  static void registerMainThread();  // only needed by one thread
  void runThreadLoop(std::recursive_mutex* gpioLockMutexP);  // to be ran by timerTickThread only

  // call-ins from other threads that add async requests to the ledStrip thread
  void enqueueMsgModePost(StringMap& postValues);

  const char* getInternalLedStripMode();

private:
  static std::thread::id mainThreadId; // http://en.cppreference.com/w/cpp/thread/thread/id

  typedef std::list<const LedStripTodo*> LedStripTodos;
  LedStripTodos ledStripTodos;

  void checkTodoList();
  const LedStripTodo* dequeueLedStripTodo();

  // gpio pins used by ledStrip thread
  static const Int8U pinDATA;
  static const Int8U pinCLK;
  
  static std::recursive_mutex instanceMutex; // ... and todo list as well
  static LedStrip* instance;

  LedStripInternal* internal;
  
  LedStrip();
  ~LedStrip();

  // not implemented
  LedStrip(const LedStrip& other) = delete;
  LedStrip& operator=(const LedStrip& other) = delete;
};

#endif // _LED_STRIP_H
