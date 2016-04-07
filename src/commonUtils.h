#ifndef _COMMON_UTILS_H
#define _COMMON_UTILS_H

#include "stdTypes.h"

Int32U getRandomNumber(Int32U upperBound); // returns something between 0 and upperBound - 1
int netNotifyEvent(const Int8U* buffer, size_t size);
bool parseBooleanValue(const char* valueStr);

#endif  // define _COMMON_UTILS_H 
