#ifndef _DISPLAY_H

#define _DISPLAY_H

#include <mutex>
#include <thread>
#include <list>

#include "commonTypes.h"

class DisplayTodo;  // FWD
class DisplayInternal;  // FWD

class Display {
public:
  static Display& bind();
  static void shutdown();
  
  static void registerMainThread();  // only needed by one thread
  void runThreadLoop(std::recursive_mutex* gpioLockMutexP);  // to be ran by timerTickThread only

  // call-ins from other threads that add async requests to the display thread
  void enqueueMsgModePost(StringMap& postValues);
  void enqueueImgBackgroundPost(StringMap& postValues);
  
  const char* getInternalDisplayMode();

private:
  static std::thread::id mainThreadId; // http://en.cppreference.com/w/cpp/thread/thread/id

  typedef std::list<const DisplayTodo*> DisplayTodos;
  DisplayTodos displayTodos;

  void checkTodoList();
  void enqueueDisplayTodo(DisplayTodo* displayTodo);
  const DisplayTodo* dequeueDisplayTodo();
  
  // gpio pins used by display thread
  static const int pinCS;
  static const int pinWR;
  static const int pinDATA;
  static const int pinCLK;
  
  static std::recursive_mutex instanceMutex; // ... and todo list as well
  static Display* instance;

  DisplayInternal* internal;
  
  Display();
  ~Display();

  // not implemented
  Display(const Display& other) = delete;
  Display& operator=(const Display& other) = delete;
};

#endif // _DISPLAY_H
