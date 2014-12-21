PLATFORM_ARCH="generic"

# Override some bad defaults
symbian: QMAKE_TARGET.arch=armv6

contains(QT_ARCH, ".*86.*")|contains(QMAKE_TARGET.arch, ".*86.*") {
	!win32-msvc*: QMAKE_ALLFLAGS += -msse2
	else: QMAKE_ALLFLAGS += /arch:SSE2
	CONFIG += i86

	contains(QT_ARCH, x86_64)|contains(QMAKE_TARGET.arch, x86_64) {
		PLATFORM_ARCH = x86_64
		DEFINES += _M_X64 _ARCH_64
	}
	else {
		PLATFORM_ARCH = x86
		DEFINES += _M_IX86 _ARCH_32
	}
} else:contains(QT_ARCH, ".*arm.*")|contains(QMAKE_TARGET.arch, ".*arm.*") {
	DEFINES += ARM _ARCH_32
	CONFIG += arm

	# Will need to see how QT_ARCH and QMAKE_TARGET.arch are populated for various ARM platforms.
	symbian: PLATFORM_ARCH="armv6"
	else {
		PLATFORM_ARCH="armv7"
		CONFIG += armv7
		QMAKE_CFLAGS_RELEASE ~= s/-mfpu.*/
		QMAKE_CFLAGS_DEBUG ~= s/-mfpu.*/
		QMAKE_ALLFLAGS_DEBUG += -march=armv7-a -mtune=cortex-a8 -mfpu=neon -ftree-vectorize
		QMAKE_ALLFLAGS_RELEASE += -march=armv7-a -mtune=cortex-a8 -mfpu=neon -ftree-vectorize
	}
	# TODO: aarch32/64?
} else:contains(QT_ARCH, ".*mips.*") {
	DEFINES += MIPS
	CONFIG += mips
	PLATFORM_ARCH="mips32"
	DEFINES += _ARCH_32
} else {
	# Generic
	warning("You are using an untested arch: $${QT_ARCH}. Only x86 and ARM CPUs are supported")
	DEFINES += GENERIC_ARCH
	CONFIG += generic
	PLATFORM_ARCH=$${QT_ARCH}
	DEFINES += _ARCH_32
}


# Odd one out
ios: PLATFORM_ARCH="universal"
