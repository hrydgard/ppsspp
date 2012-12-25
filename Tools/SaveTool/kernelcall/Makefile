release: all 
	psp-build-exports -k exports.exp 
	psp-build-exports -s -k -v exports.exp

TARGET = kernelcall
OBJS = kernelcall.o

BUILD_PRX=1
PRX_EXPORTS=exports.exp
USE_KERNEL_LIBS = 1
USE_KERNEL_LIBC = 1

INCDIR = 
CFLAGS = -O0 -G0 -Wall -g
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBDIR =
LDFLAGS = -mno-crt0 -nostartfiles

LIBS = -lpspchnnlsv

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
