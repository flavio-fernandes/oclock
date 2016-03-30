#ifdef FAKE_WIRING

#ifndef	__WIRING_PI_H__
#define	__WIRING_PI_H__

#define	INPUT			 0
#define	OUTPUT			 1
#define	LOW			 0
#define	HIGH			 1

extern int wiringPiSetupGpio    (void);
extern void delay               (unsigned int howLong);
extern void pinMode             (int pin, int mode);
extern int  digitalRead         (int pin);
extern void digitalWrite        (int pin, int value);
extern void pwmWrite            (int pin, int value);
extern int  analogRead          (int pin);
extern void analogWrite         (int pin, int value);

#endif // ifndef	__WIRING_PI_H__
#endif // ifdef  FAKE_WIRING
