

!contains(CONFIG, staticlib) {
	isEmpty(PREFIX) {
		PREFIX = /usr
	}

	# Executable
	LIBS += $$QMAKE_LIBS_DYNLOAD # dlopen
	linux-*|hpux-*|solaris-*: LIBS += -lrt # clock_gettime

	# Packaging
	icon16.files = $$P/assets/unix-icons/hicolor/16x16/apps/ppsspp.png
        icon16.path = $$DESTDIR$$PREFIX/share/icons/hicolor/16x16/apps

	icon32.files = $$P/assets/unix-icons/hicolor/32x32/apps/ppsspp.png
        icon32.path = $$DESTDIR$$PREFIX/share/icons/hicolor/32x32/apps

	icon64.files = $$P/assets/unix-icons/hicolor/64x64/apps/ppsspp.png
        icon64.path = $$DESTDIR$$PREFIX/share/icons/hicolor/64x64/apps

	icon96.files = $$P/assets/unix-icons/hicolor/96x96/apps/ppsspp.png
	icon96.path = $$DESTDIR$$PREFIX/share/icons/hicolor/96x96/apps

	icon256.files = $$P/assets/unix-icons/hicolor/256x256/apps/ppsspp.png
        icon256.path = $$DESTDIR$$PREFIX/share/icons/hicolor/256x256/apps

	icon512.files = $$P/assets/unix-icons/hicolor/512x512/apps/ppsspp.png
        icon512.path = $$DESTDIR$$PREFIX/share/icons/hicolor/512x512/apps

	applications.files = $$P/debian/ppsspp.desktop
	applications.path = $$DESTDIR$$PREFIX/share/applications

	INSTALLS += icon16 icon32 icon64 icon96 icon256 icon512 applications
}
