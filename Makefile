ifneq ($(V),1)
Q ?= @
endif

.DEFAULT_GOAL := all
.SUFFIXES:
.PHONY: all sudo_oclock hardware sandbox compatibility test test-core \
	check-arm-warnings smoke valgrind clean

# Keep the original CC override working even though every source is C++.
CC = g++
CXX = $(CC)
CPPFLAGS = -I/usr/local/include -I./mcp300x -I./ht1632 -I./lpd8806 -I./src -I./pulsar
CXXFLAGS ?= -g -O0
CXXFLAGS += -std=gnu++11 -Winline -pipe -Wall -Wextra
LDFLAGS ?=

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
HARDWARE_OBJ = $(addprefix build/hardware/,$(addsuffix .o,$(SRC)))
SANDBOX_OBJ = $(addprefix build/sandbox/,$(addsuffix .o,$(SRC))) \
	build/sandbox/src/fakeWiringPi.cpp.o
ARM_WARNING_OBJ = $(addprefix build/arm-warnings/,$(addsuffix .o,$(SRC))) \
	build/arm-warnings/src/fakeWiringPi.cpp.o

HARDWARE_LIBS = -lwiringPi -lpthread -levent -lmosquitto
SANDBOX_LIBS = -lpthread -levent -lmosquitto

all: sudo_oclock

# Keep the original `make` workflow intact for the deployed Raspberry Pi.
# `make hardware` is the build-only alternative for development and packaging.
sudo_oclock: oclock
	$Q sudo chown root:root oclock
	$Q sudo chmod u+s oclock

hardware: oclock

sandbox: oclock-sandbox

oclock: $(HARDWARE_OBJ)
	$Q echo "[Link] $@"
	$Q $(CXX) -o $@ $^ $(LDFLAGS) $(HARDWARE_LIBS)

oclock-sandbox: $(SANDBOX_OBJ)
	$Q echo "[Link] $@"
	$Q $(CXX) -o $@ $^ $(LDFLAGS) $(SANDBOX_LIBS)

# Pulsar's .c sources include the C++ request-handler boundary, so they are
# intentionally compiled as C++ until that interface is split cleanly.
build/hardware/%.cpp.o: %.cpp
	$Q echo "[Compile] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c $(CPPFLAGS) $(CXXFLAGS) $< -o $@

build/hardware/%.c.o: %.c
	$Q echo "[Compile] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c $(CPPFLAGS) $(CXXFLAGS) $< -o $@

build/sandbox/%.cpp.o: %.cpp
	$Q echo "[Compile sandbox] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c $(CPPFLAGS) $(CXXFLAGS) -DFAKE_WIRING $< -o $@

build/sandbox/%.c.o: %.c
	$Q echo "[Compile sandbox] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c $(CPPFLAGS) $(CXXFLAGS) -DFAKE_WIRING $< -o $@

build/arm-warnings/%.cpp.o: %.cpp
	$Q echo "[Compile ARM warning check] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c $(CPPFLAGS) $(CXXFLAGS) -DFAKE_WIRING \
		-funsigned-char -Werror $< -o $@

build/arm-warnings/%.c.o: %.c
	$Q echo "[Compile ARM warning check] $<"
	$Q mkdir -p $(@D)
	$Q $(CXX) -c $(CPPFLAGS) $(CXXFLAGS) -DFAKE_WIRING \
		-funsigned-char -Werror $< -o $@

build/tests/oclock-arm-warnings: $(ARM_WARNING_OBJ)
	$Q echo "[Link ARM warning check] $@"
	$Q mkdir -p $(@D)
	$Q $(CXX) -o $@ $^ $(LDFLAGS) $(SANDBOX_LIBS)

build/tests/core_tests: tests/core_tests.cpp src/inbox.cpp src/commonUtils.cpp \
		ht1632/HT1632.cpp lpd8806/LPD8806.cpp src/fakeWiringPi.cpp
	$Q echo "[Build test] $@"
	$Q mkdir -p $(@D)
	$Q $(CXX) $(CPPFLAGS) $(CXXFLAGS) -DFAKE_WIRING \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$^ -o $@ -lpthread

test-core: build/tests/core_tests
	$Q ASAN_OPTIONS=detect_leaks=1 ./build/tests/core_tests

smoke: oclock-sandbox
	$Q ./tests/smoke.sh ./oclock-sandbox

compatibility: oclock-sandbox
	$Q ./tests/compatibility.sh ./oclock-sandbox

check-arm-warnings: build/tests/oclock-arm-warnings
	$Q ./tests/smoke.sh ./build/tests/oclock-arm-warnings

test: compatibility test-core check-arm-warnings smoke

valgrind: oclock-sandbox
	$Q ./tests/valgrind-smoke.sh ./oclock-sandbox

clean:
	$Q echo "[Clean]"
	$Q rm -rf build oclock oclock-sandbox
	$Q rm -f log/pulsar.log *~ core tags cscope.*
