DEFINES += ANDROID
INCLUDEPATH += $$P/native/ext/libzip

!contains(CONFIG, staticlib) {
	# Packaging
	ANDROID_PACKAGE_SOURCE_DIR = $$P/android
}
