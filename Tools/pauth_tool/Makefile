
TARGET = pauth
OBJS = main.o pauth.o

CFLAGS = -O2 -G0 -Wall -I.
LDFLAGS = -mno-crt0 -nostartfiles -L.
LIBS +=

BUILD_PRX = 1

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = pauth file decrypter

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

