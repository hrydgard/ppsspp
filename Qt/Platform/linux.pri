

!contains(CONFIG, staticlib) {
	# Executable
	LIBS += -ldl -lrt

	# Packaging
	icon.files = $$P/assets/icon-114.png
	icon.path = /usr/share/icons/hicolor/114x114/apps
	INSTALLS += icon
}
