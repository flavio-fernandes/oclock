#ifndef OCLOCK_TEST_SUPPORT_WIRING_PI_H
#define OCLOCK_TEST_SUPPORT_WIRING_PI_H

#define INPUT 0
#define OUTPUT 1
#define LOW 0
#define HIGH 1

int wiringPiSetupGpio(void);
void pinMode(int pin, int mode);
int digitalRead(int pin);
void digitalWrite(int pin, int value);
void delay(unsigned int duration);

#endif
