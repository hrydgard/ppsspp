# Use a mkspec that allows g++
DEFINES += BLACKBERRY "_QNX_SOURCE=1" "_C99=1"

equals(TARGET, PPSSPPQt) {
	# Executable
	LIBS += -lscreen -liconv
}
