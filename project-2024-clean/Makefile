# For TMote Sky (emulated in Cooja) use the following target
TARGET ?= sky

DEFINES=PROJECT_CONF_H=\"project-conf.h\"
CONTIKI_PROJECT = app

# For Zolertia Firefly (testbed) use the following target and board
# Don't forget to make clean when you are changing the board
ifeq ($(TARGET), zoul)
	BOARD	?= firefly
	LDFLAGS += -specs=nosys.specs # For Zolertia Firefly only
	PROJECTDIRS += testbed_files
	PROJECT_SOURCEFILES += deployment.c
endif

# Add Routing Protocol source code file for compilation
# Other files may be added in the same way
PROJECT_SOURCEFILES += rp.c

PROJECTDIRS += tools
PROJECT_SOURCEFILES += simple-energest.c

all: $(CONTIKI_PROJECT)

CONTIKI_WITH_RIME = 1
CONTIKI ?= ../../contiki
include $(CONTIKI)/Makefile.include
