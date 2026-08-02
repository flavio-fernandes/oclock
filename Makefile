ifneq ($(V),1)
Q ?= @
endif

.DEFAULT_GOAL := all
.SUFFIXES:
.PHONY: all hardware sandbox hardware-preflight \
	compatibility gpio-boundary test \
	test-core test-gpio-protocols test-gpio-registers \
	test-spi-output test-iio-analog test-spi-overlay check-arm-warnings \
	smoke test-shutdown valgrind spi-overlay \
	phase5-lpd8806-all-off phase5-lpd8806-all-off-2mhz \
	phase5-lpd8806-colors-2mhz \
	phase5-mcp3002-read clean

# Keep the original CC override working even though every source is C++.
CC = g++
CXX = $(CC)
CPPFLAGS = -I/usr/local/include -I./mcp300x -I./ht1632 -I./lpd8806 -I./src -I./pulsar
CXXFLAGS ?= -g -O0
CXXFLAGS += -std=gnu++11 -Winline -pipe -Wall -Wextra
LDFLAGS ?=
DTC ?= dtc

ifneq ($(origin GPIO_BACKEND),undefined)
$(error GPIO_BACKEND was removed; hardware builds use the selected modern GPIO path)
endif
ifneq ($(origin STRIP_TRANSPORT),undefined)
$(error STRIP_TRANSPORT was removed; hardware builds use the selected spidev strip path)
endif

PULSAR_SRC = \
	pulsar/logger.c \
	pulsar/conf.c \
	pulsar/worker.c \
	pulsar/server.c \
	pulsar/pulsar.c

CPP_SRC = \
	ht1632/HT1632.cpp \
	lpd8806/LPD8806.cpp \
	src/webHandlerInternal.cpp \
	src/dictionary.cpp \
	src/motionInput.cpp \
	src/motionSensor.cpp \
	src/lightSensor.cpp \
	src/mqttClient.cpp \
	src/inbox.cpp \
	src/timerTick.cpp \
	src/display.cpp \
	src/displayInternal.cpp \
	src/ledStrip.cpp \
	src/ledStripInternal.cpp \
	src/commonUtils.cpp \
	src/main.cpp

SRC = $(PULSAR_SRC) $(CPP_SRC)
HARDWARE_GPIO_SRC = src/gpio/gpiodV2Gpio.cpp \
	src/gpio/bcm2835GpioRegisters.cpp \
	src/gpio/bcm2835MmapValueIo.cpp \
	src/gpio/gpiodMmapFactory.cpp
HARDWARE_SPI_SRC = src/spi/linuxSpidevOutput.cpp
HARDWARE_ADC_SRC = src/adc/linuxIioAnalogInput.cpp
HARDWARE_SRC = $(SRC) $(HARDWARE_GPIO_SRC) $(HARDWARE_SPI_SRC) \
	$(HARDWARE_ADC_SRC)
SANDBOX_SRC = $(SRC) src/gpio/fakeGpio.cpp src/spi/noSpiOutput.cpp \
	src/adc/fakeAnalogInput.cpp
HARDWARE_OBJ = $(addprefix build/hardware/,$(addsuffix .o,$(HARDWARE_SRC)))
SANDBOX_OBJ = $(addprefix build/sandbox/,$(addsuffix .o,$(SANDBOX_SRC)))
ARM_WARNING_OBJ = $(addprefix build/arm-warnings/,$(addsuffix .o,$(SANDBOX_SRC)))

# ARMv6 cannot implement every 64-bit std::atomic operation inline. GCC emits
# calls into libatomic for the selected Trixie toolchain.
HARDWARE_LIBS = -lgpiod -latomic -lpthread -levent -lmosquitto
SANDBOX_LIBS = -lpthread -levent -lmosquitto

all: hardware

hardware: oclock

hardware-preflight:
	$Q version=$$(pkg-config --modversion libgpiod 2>/dev/null) || { \
		echo "error: libgpiod v2 development files are required" >&2; exit 1; }; \
	case "$${version}" in 2.*) ;; *) \
		echo "error: libgpiod v2 is required (found $${version})" >&2; exit 1;; esac

sandbox: oclock-sandbox

