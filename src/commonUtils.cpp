#include <random>
#include <stdlib.h>
#include <strings.h>

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

bool parseBooleanValue(const char* valueStr) {
  if (valueStr == nullptr) return false;
  if (strncasecmp(valueStr, "n", 1) == 0) return false;
  if (strncasecmp(valueStr, "y", 1) == 0) return true;
  if (strcasecmp(valueStr, "false") == 0) return false;
  if (strcasecmp(valueStr, "true") == 0) return true;
  return strtoul(valueStr, NULL /*endptr*/, 0 /*base*/) != 0;
}
