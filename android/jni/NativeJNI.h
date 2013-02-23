#pragma once

#include "NativeApp.h"
#include <jni.h>
#include <string>

void setupJNI(struct APP_INSTANCE* app_instance);
std::string getApkPath();
std::string getExternalDir();
std::string getUserDataPath();
std::string getLibraryPath();
std::string getInstallID();
int getDPI();
void launchBrowser(const char *url);