oclock: $(HARDWARE_OBJ)
	$Q echo "[Link] $@"
	$Q $(CXX) -o $@ $(HARDWARE_OBJ) $(LDFLAGS) $(HARDWARE_LIBS)

oclock-sandbox: $(SANDBOX_OBJ)
	$Q echo "[Link] $@"
	$Q $(CXX) -o $@ $^ $(LDFLAGS) $(SANDBOX_LIBS)

# Pulsar's .c sources include the C++ request-handler boundary, so they are
# intentionally compiled as C++ until that interface is split cleanly.
build/hardware/%.cpp.o: %.cpp | hardware-preflight
	$Q echo "[Compile] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c $(CPPFLAGS) $(CXXFLAGS) $< -o $@

build/hardware/%.c.o: %.c | hardware-preflight
	$Q echo "[Compile] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c $(CPPFLAGS) $(CXXFLAGS) $< -o $@

build/sandbox/%.cpp.o: %.cpp
	$Q echo "[Compile sandbox] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c $(CPPFLAGS) $(CXXFLAGS) $< -o $@

build/sandbox/%.c.o: %.c
	$Q echo "[Compile sandbox] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c $(CPPFLAGS) $(CXXFLAGS) $< -o $@

build/arm-warnings/%.cpp.o: %.cpp
	$Q echo "[Compile ARM warning check] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c $(CPPFLAGS) $(CXXFLAGS) \
		-funsigned-char -Werror $< -o $@

build/arm-warnings/%.c.o: %.c
	$Q echo "[Compile ARM warning check] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c $(CPPFLAGS) $(CXXFLAGS) \
		-funsigned-char -Werror $< -o $@

build/tests/oclock-arm-warnings: $(ARM_WARNING_OBJ)
	$Q echo "[Link ARM warning check] $@"
	$Q mkdir -p $(@D)
	$Q $(CXX) -o $@ $^ $(LDFLAGS) $(SANDBOX_LIBS)

