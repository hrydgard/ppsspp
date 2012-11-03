set(SRCS
	ABI.cpp
	Action.cpp
	CPUDetect.cpp
	ColorUtil.cpp
	ConsoleListener.cpp
	ExtendedTrace.cpp
	#FPURoundModeX86.cpp
	FileSearch.cpp
	FileUtil.cpp
	Hash.cpp
	IniFile.cpp
	LogManager.cpp
	MathUtil.cpp
	MemArena.cpp
	MemoryUtil.cpp
	Misc.cpp
	MsgHandler.cpp
	StringUtil.cpp
	Thread.cpp
	Thunk.cpp
	Timer.cpp
	x64Analyzer.cpp
	x64Emitter.cpp
	x86Disasm.cpp
)

# TODO
if (ARM)
	set(SRCS ${SRCS} ArmEmitter.cpp ArmABI.cpp)
endif (ARM)

set(SRCS ${SRCS})

add_library(common STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)

