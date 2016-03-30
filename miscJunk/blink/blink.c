#include <stdio.h>
#include <wiringPi.h>

#define LED_GPIO     26

int main (void)
{
  printf ("Raspberry Pi Quick2Wire blink board rev %d\n", piBoardRev()) ;

  // wiringPiSetup () ;
  wiringPiSetupGpio();

  pinMode (LED_GPIO, OUTPUT) ;

  for (;;)
  {
    digitalWrite (LED_GPIO, HIGH) ;  // On
    delay (100) ;               // mS
    digitalWrite (LED_GPIO, LOW) ;   // Off
    delay (100) ;
  }
  return 0 ;
}

