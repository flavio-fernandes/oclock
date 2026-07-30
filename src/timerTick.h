#ifndef __TIMER_TICK_H
#define __TIMER_TICK_H

#include <atomic>
#include <chrono>
#include <condition_variable> // std::condition_variable
#include <mutex>
#include <thread>
#include <unordered_map>

typedef unsigned long long TimerTickId;

class TimerTick; // FWD
class Inbox; // FWD

class TimerTickService
{
public:
  TimerTickService(int interval, bool periodic = true);
  virtual ~TimerTickService();

  inline TimerTickId getCookie() const  { return cookie.load(); }
  inline bool getIsRegistered() const { return cookie.load() != nullCookie; }
  inline bool getIsExpired() const  { return ticksLeft == 0; }
  
  int interval;  // in milliseconds
  bool periodic;
  static const TimerTickId nullCookie;

protected:
  virtual void expireTrigger() = 0;  // callback

private:
  std::atomic_ullong cookie;
  std::atomic_ullong ticksLeft;  // decreases as the timer ticks...

  friend class TimerTick;

  TimerTickService(const TimerTickService& other) = delete;
  TimerTickService& operator=(const TimerTickService& other) = delete;
};

// ======================================================================

#if 0
class TimerTickServiceCounter : public TimerTickService
{
public:
  TimerTickServiceCounter(int interval, bool periodic = true) :
    TimerTickService(interval, periodic), counter(0) {}
  virtual void expireTrigger() override final { ++counter; }
  std::atomic_ullong counter;
};
#endif  // #if 0

class TimerTickServiceBool : public TimerTickService
{
public:
  TimerTickServiceBool(int interval, bool periodic = true, bool expired = false) :
    TimerTickService(interval, periodic), expired(expired) {}
  virtual void expireTrigger() override final { expired = true; }
  bool getAndResetExpired() { return expired.exchange(false); }
private:
  std::atomic_bool expired;
};

class TimerTickServiceCv : public TimerTickService
{
public:
  TimerTickServiceCv(int interval, bool periodic = true) :
    TimerTickService(interval, periodic), mtx(), cv(), pendingExpirations(0) {}
  virtual void expireTrigger() override {
    std::unique_lock<std::mutex> lck(mtx);
    ++pendingExpirations;
    cv.notify_all();
  }
  void wait() {
    std::unique_lock<std::mutex> lck(mtx);
    cv.wait(lck, [this] { return pendingExpirations > 0; });
    --pendingExpirations;
  }
private:
  std::mutex mtx;
  std::condition_variable cv;
  unsigned int pendingExpirations;
};

typedef bool (*TimerTickServiceMessageCondFunction)(void* arg);
class TimerTickServiceMessage : public TimerTickService
{
public:
  TimerTickServiceMessage(int interval,
			  Inbox& inbox,
			  TimerTickServiceMessageCondFunction condFunction = nullptr,
			  void* condFunctionArg = nullptr);
  virtual void expireTrigger() override final;
private:
  Inbox& inbox;
  TimerTickServiceMessageCondFunction condFunction;
  void* const condFunctionArg;
};

// ======================================================================

class TimerTick
{
public:
  static TimerTick& bind();
  static void shutdown();
  
  TimerTickId registerTimerTickService(TimerTickService& timerTickService, bool start = true);
  bool startTimerTickService(TimerTickId cookie);
  void unregisterTimerTickService(TimerTickId cookie);

  static void registerTimerTickMainThread();  // only needed by one thread
  void runThreadLoop();  // to be ran by timerTickThread only

  static const int millisPerTick;
private:
  static std::thread::id timerTickMainThread; // http://en.cppreference.com/w/cpp/thread/thread/id

  TimerTick();
  ~TimerTick();

  bool _startTimerTickService2(TimerTickService& timerTickService);
  
  static std::recursive_mutex instanceMutex;
  static TimerTick* instance;
  TimerTickId nextTimerTickId;
  
  typedef std::unordered_map<TimerTickId, TimerTickService*> TimerTickServices;
  TimerTickServices timerTickServices;
  
  // not implemented
  TimerTick(const TimerTick& other) = delete;
  TimerTick& operator=(const TimerTick& other) = delete;
};

#endif  // __TIMER_TICK_H