build/tests/core_tests: tests/core_tests.cpp src/inbox.cpp src/commonUtils.cpp \
		ht1632/HT1632.cpp lpd8806/LPD8806.cpp src/gpio/fakeGpio.cpp
	$Q echo "[Build test] $@"
	$Q mkdir -p $(@D)
	$Q $(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$^ -o $@ -lpthread

test-core: build/tests/core_tests
	$Q ASAN_OPTIONS=detect_leaks=1 ./build/tests/core_tests

build/tests/gpio_protocol_tests: tests/gpio_protocol_tests.cpp \
		src/motionInput.cpp mcp300x/mcp300x.cpp ht1632/HT1632.cpp \
		lpd8806/LPD8806.cpp src/gpio/fakeGpio.cpp
	$Q echo "[Build test] $@"
	$Q mkdir -p $(@D)
	$Q $(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$^ -o $@ -lpthread

test-gpio-protocols: build/tests/gpio_protocol_tests
	$Q ASAN_OPTIONS=detect_leaks=1 ./build/tests/gpio_protocol_tests

build/tests/gpio_register_tests: tests/gpio_register_tests.cpp \
		src/gpio/bcm2835GpioRegisters.cpp
	$Q echo "[Build test] $@"
	$Q mkdir -p $(@D)
	$Q $(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-funsigned-char -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$^ -o $@

test-gpio-registers: build/tests/gpio_register_tests
	$Q ASAN_OPTIONS=detect_leaks=1 ./build/tests/gpio_register_tests

build/tests/spi_output_tests: tests/spi_output_tests.cpp \
		lpd8806/LPD8806.cpp src/gpio/fakeGpio.cpp \
		src/spi/fakeSpiOutput.cpp
	$Q echo "[Build test] $@"
	$Q mkdir -p $(@D)
	$Q $(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-funsigned-char -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$^ -o $@ -lpthread

build/tests/linuxSpidevOutput.cpp.o: src/spi/linuxSpidevOutput.cpp
	$Q echo "[Compile spidev transport] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c $(CPPFLAGS) $(CXXFLAGS) \
		-funsigned-char -Werror $< -o $@

build/phase5-lpd8806-all-off: misc/phase5Lpd8806AllOff.cpp \
		lpd8806/LPD8806.cpp src/gpio/fakeGpio.cpp \
		src/spi/linuxSpidevOutput.cpp
	$Q echo "[Build Phase 5 1 MHz all-off transfer tool] $@"
	$Q mkdir -p $(@D)
	$Q $(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-DOCLOCK_STRIP_SPEED_HZ=1000000U \
		-funsigned-char -Werror $^ -o $@ -lpthread

phase5-lpd8806-all-off: build/phase5-lpd8806-all-off

build/phase5-lpd8806-all-off-2mhz: misc/phase5Lpd8806AllOff.cpp \
		lpd8806/LPD8806.cpp src/gpio/fakeGpio.cpp \
		src/spi/linuxSpidevOutput.cpp
	$Q echo "[Build Phase 5 2 MHz all-off transfer tool] $@"
	$Q mkdir -p $(@D)
	$Q $(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-DOCLOCK_STRIP_SPEED_HZ=2000000U \
		-funsigned-char -Werror $^ -o $@ -lpthread

phase5-lpd8806-all-off-2mhz: build/phase5-lpd8806-all-off-2mhz

build/phase5-lpd8806-colors-2mhz: misc/phase5Lpd8806Colors.cpp \
		lpd8806/LPD8806.cpp src/gpio/fakeGpio.cpp \
		src/spi/linuxSpidevOutput.cpp
	$Q echo "[Build Phase 5 2 MHz color transfer tool] $@"
	$Q mkdir -p $(@D)
	$Q $(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-DOCLOCK_STRIP_SPEED_HZ=2000000U \
		-funsigned-char -Werror $^ -o $@ -lpthread

phase5-lpd8806-colors-2mhz: build/phase5-lpd8806-colors-2mhz

build/phase5-mcp3002-read: misc/phase5Mcp3002Read.cpp \
		src/adc/linuxIioAnalogInput.cpp
	$Q echo "[Build Phase 5 MCP3002 reader] $@"
	$Q mkdir -p $(@D)
	$Q $(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-funsigned-char -Werror $^ -o $@ -lpthread

phase5-mcp3002-read: build/phase5-mcp3002-read

test-spi-output: build/tests/spi_output_tests \
		build/tests/linuxSpidevOutput.cpp.o \
		build/phase5-lpd8806-all-off
	$Q ASAN_OPTIONS=detect_leaks=1 ./build/tests/spi_output_tests

build/tests/iio_analog_input_tests: tests/iio_analog_input_tests.cpp \
		src/adc/linuxIioAnalogInput.cpp
	$Q echo "[Build test] $@"
	$Q mkdir -p $(@D)
	$Q $(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		-funsigned-char -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$^ -o $@ -lpthread

test-iio-analog: build/tests/iio_analog_input_tests build/phase5-mcp3002-read
	$Q ASAN_OPTIONS=detect_leaks=1 ./build/tests/iio_analog_input_tests
	$Q test -x build/phase5-mcp3002-read

smoke: oclock-sandbox
	$Q ./tests/smoke.sh ./oclock-sandbox

test-shutdown: oclock-sandbox
	$Q ./tests/shutdown-stress.sh ./oclock-sandbox

compatibility: oclock-sandbox
	$Q ./tests/compatibility.sh ./oclock-sandbox

gpio-boundary:
	$Q ./tests/gpio-boundary.sh

check-arm-warnings: build/tests/oclock-arm-warnings
	$Q ./tests/smoke.sh ./build/tests/oclock-arm-warnings

test: compatibility gpio-boundary test-core test-gpio-protocols \
	test-gpio-registers test-spi-output test-iio-analog \
	check-arm-warnings smoke test-shutdown

valgrind: oclock-sandbox
	$Q ./tests/valgrind-smoke.sh ./oclock-sandbox

spi-overlay: build/oclock-spi-overlay.dtbo

build/oclock-spi-overlay.dtbo: hardware/oclock-spi-overlay.dts
	$Q echo "[Compile Device Tree overlay] $<"
	$Q mkdir -p $(@D)
	$Q $(DTC) -@ -I dts -O dtb -o $@ $<

test-spi-overlay: build/oclock-spi-overlay.dtbo
	$Q ./tests/spi-overlay.sh $<

clean:
	$Q echo "[Clean]"
	$Q rm -rf build oclock oclock-sandbox
	$Q rm -f log/pulsar.log *~ core tags cscope.*
