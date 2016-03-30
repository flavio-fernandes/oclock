#ifdef FAKE_WIRING

#include <chrono>
#include <thread>

#include "fakeWiringPi.h"

int wiringPiSetupGpio(void) { return 0; }

void delay (unsigned int howLong) {
  const std::chrono::milliseconds sleepInterval(howLong);
  std::this_thread::sleep_for(sleepInterval);  // sleep for a tick
}

void pinMode(int /*pin*/, int /*mode*/) { }
int  digitalRead         (int /*pin*/) { return 0; }
void digitalWrite        (int /*pin*/, int /*value*/) {}
void pwmWrite            (int /*pin*/, int /*value*/) {}
int  analogRead          (int /*pin*/) { return 0; }
void analogWrite         (int /*pin*/, int /*value*/) {}

#endif // ifdef  FAKE_WIRING
