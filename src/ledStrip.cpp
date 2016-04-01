#include "LPD8806.h"

#include "ledStrip.h"
#include "ledStripInternal.h"

#include "threadsMain.h"
#include "timerTick.h"
#include "inbox.h"

std::thread::id LedStrip::mainThreadId;
std::recursive_mutex LedStrip::instanceMutex;
LedStrip* LedStrip::instance;

// NOTE: Change these as needed!
const Int16U LedStrip::numberOfLeds = 240;
const Int8U LedStrip::pinDATA = 21;
const Int8U LedStrip::pinCLK = 20;

// ======================================================================

typedef enum {
  ledStripTodoTypeHandleModePost,
} LedStripTodoType; 

class LedStripTodo {
public:
  LedStripTodo(LedStripTodoType ledStripTodoType, StringMap&& postValues);  // && std::move()
  ~LedStripTodo() { }
  
  const LedStripTodoType ledStripTodoType;
  const StringMap postValues;

private:
  LedStripTodo() = delete;
  LedStripTodo(const LedStripTodo& other) = delete;
  LedStripTodo& operator=(const LedStripTodo& other) = delete;
};

LedStripTodo::LedStripTodo(LedStripTodoType ledStripTodoType, StringMap&& postValues) :
  ledStripTodoType(ledStripTodoType), postValues(postValues) {
  // empty
}

// ======================================================================

LedStrip& LedStrip::bind() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (instance == 0) {
    instance = new LedStrip();
  }
  return *instance;
}

void LedStrip::shutdown() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  for (const LedStripTodo* ledStripTodo : instance->ledStripTodos) {
    delete ledStripTodo;
  }
  instance->ledStripTodos.clear();

  delete instance;
  instance = 0;
}

void LedStrip::enqueueMsgModePost(StringMap& postValues) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  // alloc LedStripTodo
  LedStripTodo* const ledStripTodo =
    new LedStripTodo(ledStripTodoTypeHandleModePost, std::move(postValues));

  ledStripTodos.push_back(ledStripTodo);
}

const char* LedStrip::getInternalLedStripMode() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  return internal->getLedStripModeStr();
}

const char* LedStrip::getLedStripModeStr(LedStripMode ledStripMode) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  return internal->getLedStripModeStr(ledStripMode);
}

const LedStripTodo* LedStrip::dequeueLedStripTodo() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  if (ledStripTodos.empty()) return 0;
  const LedStripTodo* const ledStripTodo = ledStripTodos.front();
  ledStripTodos.pop_front();
  return ledStripTodo;
}

void LedStrip::checkTodoList() {
  const LedStripTodo* const ledStripTodo = dequeueLedStripTodo();
  if (ledStripTodo == 0) return; // noop
  switch (ledStripTodo->ledStripTodoType) {
  case ledStripTodoTypeHandleModePost:
    internal->doHandleModePost(ledStripTodo->postValues);
    break;

  default:
    throw std::runtime_error( "unexpected LedStripTodo type: " + std::to_string(ledStripTodo->ledStripTodoType) );
    break;
  }
  delete ledStripTodo;  // de-alloc LedStripTodo
}

void LedStrip::registerMainThread() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  const std::thread::id expectedInitialValue;
  const std::thread::id caller(std::this_thread::get_id());
  
  if (mainThreadId != expectedInitialValue && mainThreadId != caller) {
    throw std::runtime_error( "double register or invalid main thread" );
    return;
  }

  mainThreadId = caller;
}

void LedStrip::runThreadLoop(std::recursive_mutex* gpioLockMutexP) {
  TimerTickServiceCv ledStripFastTick(TimerTick::millisPerTick);
  TimerTickServiceBool ledStrip1secTick(1000);
  TimerTickServiceBool ledStrip10secTick(10000);
  TimerTickServiceBool ledStrip1minTick(60000);

  TimerTick& timerTick = TimerTick::bind();
  timerTick.registerTimerTickService(ledStripFastTick);
  timerTick.registerTimerTickService(ledStrip1secTick);
  timerTick.registerTimerTickService(ledStrip10secTick);
  timerTick.registerTimerTickService(ledStrip1minTick);

  InboxRegistry& inboxRegistry = InboxRegistry::bind();
  Inbox& inbox = inboxRegistry.getInbox(threadIdLedStrip);
  InboxMsg msg;

  LPD8806 lpd8806(gpioLockMutexP, numberOfLeds, pinDATA, pinCLK);
  this->internal = new LedStripInternal(lpd8806);

  while (true) {
    if (inbox.getMessage(msg)) {
      if (msg.inboxMsgType == inboxMsgTypeTerminate) break;
    }

    // wait for fastr tick...
    ledStripFastTick.wait();

    {
      std::lock_guard<std::recursive_mutex> guard(instanceMutex);
      checkTodoList();
      internal->fastTick();
      if (ledStrip1secTick.getAndResetExpired()) { internal->tick1sec(); }
      if (ledStrip10secTick.getAndResetExpired()) { internal->tick10sec(); }
      if (ledStrip1minTick.getAndResetExpired()) { internal->tick1min(); }
    }
  }

  delete internal; internal = 0;

  timerTick.unregisterTimerTickService(ledStripFastTick.getCookie());
  timerTick.unregisterTimerTickService(ledStrip1secTick.getCookie());
  timerTick.unregisterTimerTickService(ledStrip10secTick.getCookie());
  timerTick.unregisterTimerTickService(ledStrip1minTick.getCookie());
}

LedStrip::LedStrip() : internal(0) {
}

LedStrip::~LedStrip() {
}

void ledStripMain(const ThreadParam& threadParam) {
  // thread entry point
  LedStrip::registerMainThread();
  LedStrip& ledStrip = LedStrip::bind();
  ledStrip.runThreadLoop(threadParam.gpioLockMutexP);
}
