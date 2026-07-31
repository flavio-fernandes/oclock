#ifndef OCLOCK_MOTION_INPUT_H
#define OCLOCK_MOTION_INPUT_H

class Gpio;

void configureMotionInput(Gpio& gpio, int bcmGpio);
bool readMotionDetected(Gpio& gpio, int bcmGpio);

#endif
