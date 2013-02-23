#include "NativeJNI.h"

#define TAG "NativeJNI"

JNIEnv *env;

APP_INSTANCE* app_instance;

//TODO: JNI

void setupJNI(struct APP_INSTANCE* new_instance)
{
	app_instance = new_instance;
	app_instance->activity->vm->AttachCurrentThread(&env, NULL);
}

std::string GetJavaString(JNIEnv *env, jstring jstr)
{
	const char *str = env->GetStringUTFChars(jstr, 0);
	std::string cpp_string = std::string(str);
	env->ReleaseStringUTFChars(jstr, str);
	return cpp_string;
}

std::string getApkPath()
{
	jclass clazz = env->GetObjectClass(app_instance->activity->clazz);
    jmethodID methodID = env->GetMethodID(clazz, "getPackageCodePath", "()Ljava/lang/String;");
    jstring result = (jstring)env->CallObjectMethod(app_instance->activity->clazz, methodID);
    std::string apkPath = GetJavaString(env, result);

    return apkPath;
}

std::string getExternalDir()
{
	std::string ExternalDir = "/mnt/sdcard";
	return ExternalDir;
}

std::string getUserDataPath()
{
	std::string UserDataPath = app_instance->activity->internalDataPath;
	UserDataPath += "/";
	return UserDataPath;
}

std::string getLibraryPath()
{
	std::string LibraryPath = getUserDataPath() + "../lib";
	return LibraryPath;
}

std::string getInstallID()
{
	std::string InstallID = ""; //Not neccesary at the moment
	return InstallID;
}

int getDPI()
{
	//TODO: Stub
	return 240;
}

void launchBrowser(const char *url) {
	//TODO: Implement
}
