#ifndef _DISPLAY_INTERNAL_H

#define _DISPLAY_INTERNAL_H

#include "commonTypes.h"

class HT1632Class;
class DisplayInternalInfo;

class DisplayInternal {

public:
  ~DisplayInternal();
  
  void displayTickFast();
  void displayTick250ms();
  void displayTick500ms();
  void displayTick1sec();
  void displayTick5sec();
  void displayTick10sec();
  void displayTick25sec();
  void displayTick1min();

  void doHandleMsgModePost(const StringMap& postValues);
  void doHandleImgBackgroundPost(const StringMap& postValues);
  const char* getDisplayModeStr() const;

private:
  DisplayInternal(HT1632Class& ht1632);

  DisplayInternalInfo* info;
  
  DisplayInternal() = delete;
  DisplayInternal(const DisplayInternal& other) = delete;
  DisplayInternal& operator=(const DisplayInternal& other) = delete;
  
  friend class Display;
};
  
#endif // _DISPLAY_INTERNAL_H
