QMAKE_MAC_SDK=macosx10.9

equals(TARGET, PPSSPPQt) {
	# Executable
	LIBS += -liconv
}
