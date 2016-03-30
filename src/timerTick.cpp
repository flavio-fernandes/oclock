
#include <list>

#include "threadsMain.h"
#include "timerTick.h"
#include "inbox.h"

// ======================================================================

/*static*/ const TimerTickId TimerTickService::nullCookie = 0;

TimerTickService::TimerTickService(int interval, bool periodic) :
  interval(interval), periodic(periodic), cookie(nullCookie), ticksLeft(0) {
}

TimerTickService::~TimerTickService() {
}

// ======================================================================

/*static*/ std::recursive_mutex TimerTick::instanceMutex;
/*static*/ TimerTick* TimerTick::instance = 0;
/*static*/ const int TimerTick::millisPerTick = 15;
/*static*/ std::thread::id TimerTick::timerTickMainThread;  // default 'invalid' value

TimerTickId TimerTick::registerTimerTickService(TimerTickService& timerTickService, bool start) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  if (timerTickService.getIsRegistered()) {
    throw std::runtime_error( "double register or invalid cookie for: " + std::to_string(timerTickService.cookie) );
    return timerTickService.cookie;
  }

  timerTickService.cookie = nextTimerTickId++;  // protected by guard (post increment)
  if (timerTickService.cookie == TimerTickService::nullCookie) {
    throw std::runtime_error( "too many timerTickServices?" );
    return timerTickService.cookie;
  }
  
  // http://stackoverflow.com/questions/9641960/c11-make-pair-with-specified-template-parameters-doesnt-compile
  std::pair<TimerTickServices::iterator, bool> result =
    timerTickServices.insert( std::make_pair(timerTickService.cookie, &timerTickService) );
  if (!result.second) {
    throw std::runtime_error( "could not insert TimerTickService for: " + std::to_string(timerTickService.cookie) );
    timerTickService.cookie = TimerTickService::nullCookie;
    return timerTickService.cookie;
  }
  
  if (start) _startTimerTickService2(timerTickService);
  return timerTickService.cookie;
}
  
bool TimerTick::startTimerTickService(TimerTickId cookie) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  TimerTickServices::iterator iter = timerTickServices.find(cookie);
  return iter == timerTickServices.end() ? false : _startTimerTickService2(*(iter->second));
}

bool TimerTick::_startTimerTickService2(TimerTickService& timerTickService) {
  // may or may not have lock guard... not needed
  
  if (timerTickService.interval >= 0) {
    timerTickService.ticksLeft = 1 + timerTickService.interval / millisPerTick;
  } else {
    timerTickService.ticksLeft = 1;
  }

  return true;
}

void TimerTick::unregisterTimerTickService(TimerTickId cookie) {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  TimerTickServices::iterator iter = timerTickServices.find(cookie);

  if (iter == timerTickServices.end()) {
    return;  // noop
  }
  
  TimerTickService& timerTickService = *(iter->second);
  
  timerTickService.cookie = TimerTickService::nullCookie;
  timerTickService.ticksLeft = 0;
  
  timerTickServices.erase(iter);
}

void TimerTick::registerTimerTickMainThread() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);

  const std::thread::id expectedInitialValue;
  const std::thread::id caller(std::this_thread::get_id());
  
  if (timerTickMainThread != expectedInitialValue && timerTickMainThread != caller) {
    throw std::runtime_error( "double register or invalid main timer thread" );
    return;
  }

  timerTickMainThread = caller;
}

void TimerTick::runThreadLoop() {
  const std::chrono::milliseconds sleepInterval(millisPerTick);
  const std::thread::id caller(std::this_thread::get_id());

  // sanity
  if (timerTickMainThread != caller) {
    throw std::runtime_error( "only main registered timer thread should invoke TimerTick::runThreadLoop()" );
    return;
  }

  InboxRegistry& inboxRegistry = InboxRegistry::bind();
  Inbox& inbox = inboxRegistry.getInbox(threadIdTimerTick);
  InboxMsg msg;

  while (true) {
    if (inbox.getMessage(msg)) {
      if (msg.inboxMsgType == inboxMsgTypeTerminate) {
	for (auto& entry : timerTickServices) {
	  TimerTickService& timerTickService = *(entry.second);
	  timerTickService.terminate();
	}
	break;
      }
    }

    std::this_thread::sleep_for(sleepInterval);  // sleep for a tick
    std::list<TimerTickService*> expiredEntries;
    {
      std::lock_guard<std::recursive_mutex> guard(instanceMutex);
      
      for (auto& entry : timerTickServices) {
	TimerTickService& timerTickService = *(entry.second);
	if (timerTickService.ticksLeft > 0) {
	  --timerTickService.ticksLeft;
	  if (timerTickService.ticksLeft == 0) {
	    expiredEntries.push_back(entry.second);
	  }
	}
      }
    }

    // w/out holding lock, invoke callback on expired entries
    for (auto& entry : expiredEntries) {
      TimerTickService& timerTickService = *entry;
      timerTickService.expireTrigger();
      if (timerTickService.periodic && timerTickService.getIsRegistered()) _startTimerTickService2(timerTickService);
    }
  }
}

TimerTick& TimerTick::bind() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  if (instance == 0) {
    instance = new TimerTick();
  }
  return *instance;
}

void TimerTick::shutdown() {
  std::lock_guard<std::recursive_mutex> guard(instanceMutex);
  delete instance;
  instance = 0;
}

TimerTick::TimerTick() : nextTimerTickId(TimerTickService::nullCookie + 1), timerTickServices() {
  if (instance != 0) {
    throw std::runtime_error("There can only be one instance of TimerTick!");
  }
}

TimerTick::~TimerTick() {
}

// ======================================================================

void timerTickMain(const ThreadParam& threadParam) {
  TimerTick::registerTimerTickMainThread();
  TimerTick& timerTick = TimerTick::bind();
  timerTick.runThreadLoop();
}

