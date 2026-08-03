#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>

#include "HT1632.h"
#include "LPD8806.h"
#include "commonUtils.h"
#include "gpio/Gpio.h"
#include "inbox.h"

static void testPrequeuedInboxMessage() {
  Inbox inbox;
  inbox.addMessage(InboxMsg(inboxMsgTypeMotionOn));

  const auto started = std::chrono::steady_clock::now();
  const InboxMsg received = inbox.waitForMessage();
  const auto elapsed = std::chrono::steady_clock::now() - started;

  assert(received.inboxMsgType == inboxMsgTypeMotionOn);
  assert(elapsed < std::chrono::milliseconds(100));
  assert(inbox.empty());
  assert(inbox.getMsgCount() == 0);
}

static void testRandomBounds() {
  assert(getRandomNumber(0) == 0);
  assert(getRandomNumber(1) == 0);
  for (int i = 0; i < 10000; ++i) {
    assert(getRandomNumber(17) < 17);
  }
}

static void testLongTextAndUninitializedDisplayCleanup() {
  std::recursive_mutex gpioMutex;
  std::unique_ptr<Gpio> gpio = createGpio();
  assert(gpio->initialize());
  HT1632Class display(&gpioMutex, *gpio);
  char font[64] = {};
  char widths[64];
  std::fill(widths, widths + 64, 1);

  const std::string message(300, 'A');
  const int width = display.getTextWidth(message.c_str(), widths, 1);
  assert(width == 599);

  display.begin(8, 7, 4, 11);
  display.drawTarget(BUFFER_BOARD(1));
  display.drawText(message.c_str(), 0, 0, font, widths, 1, 1);

  const char image[] = {0x0f, 0x0f};
  display.drawImage(image, 2, 4, -1, -1);
  display.render();
}

static void testEmptyLedStrip() {
  std::recursive_mutex gpioMutex;
  std::unique_ptr<Gpio> gpio = createGpio();
  assert(gpio->initialize());
  LPD8806 strip(&gpioMutex, *gpio, 0, 1, 2);
  strip.clearPixelColors();
  strip.show();
  assert(strip.numPixels() == 0);
}

int main() {
  testPrequeuedInboxMessage();
  testRandomBounds();
  testLongTextAndUninitializedDisplayCleanup();
  testEmptyLedStrip();
  std::cout << "core tests passed\n";
  return 0;
}
