SUBDIRS = kernelcall 
release:  
	@for i in $(SUBDIRS); do echo "make all in $$i...";  (cd $$i; $(MAKE)  release; cp $$i.prx ..);  done 
	make all 
	
allclean: 
	make clean 
	@for i in $(SUBDIRS); do echo "Clearing in $$i..."; (cd $$i; $(MAKE) clean; rm -rf $$i.prx); done

TARGET = ppssppsavetool
OBJS = main.o decrypt.o encrypt.o hash.o psf.o $(KERNELCALL_OBJS)

BUILD_PRX=1


EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PPSSPP Save Tool

PSP_FW_VERSION=350

INCDIR = 
CFLAGS = -O0 -G0 -Wall -g
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

KERNELCALL_OBJS = 	kernelcall_0000.o \
					kernelcall_0001.o \
					kernelcall_0002.o \
					kernelcall_0003.o \
					kernelcall_0004.o \
					kernelcall_0005.o \
					kernelcall_0006.o \
					kernelcall_0007.o

$(KERNELCALL_OBJS): kernelcall/kernelcall.S 
	psp-gcc $(CFLAGS) -DF_$* $< -c -o $@

LIBDIR =
LDFLAGS =

LIBS = -lpsputility -lpspgum -lpspgu -lm

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
