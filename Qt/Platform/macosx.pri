QMAKE_MAC_SDK=macosx10.9

!contains(CONFIG, staticlib) {
	# Executable
	LIBS += -liconv
}
