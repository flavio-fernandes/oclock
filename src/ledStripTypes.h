#ifndef _LED_STRIP_TYPES_H

#define _LED_STRIP_TYPES_H

// #include "stdTypes.h"

const char* const ledStripParamLedStripMode = "ledStripMode";
const char* const ledStripParamTimeout = "timeout";
const char* const ledStripParamClearAllPixels = "clearPixels";
const char* const ledStripParamEnabled = "1";

typedef enum {
  ledStripModeManual = 0,
  ledStripModeFill,
  ledStripModeRainbow,
  ledStripModeScan
} LedStripMode;


#endif // _LED_STRIP_TYPES_H
