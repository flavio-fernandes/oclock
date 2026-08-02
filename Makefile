ifneq ($(V),1)
Q ?= @
endif

.DEFAULT_GOAL := all
.SUFFIXES:
.PHONY: all sudo_oclock hardware sandbox gpio-backend-preflight \
	compatibility gpio-boundary test \
	test-core test-gpio-protocols test-wiringpi-compile check-arm-warnings \
	smoke test-shutdown valgrind clean FORCE

# Keep the original CC override working even though every source is C++.
CC = g++
CXX = $(CC)
CPPFLAGS = -I/usr/local/include -I./mcp300x -I./ht1632 -I./lpd8806 -I./src -I./pulsar
CXXFLAGS ?= -g -O0
CXXFLAGS += -std=gnu++11 -Winline -pipe -Wall -Wextra
LDFLAGS ?=

GPIO_BACKEND ?= wiringpi
VALID_GPIO_BACKENDS := wiringpi gpiod
ifeq ($(filter $(GPIO_BACKEND),$(VALID_GPIO_BACKENDS)),)
$(error unsupported GPIO_BACKEND '$(GPIO_BACKEND)'; expected one of: $(VALID_GPIO_BACKENDS))
endif

PULSAR_SRC = \
	pulsar/logger.c \
	pulsar/conf.c \
	pulsar/worker.c \
	pulsar/server.c \
	pulsar/pulsar.c

CPP_SRC = \
	mcp300x/mcp300x.cpp \
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
ifeq ($(GPIO_BACKEND),gpiod)
HARDWARE_GPIO_SRC = src/gpio/gpiodV2Gpio.cpp
HARDWARE_GPIO_LIB = -lgpiod
else
HARDWARE_GPIO_SRC = src/gpio/wiringPiGpio.cpp
HARDWARE_GPIO_LIB = -lwiringPi
endif
HARDWARE_SRC = $(SRC) $(HARDWARE_GPIO_SRC)
SANDBOX_SRC = $(SRC) src/gpio/fakeGpio.cpp
HARDWARE_OBJ = $(addprefix build/hardware/,$(addsuffix .o,$(HARDWARE_SRC)))
SANDBOX_OBJ = $(addprefix build/sandbox/,$(addsuffix .o,$(SANDBOX_SRC)))
ARM_WARNING_OBJ = $(addprefix build/arm-warnings/,$(addsuffix .o,$(SANDBOX_SRC)))

HARDWARE_LIBS = $(HARDWARE_GPIO_LIB) -lpthread -levent -lmosquitto
SANDBOX_LIBS = -lpthread -levent -lmosquitto

all: sudo_oclock

# Keep the original `make` workflow intact for the deployed Raspberry Pi.
# `make hardware` is the build-only alternative for development and packaging.
sudo_oclock: oclock
	$Q sudo chown root:root oclock
	$Q sudo chmod u+s oclock

hardware: oclock

gpio-backend-preflight:
ifeq ($(GPIO_BACKEND),gpiod)
	$Q version=$$(pkg-config --modversion libgpiod 2>/dev/null) || { \
		echo "error: libgpiod v2 development files are required" >&2; exit 1; }; \
	case "$${version}" in 2.*) ;; *) \
		echo "error: libgpiod v2 is required (found $${version})" >&2; exit 1;; esac
endif

sandbox: oclock-sandbox

oclock: $(HARDWARE_OBJ) FORCE
	$Q echo "[Link] $@"
	$Q $(CXX) -o $@ $(HARDWARE_OBJ) $(LDFLAGS) $(HARDWARE_LIBS)

# Backend selection changes the object and library lists without changing the
# output filename. Relink every requested hardware build so switching an
# existing tree between WiringPi and libgpiod cannot leave a stale executable.
FORCE:

oclock-sandbox: $(SANDBOX_OBJ)
	$Q echo "[Link] $@"
	$Q $(CXX) -o $@ $^ $(LDFLAGS) $(SANDBOX_LIBS)

# Pulsar's .c sources include the C++ request-handler boundary, so they are
# intentionally compiled as C++ until that interface is split cleanly.
build/hardware/%.cpp.o: %.cpp | gpio-backend-preflight
	$Q echo "[Compile] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c $(CPPFLAGS) $(CXXFLAGS) $< -o $@

build/hardware/%.c.o: %.c | gpio-backend-preflight
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

build/tests/wiringPiGpio.cpp.o: src/gpio/wiringPiGpio.cpp \
		tests/support/wiringPi.h
	$Q echo "[Compile legacy backend] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c -Itests/support $(CPPFLAGS) $(CXXFLAGS) \
		-funsigned-char -Werror $< -o $@

test-wiringpi-compile: build/tests/wiringPiGpio.cpp.o

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
	test-wiringpi-compile check-arm-warnings smoke test-shutdown

valgrind: oclock-sandbox
	$Q ./tests/valgrind-smoke.sh ./oclock-sandbox

clean:
	$Q echo "[Clean]"
	$Q rm -rf build oclock oclock-sandbox
	$Q rm -f log/pulsar.log *~ core tags cscope.*
