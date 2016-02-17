QT_CONFIG -= no-pkg-config

!contains(CONFIG, staticlib) {
	# Executable
	LIBS += -liconv
}
