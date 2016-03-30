ifneq ($V,1)
Q ?= @
endif

ifneq ($D,1)
#DEBUG	= -O2
DEBUG	= -g -O0
else
DEBUG	= -g -O0 -DFAKE_WIRING
endif

.SUFFIXES: .c .cpp .o
.PHONY:	clean

CC	= g++
INCLUDE	= -I/usr/local/include -I./mcp300x -I./ht1632 -I./lpd8806 -I./src -I./pulsar
CFLAGS	= $(DEBUG) $(INCLUDE) -std=gnu++11 -Winline -pipe -Wall

LDFLAGS	= 
LIBS    = -lwiringPi -lpthread -levent
VALGRIND_LIBS    = -lpthread -levent

PULSAR_SRC_DIR	= pulsar
PULSAR_SRC = $(addprefix $(PULSAR_SRC_DIR)/, logger.c conf.c worker.c server.c pulsar.c)

SRC	= \
	  mcp300x/mcp300x.cpp \
	  ht1632/HT1632.cpp \
	  lpd8806/LPD8806.cpp \
	  $(addprefix src/, \
	    webHandlerInternal.cpp \
	    motionSensor.cpp \
	    lightSensor.cpp \
	    inbox.cpp \
	    timerTick.cpp \
	    display.cpp \
	    displayInternal.cpp \
	    ledStrip.cpp \
	    ledStripInternal.cpp \
	    commonUtils.cpp \
	    main.cpp \
	  )

OBJ	= $(PULSAR_SRC:.c=.o) $(SRC:.cpp=.o)

all:	sudo_oclock

# valgrind: build with -DFAKE_WIRING
# V=1 D=1 make valgrind
# time valgrind --leak-check=full --show-reachable=yes -v ./oclock
valgrind: $(OBJ) src/fakeWiringPi.o
	$Q echo [Valgrind Link]
	$Q $(CC) -o oclock $(OBJ) src/fakeWiringPi.o $(LDFLAGS) $(VALGRIND_LIBS)
	$Q echo "hint:  time valgrind --leak-check=full --show-reachable=yes ./oclock"

sudo_oclock: oclock
	$Q sudo chown root:root oclock
	$Q sudo chmod u+s oclock

oclock:	$(OBJ)
	$Q echo [Link]
	$Q $(CC) -o $@ $(OBJ) $(LDFLAGS) $(LIBS)

.cpp.o:
	$Q echo [Compile] $<
	$Q $(CC) -c $(CFLAGS) $< -o $@

.c.o:
	$Q echo [CompileC] $<
	$Q $(CC) -c $(CFLAGS) $< -o $@

clean:
	$Q echo "[Clean]"
	$Q rm -f $(OBJ) src/fakeWiringPi.o oclock *~ core tags cscope.* log/pulsar.log

