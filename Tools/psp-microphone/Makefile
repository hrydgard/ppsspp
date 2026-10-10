TARGET = micdemo
OBJS = main.o
CFLAGS = -O2 -G0 -Wall -std=gnu99
CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)
LIBS = -lpspaudio -lpspctrl -lpspdisplay
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSP Microphone Demo
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
