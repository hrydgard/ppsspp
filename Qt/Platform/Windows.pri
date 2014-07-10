PRECOMPILED_HEADER = $$P/Windows/stdafx.h
PRECOMPILED_SOURCE = $$P/Windows/stdafx.cpp
INCLUDEPATH += $$P

!contains(CONFIG, staticlib) {
	# Executable
	# Use a fixed base-address under windows
	QMAKE_LFLAGS += /FIXED /BASE:"0x00400000" /DYNAMICBASE:NO
	LIBS += -lwinmm -lws2_32 -lShell32 -lAdvapi32
	contains(QMAKE_TARGET.arch, x86_64): LIBS += $$files($$P/dx9sdk/Lib/x64/*.lib)
	else: LIBS += $$files($$P/dx9sdk/Lib/x86/*.lib)

	# Packaging
	ICON = $$P/Windows/ppsspp.rc
}
