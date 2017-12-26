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

  inline TimerTickId getCookie() const  { return cookie; }
  inline bool getIsRegistered() const { return cookie != nullCookie; }
  inline bool getIsExpired() const  { return ticksLeft == 0; }
  
  int interval;  // in milliseconds
  bool periodic;
  static const TimerTickId nullCookie;

protected:
  virtual void expireTrigger() = 0;  // callback

private:
  TimerTickId cookie;
  std::atomic_ullong ticksLeft;  // decreases as the timer ticks...

  friend class TimerTick;

  TimerTickService(const TimerTickService& other) = delete;
  TimerTickService& operator=(const TimerTickService& other) = delete;
};

// ======================================================================

class TimerTickServiceBool : public TimerTickService
{
public:
  TimerTickServiceBool(int interval, bool periodic = true, bool expired = false) :
    TimerTickService(interval, periodic), expired(expired) {}
  virtual void expireTrigger() override final { expired = true; }
  bool getAndResetExpired() { if (expired) { expired = false; return true; } return false; }
private:
  std::atomic_bool expired;
};

class TimerTickServiceCv : public TimerTickService
{
public:
  TimerTickServiceCv(int interval, bool periodic = true) : TimerTickService(interval, periodic), mtx(), cv() {}
  virtual void expireTrigger() override { std::unique_lock<std::mutex> lck(mtx); cv.notify_all(); }
  void wait() { std::unique_lock<std::mutex> lck(mtx); if (getIsRegistered()) cv.wait(lck); }
private:
  std::mutex mtx;
  std::condition_variable cv;
};

class TimerTickServiceMessage : public TimerTickService
{
public:
  TimerTickServiceMessage(int interval, Inbox& inbox);
  virtual void expireTrigger() override final;
private:
  Inbox& inbox;
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
