#include "lockGuard.h"

LockGuard::LockGuard(pthread_mutex_t*& mutexP) : mutexP(mutexP) {
  pthread_mutex_lock(mutexP);
}

LockGuard::~LockGuard() {
  pthread_mutex_unlock(mutexP);
}

