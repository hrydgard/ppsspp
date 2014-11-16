# Use a mkspec that allows g++
DEFINES += BLACKBERRY "_QNX_SOURCE=1" "_C99=1"

!contains(CONFIG, staticlib) {
	# Executable
	LIBS += -lscreen -liconv
}
