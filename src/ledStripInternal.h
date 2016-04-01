#ifndef _LED_STRIP_INTERNAL_H

#define _LED_STRIP_INTERNAL_H

#include "commonTypes.h"

class LedStripInternalInfo;
class LPD8806;

class LedStripInternal {

public:
  ~LedStripInternal();
  
  void fastTick();
  void tick1sec();
  void tick10sec();
  void tick1min();
  
  void doHandleModePost(const StringMap& postValues);
  const char* getLedStripModeStr() const;
  const char* getLedStripModeStr(LedStripMode ledStripMode) const;  // enum to string

private:
  LedStripInternal(LPD8806& lpd8806);

  LedStripInternalInfo* info;
  
  LedStripInternal() = delete;
  LedStripInternal(const LedStripInternal& other) = delete;
  LedStripInternal& operator=(const LedStripInternal& other) = delete;
  
  friend class LedStrip;
};
  
#endif // _LED_STRIP_INTERNAL_H
