#ifndef _COMMON_UTILS_H
#define _COMMON_UTILS_H

#include "stdTypes.h"
#include "commonTypes.h"

Int32U getRandomNumber(Int32U upperBound); // returns something between 0 and upperBound - 1
int netNotifyEvent(const Int8U* buffer, size_t size);
bool parseBooleanValue(const char* valueStr);
bool isParamSet(const StringMap& params, const char* const paramName, const char* const paramValue = 0);
bool getParamValue(const StringMap& params, const char* const paramName, std::string& paramValueFound);

#endif  // define _COMMON_UTILS_H 
