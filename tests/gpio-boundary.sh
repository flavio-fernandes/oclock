#!/usr/bin/env bash

set -euo pipefail

legacy_backend=src/gpio/wiringPiGpio.cpp
production_roots=(src ht1632 lpd8806 mcp300x)
direct_gpio_pattern='#include[[:space:]]*[<"]wiringPi\.h[>"]|(wiringPiSetupGpio|pinMode|digitalRead|digitalWrite|pwmWrite|analogRead|analogWrite|delay)[[:space:]]*\('

violations=$(grep -R -n -E \
    --include='*.c' --include='*.cpp' --include='*.h' \
    "${direct_gpio_pattern}" "${production_roots[@]}" |
    grep -v "^${legacy_backend}:" || true)
if [[ -n ${violations} ]]; then
    echo "direct WiringPi access exists outside ${legacy_backend}:" >&2
    echo "${violations}" >&2
    exit 1
fi

grep -q '#include <wiringPi.h>' "${legacy_backend}"
grep -q 'wiringPiSetupGpio' "${legacy_backend}"
if grep -n 'wiringPi' src/gpio/Gpio.h src/gpio/fakeGpio.cpp; then
    echo "the project interface or fake backend exposes WiringPi" >&2
    exit 1
fi

# Freeze the accepted BCM pin map before backend behavior is made testable.
grep -Fqx 'const int Display::pinCS = 6;' src/display.cpp
grep -Fqx 'const int Display::pinWR = 13;' src/display.cpp
grep -Fqx 'const int Display::pinDATA = 19;' src/display.cpp
grep -Fqx 'const int Display::pinCLK = 26;' src/display.cpp
grep -Fqx 'const Int8U LedStrip::pinDATA = 21;' src/ledStrip.cpp
grep -Fqx 'const Int8U LedStrip::pinCLK = 20;' src/ledStrip.cpp
grep -Fqx 'const int LightSensor::pinClock = 17;' src/lightSensor.cpp
grep -Fqx 'const int LightSensor::pinDigitalOut = 27;' src/lightSensor.cpp
grep -Fqx 'const int LightSensor::pinDigitalIn = 22;' src/lightSensor.cpp
grep -Fqx 'const int LightSensor::pinChipSelect = 4;' src/lightSensor.cpp
grep -Fqx 'const int MotionSensor::sensorGpioPin = 10; // 18;' \
    src/motionSensor.cpp

hardware_build=$(make -Bn hardware)
gpiod_build=$(make -Bn GPIO_BACKEND=gpiod hardware)
sandbox_build=$(make -Bn sandbox)

grep -q 'src/gpio/wiringPiGpio.cpp' <<<"${hardware_build}"
grep -q -- '-lwiringPi' <<<"${hardware_build}"
grep -q 'src/gpio/gpiodV2Gpio.cpp' <<<"${gpiod_build}"
grep -q -- '-lgpiod' <<<"${gpiod_build}"
if grep -q 'src/gpio/wiringPiGpio.cpp' <<<"${gpiod_build}" ||
        grep -q -- '-lwiringPi' <<<"${gpiod_build}"; then
    echo "libgpiod build unexpectedly selects or links WiringPi" >&2
    exit 1
fi
grep -q 'src/gpio/fakeGpio.cpp' <<<"${sandbox_build}"
if grep -q 'src/gpio/wiringPiGpio.cpp' <<<"${sandbox_build}" ||
        grep -q -- '-lwiringPi' <<<"${sandbox_build}"; then
    echo "sandbox build unexpectedly selects or links WiringPi" >&2
    exit 1
fi

if make -Bn GPIO_BACKEND=unknown hardware >/dev/null 2>&1; then
    echo "unknown GPIO backend was accepted" >&2
    exit 1
fi

echo "GPIO boundary tests passed"
