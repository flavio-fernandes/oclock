#ifndef _LOCK_GUARD
#define _LOCK_GUARD

#include <pthread.h>

class LockGuard {
public:
  LockGuard(pthread_mutex_t*& mutexP);
  ~LockGuard();
  bool isValid() const { return mutexP != 0; }

private:
  pthread_mutex_t*& mutexP;

  // not implemented
  LockGuard();
  LockGuard(const LockGuard& other);
  LockGuard& operator=(const LockGuard& other);
};

#endif
