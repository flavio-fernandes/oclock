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
if grep -Eq 'pinClock|pinDigitalOut|pinDigitalIn|pinChipSelect' \
        src/lightSensor.cpp src/lightSensor.h; then
    echo "modern light sensor still owns legacy ADC GPIO pins" >&2
    exit 1
fi
grep -Fqx 'const int MotionSensor::sensorGpioPin = 10; // 18;' \
    src/motionSensor.cpp

default_build=$(make -Bn)
hardware_build=$(make -Bn hardware)
sandbox_build=$(make -Bn sandbox)

for source in src/gpio/gpiodV2Gpio.cpp \
        src/gpio/bcm2835GpioRegisters.cpp \
        src/gpio/bcm2835MmapValueIo.cpp \
		src/gpio/gpiodMmapFactory.cpp \
		src/spi/linuxSpidevOutput.cpp \
		src/adc/linuxIioAnalogInput.cpp; do
    grep -q "${source}" <<<"${hardware_build}"
    grep -q "${source}" <<<"${default_build}"
done
grep -q -- '-lgpiod' <<<"${hardware_build}"
grep -q -- '-latomic' <<<"${hardware_build}"
if grep -q 'src/gpio/wiringPiGpio.cpp' <<<"${hardware_build}" ||
        grep -q 'mcp300x/mcp300x.cpp' <<<"${hardware_build}" ||
        grep -q 'src/adc/fakeAnalogInput.cpp' <<<"${hardware_build}" ||
        grep -q 'src/spi/noSpiOutput.cpp' <<<"${hardware_build}" ||
        grep -q -- '-lwiringPi' <<<"${hardware_build}"; then
    echo "modern hardware build contains a retired transport" >&2
    exit 1
fi
grep -q 'src/gpio/fakeGpio.cpp' <<<"${sandbox_build}"
grep -q 'src/spi/noSpiOutput.cpp' <<<"${sandbox_build}"
grep -q 'src/adc/fakeAnalogInput.cpp' <<<"${sandbox_build}"
if grep -q 'src/gpio/wiringPiGpio.cpp' <<<"${sandbox_build}" ||
        grep -q 'mcp300x/mcp300x.cpp' <<<"${sandbox_build}" ||
        grep -q 'src/adc/linuxIioAnalogInput.cpp' <<<"${sandbox_build}" ||
        grep -q -- '-lwiringPi' <<<"${sandbox_build}"; then
    echo "sandbox build unexpectedly selects or links WiringPi" >&2
    exit 1
fi

if make -Bn GPIO_BACKEND=wiringpi hardware >/dev/null 2>&1; then
    echo "retired GPIO_BACKEND knob was accepted" >&2
    exit 1
fi
if make -Bn STRIP_TRANSPORT=spidev hardware >/dev/null 2>&1; then
    echo "retired STRIP_TRANSPORT knob was accepted" >&2
    exit 1
fi

grep -Fq '"/oclock-strip-spi/lpd8806@0"' \
    src/spi/linuxSpidevOutput.cpp
if grep -Eq '/dev/spidev[0-9]+\.[0-9]+' src/spi/linuxSpidevOutput.cpp; then
    echo "spidev transport hard-codes a dynamic device number" >&2
    exit 1
fi
grep -Fq '"/oclock-adc-spi/mcp3002@0"' \
    src/adc/linuxIioAnalogInput.cpp
if grep -Eq '/sys/bus/iio/devices/iio:device[0-9]+' \
        src/adc/linuxIioAnalogInput.cpp; then
    echo "IIO input hard-codes a dynamic device number" >&2
    exit 1
fi

echo "GPIO boundary tests passed"
