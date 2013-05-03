set(SRCS
	ColorUtil.cpp
	ConsoleListener.cpp
	ExtendedTrace.cpp
	#FPURoundModeX86.cpp
	FileSearch.cpp
	FileUtil.cpp
	Hash.cpp
	IniFile.cpp
	LogManager.cpp
	MemArena.cpp
	MemoryUtil.cpp
	Misc.cpp
	MsgHandler.cpp
	StringUtils.cpp
	Thread.cpp
	ThreadPools.cpp
	Timer.cpp
)

# TODO
if (ARM)
	set(SRCS ${SRCS} ArmEmitter.cpp ArmABI.cpp)
else()
	set(SRCS ${SRCS} CPUDetect.cpp MathUtil.cpp Thunk.cpp x64Analyzer.cpp x64Emitter.cpp ABI.cpp)
endif (ARM)

set(SRCS ${SRCS})

add_library(common STATIC ${SRCS})

if(UNIX)
	add_definitions(-fPIC)
endif(UNIX)

