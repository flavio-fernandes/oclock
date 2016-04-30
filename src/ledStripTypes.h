#ifndef _LED_STRIP_TYPES_H

#define _LED_STRIP_TYPES_H

// #include "stdTypes.h"

const char* const ledStripParamLedStripMode = "ledStripMode";
const char* const ledStripParamTimeout = "timeout";
const char* const ledStripParamClearAllPixels = "clearPixels";
const char* const ledStripParamExtra = "extraParam";

typedef enum {
  ledStripModeManual = 0,
  ledStripModePastel,
  ledStripModeFill,
  ledStripModeRainbow,
  ledStripModeScan,
  ledStripModeBinaryCounter,
  ledStripModeCount  // last one
} LedStripMode;


#endif // _LED_STRIP_TYPES_H
