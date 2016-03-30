#include <random>

#include "commonUtils.h"

// http://en.cppreference.com/w/cpp/numeric/random
// Choose a random mean between 1 and 0xffffff
static std::random_device randomDevice;
static std::default_random_engine randomEngine(randomDevice());
static std::uniform_int_distribution<Int32U> uniform_dist(0);

Int32U getRandomNumber(Int32U upperBound) {
  return uniform_dist(randomEngine) % upperBound;
}

int netNotifyEvent(const Int8U* buffer, size_t size) {
  // FIXME: implement this!!!
  return 0;
}
