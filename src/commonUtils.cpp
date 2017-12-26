#include <random>
#include <algorithm>
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

bool parseBooleanValue(const char* valueStr) {
  if (valueStr == nullptr) return false;
  if (strncasecmp(valueStr, "n", 1) == 0) return false;
  if (strncasecmp(valueStr, "y", 1) == 0) return true;
  if (strcasecmp(valueStr, "false") == 0) return false;
  if (strcasecmp(valueStr, "true") == 0) return true;
  return strtoul(valueStr, NULL /*endptr*/, 0 /*base*/) != 0;
}

bool isParamSet(const StringMap& params, const char* const paramName, const char* const paramValue) {
  StringMap::const_iterator iter = params.find(paramName);
  if (iter == params.end()) return false;
  if (paramValue != nullptr && iter->second != paramValue) return false;
  return true;
}

bool getParamValue(const StringMap& params, const char* const paramName, std::string& paramValueFound) {
  StringMap::const_iterator iter = params.find(paramName);
  if (iter != params.end()) {
    paramValueFound = iter->second;
    return true;
  }
  return false;
}

static bool isUnwantedChar(char c) {
  return c < ' ' or c > '~';
}

std::string currTimestamp() {
  time_t rawTime;
  struct tm timeInfo;
  char buff[64];

  time(&rawTime);
  localtime_r(&rawTime, &timeInfo);
  std::string str(asctime_r(&timeInfo, buff));
  str.erase(std::remove_if(str.begin(), str.end(), &isUnwantedChar), str.end());
  return str;
}
