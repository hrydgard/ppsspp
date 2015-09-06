DEFINES += ANDROID
INCLUDEPATH += $$P/ext/native/ext/libzip

!contains(CONFIG, staticlib) {
	# Packaging
	ANDROID_PACKAGE_SOURCE_DIR = $$P/android
}
