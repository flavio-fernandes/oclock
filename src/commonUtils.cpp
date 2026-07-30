#include <random>
#include <algorithm>
#include <ctime>
#include <stdlib.h>
#include <strings.h>

#include "commonUtils.h"

Int32U getRandomNumber(Int32U upperBound) {
  if (upperBound == 0) return 0;

  // The display and LED strip call this from different threads. Keeping an
  // engine per thread avoids a data race, and a bounded distribution avoids
  // the modulo bias of "random % upperBound".
  static thread_local std::mt19937 randomEngine(std::random_device{}());
  std::uniform_int_distribution<Int32U> distribution(0, upperBound - 1);
  return distribution(randomEngine);
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
