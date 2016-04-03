#include "HT1632.h"

#include "display.h"
#include "displayInternal.h"

#include "threadsMain.h"
#include "timerTick.h"
#include "inbox.h"

std::thread::id Display::mainThreadId;
std::recursive_mutex Display::instanceMutex;
Display* Display::instance;

// NOTE: Change these as needed!
const int Display::pinCS = 6;
const int Display::pinWR = 13;
const int Display::pinDATA = 19;
const int Display::pinCLK = 26;

// ======================================================================

typedef enum {
  displayTodoTypeHandleMsgModePost,
  displayTodoTypeHandleImgBackgroundPost,
  displayTodoTypeHandleMsgBackgroundPost,
} DisplayTodoType; 

class DisplayTodo {
public:
  DisplayTodo(DisplayTodoType displayTodoType, StringMap&& postValues);  // && std::move()
  ~DisplayTodo() { }
  
  const DisplayTodoType displayTodoType;
  const StringMap postValues;

private:
  DisplayTodo() = delete;
  DisplayTodo(const DisplayTodo& other) = delete;
  DisplayTodo& operator=(const DisplayTodo& other) = delete;
};

DisplayTodo::DisplayTodo(DisplayTodoType displayTodoType, StringMap&& postValues) :
  displayTodoType(displayTodoType), postValues(postValues) {
  // empty
}

// ======================================================================

Display& Display::bind() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (instance == 0) {
    instance = new Display();
  }
  return *instance;
}

void Display::shutdown() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  for (const DisplayTodo* displayTodo : instance->displayTodos) {
    delete displayTodo;
  }
  instance->displayTodos.clear();

  delete instance;
  instance = 0;
}

void Display::enqueueDisplayTodo(DisplayTodo* displayTodo) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (displayTodo == nullptr) throw std::runtime_error("asked to enqueue DisplayTodo that is a nullptr");
  else displayTodos.push_back(displayTodo);
}

void Display::enqueueMsgModePost(StringMap& postValues) {
  enqueueDisplayTodo( new DisplayTodo(displayTodoTypeHandleMsgModePost, std::move(postValues)) ); // alloc DisplayTodo
}

void Display::enqueueImgBackgroundPost(StringMap& postValues) {
  enqueueDisplayTodo( new DisplayTodo(displayTodoTypeHandleImgBackgroundPost, std::move(postValues)) ); // alloc DisplayTodo
}

void Display::enqueueMsgBackgroundPost(StringMap& postValues) {
  enqueueDisplayTodo( new DisplayTodo(displayTodoTypeHandleMsgBackgroundPost, std::move(postValues)) ); // alloc DisplayTodo
}

const char* Display::getInternalDisplayMode() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  return internal->getDisplayModeStr();
}

const DisplayTodo* Display::dequeueDisplayTodo() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  if (displayTodos.empty()) return 0;
  const DisplayTodo* const displayTodo = displayTodos.front();
  displayTodos.pop_front();
  return displayTodo;
}

void Display::checkTodoList() {
  const DisplayTodo* const displayTodo = dequeueDisplayTodo();
  if (displayTodo == 0) return; // noop
  switch (displayTodo->displayTodoType) {
  case displayTodoTypeHandleMsgModePost:
    internal->doHandleMsgModePost(displayTodo->postValues);
    break;
  case displayTodoTypeHandleImgBackgroundPost:
    internal->doHandleImgBackgroundPost(displayTodo->postValues);
    break;
  case displayTodoTypeHandleMsgBackgroundPost:
    internal->doHandleMsgBackgroundPost(displayTodo->postValues);
    break;

  default:
    throw std::runtime_error( "unexpected DisplayTodo type: " + std::to_string(displayTodo->displayTodoType) );
    break;
  }
  delete displayTodo;  // de-alloc DisplayTodo
}

void Display::registerMainThread() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  const std::thread::id expectedInitialValue;
  const std::thread::id caller(std::this_thread::get_id());
  
  if (mainThreadId != expectedInitialValue && mainThreadId != caller) {
    throw std::runtime_error( "double register or invalid main thread" );
    return;
  }

  mainThreadId = caller;
}

void Display::runThreadLoop(std::recursive_mutex* gpioLockMutexP) {
  TimerTickServiceCv displayFastTick(TimerTick::millisPerTick);
  TimerTickServiceBool display250msTick(250);
  TimerTickServiceBool display500msTick(500);
  TimerTickServiceBool display1secTick(1000);
  TimerTickServiceBool display5secTick(5000);
  TimerTickServiceBool display10secTick(10000);
  TimerTickServiceBool display25secTick(25000);
  TimerTickServiceBool display1minTick(60000);

  TimerTick& timerTick = TimerTick::bind();
  timerTick.registerTimerTickService(displayFastTick);
  timerTick.registerTimerTickService(display250msTick);
  timerTick.registerTimerTickService(display500msTick);
  timerTick.registerTimerTickService(display1secTick);
  timerTick.registerTimerTickService(display5secTick);
  timerTick.registerTimerTickService(display10secTick);
  timerTick.registerTimerTickService(display25secTick);
  timerTick.registerTimerTickService(display1minTick);

  InboxRegistry& inboxRegistry = InboxRegistry::bind();
  Inbox& inbox = inboxRegistry.getInbox(threadIdDisplay);
  InboxMsg msg;

  HT1632Class ht1632(gpioLockMutexP);
  ht1632.begin(pinCS, pinWR, pinDATA, pinCLK);
  this->internal = new DisplayInternal(ht1632);
  
  while (true) {
    if (inbox.getMessage(msg)) {
      if (msg.inboxMsgType == inboxMsgTypeTerminate) break;
    }

    // wait for fast tick...
    displayFastTick.wait();

    {
      std::lock_guard<std::recursive_mutex> guard(instanceMutex);

      checkTodoList();
      internal->displayTickFast();
      if (display250msTick.getAndResetExpired()) { internal->displayTick250ms(); }
      if (display500msTick.getAndResetExpired()) { internal->displayTick500ms(); }
      if (display1secTick.getAndResetExpired()) { internal->displayTick1sec(); }
      if (display5secTick.getAndResetExpired()) { internal->displayTick5sec(); }
      if (display10secTick.getAndResetExpired()) { internal->displayTick10sec(); }
      if (display25secTick.getAndResetExpired()) { internal->displayTick25sec(); }
      if (display1minTick.getAndResetExpired()) { internal->displayTick1min(); }
    }
  }

  delete internal; internal = 0;
  
  timerTick.unregisterTimerTickService(displayFastTick.getCookie());
  timerTick.unregisterTimerTickService(display250msTick.getCookie());
  timerTick.unregisterTimerTickService(display500msTick.getCookie());
  timerTick.unregisterTimerTickService(display1secTick.getCookie());
  timerTick.unregisterTimerTickService(display5secTick.getCookie());
  timerTick.unregisterTimerTickService(display10secTick.getCookie());
  timerTick.unregisterTimerTickService(display25secTick.getCookie());
  timerTick.unregisterTimerTickService(display1minTick.getCookie());
}

Display::Display() : internal(0) {
}

Display::~Display() {
}

void displayMain(const ThreadParam& threadParam) {
  // thread entry point
  Display::registerMainThread();
  Display& display = Display::bind();
  display.runThreadLoop(threadParam.gpioLockMutexP);
}
