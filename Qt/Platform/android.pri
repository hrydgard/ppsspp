DEFINES += ANDROID
INCLUDEPATH += $$P/native/ext/libzip

equals(TARGET, PPSSPPQt) {
	# Packaging
	ANDROID_PACKAGE_SOURCE_DIR = $$P/android
}
