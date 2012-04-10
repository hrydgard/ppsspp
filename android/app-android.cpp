// This is generic code that is included in all Android apps that use the
// Native framework by Henrik Rydgård (https://github.com/hrydgard/native).

// It calls a set of methods defined in NativeApp.h. These should be implemented
// by your game or app.


#include <jni.h>
#include <android/log.h>
#include <stdlib.h>
#include <stdint.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "base/basictypes.h"
#include "base/display.h"
#include "base/NativeApp.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "file/zip_read.h"
#include "input/input_state.h"
#include "audio/mixer.h"

#define coord_xres 800
#define coord_yres 480

static JNIEnv *jniEnvUI;

std::string frameCommand;
std::string frameCommandParam;

// Android implementation of callbacks to the Java part of the app
void SystemToast(const char *text) {
  frameCommand = "toast";
  frameCommandParam = text;
}

// TODO: need a Hide or bool show;
void ShowAd(int x, int y, bool center_x) {
	ELOG("TODO! ShowAd!");
}

void Vibrate(int length_ms) {
  frameCommand = "vibrate";
  frameCommandParam = "100";
}

void LaunchBrowser(const char *url)
{
  frameCommand = "launchBrowser";
  frameCommandParam = url;
}

void LaunchMarket(const char *url)
{
  frameCommand = "launchMarket";
  frameCommandParam = url;
}

void LaunchEmail(const char *email_address)
{
  frameCommand = "launchEmail";
  frameCommandParam = email_address;
}


// Remember that all of these need initialization on init! The process
// may be reused when restarting the game. Globals are DANGEROUS.
int xres, yres;

// Used for touch. (TODO)
float xscale = 1;
float yscale = 1;

InputState input_state;

static bool renderer_inited = false;
static bool first_lost = true;

extern "C" jboolean Java_com_turboviking_libnative_NativeApp_isLandscape(JNIEnv *env, jclass) {
	std::string app_name, app_nice_name;
	bool landscape;
	NativeGetAppInfo(&app_name, &app_nice_name, &landscape);
	return landscape;
}

extern "C" void Java_com_turboviking_libnative_NativeApp_init
  (JNIEnv *env, jclass, jint xxres, jint yyres, jstring apkpath,
   jstring dataDir, jstring externalDir, jstring jinstallID) {
  jniEnvUI = env;
  xres = xxres;
  yres = yyres;
  g_xres = xres;
  g_yres = yres;
  xscale = (float)coord_xres / xres;
  yscale = (float)coord_yres / yres;
  memset(&input_state, 0, sizeof(input_state));
  renderer_inited = false;
  first_lost = true;

  jboolean isCopy;
  const char *str = env->GetStringUTFChars(apkpath, &isCopy);
  ILOG("APK path: %s", str);
  VFSRegister("", new ZipAssetReader(str, "assets/"));

  str = env->GetStringUTFChars(externalDir, &isCopy);
  ILOG("External storage path: %s", str);

  str = env->GetStringUTFChars(dataDir, &isCopy);
  std::string user_data_path = std::string(str) + "/";

  str = env->GetStringUTFChars(jinstallID, &isCopy);
  std::string installID = std::string(str);

	std::string app_name, app_nice_name;
	bool landscape;

	NativeGetAppInfo(&app_name, &app_nice_name, &landscape);
	const char *argv[2] = {app_name.c_str(), 0};
  NativeInit(1, argv, user_data_path.c_str(), installID.c_str());
}  

extern "C" void Java_com_turboviking_libnative_NativeApp_shutdown
  (JNIEnv *, jclass) {
  ILOG("RollerBallMainShutdown - calling NativeShutdown.");
  if (renderer_inited) {
    NativeShutdownGraphics();
  }
  NativeShutdown();
  ILOG("RollerBallMainShutdown - calling VFSShutdown.");
  VFSShutdown();
}

static jmethodID postCommand;

extern "C" void Java_com_turboviking_libnative_NativeRenderer_displayInit(JNIEnv * env, jobject obj) {
  if (!renderer_inited) {
    ILOG("Calling NativeInitGraphics();");
    NativeInitGraphics();
  } else {
    ILOG("Calling NativeDeviceLost();");
  }
  renderer_inited = true;
  jclass cls = env->GetObjectClass(obj);
  postCommand = env->GetMethodID(
      cls, "postCommand", "(Ljava/lang/String;Ljava/lang/String;)V");
  ILOG("MethodID: %i", (int)postCommand);
}

extern "C" void Java_com_turboviking_libnative_NativeRenderer_displayResize
  (JNIEnv *, jobject clazz, jint w, jint h) {
  ILOG("nativeResize (%i, %i), device lost!", w, h);
  if (first_lost) {
    first_lost = false;
  } else {
    NativeDeviceLost();
  }
}

extern "C" void Java_com_turboviking_libnative_NativeRenderer_displayRender
  (JNIEnv *env, jobject obj) {
  if (renderer_inited) {
    UpdateInputState(&input_state);
    NativeUpdate(input_state);
    NativeRender();
    time_update();
  } else {
    ELOG("Ended up in nativeRender even though app has quit.%s", "");
    // Shouldn't really get here.
    glClearColor(1.0, 0.0, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
  
  if (!frameCommand.empty()) {
    ILOG("frameCommand %s %s", frameCommand.c_str(), frameCommandParam.c_str());
    jstring cmd = env->NewStringUTF(frameCommand.c_str());
    jstring param = env->NewStringUTF(frameCommandParam.c_str());
    
    env->CallVoidMethod(obj, postCommand, cmd, param);
    
  	frameCommand = "";
  	frameCommandParam = "";
  }
}

extern "C" void Java_com_turboviking_libnative_NativeApp_audioRender(JNIEnv*  env, jclass clazz, jshortArray array) {
  int buf_size = env->GetArrayLength(array);
	if (buf_size) {
    short *data = env->GetShortArrayElements(array, 0);
	  int samples = buf_size / 2;
    NativeMix(data, samples);
	  env->ReleaseShortArrayElements(array, data, 0);
  }
}

extern "C" void JNICALL Java_com_turboviking_libnative_NativeApp_touch
  (JNIEnv *, jclass, int x, int y, int code) {
  // This really does require locking :/
  input_state.mouse_valid = false;
  input_state.mouse_x = x;
  input_state.mouse_y = y;
  if (code == 1) {
  	input_state.mouse_buttons = 1;
  } else if (code == 2) {
  	input_state.mouse_buttons = 0;
  }
  input_state.mouse_valid = true;
}

extern "C" void Java_com_turboviking_libnative_NativeApp_keyDown
  (JNIEnv *, jclass, jint key) {
  ILOG("Keydown %i", key);
  // Need a mechanism to release these.
  switch (key) {
  	case 1:  // Back
  	  input_state.pad_buttons |= PAD_BUTTON_BACK;
  	  break;
  	case 2:  // Menu
  	  input_state.pad_buttons |= PAD_BUTTON_START;
  	  break;
  	case 3:  // Search
  	  input_state.pad_buttons |= PAD_BUTTON_A;
  	  break;
  }
}

extern "C" void Java_com_turboviking_libnative_NativeApp_keyUp
  (JNIEnv *, jclass, jint key) {
  ILOG("Keyup %i", key);
  // Need a mechanism to release these.
  switch (key) {
  	case 1:  // Back
  	  input_state.pad_buttons &= ~PAD_BUTTON_BACK;
  	  break;
  	case 2:  // Menu
  	  input_state.pad_buttons &= ~PAD_BUTTON_START;
  	  break;
  	case 3:  // Search
  	  input_state.pad_buttons &= ~PAD_BUTTON_A;
  	  break;
  }
}

extern "C" void JNICALL Java_com_turboviking_libnative_NativeApp_accelerometer
  (JNIEnv *, jclass, float x, float y, float z) {
  // Theoretically this needs locking but I doubt it matters. Worst case, the X
  // from one "sensor frame" will be used together with Y from the next.
  // Should look into quantization though, for compressed movement storage.
  input_state.accelerometer_valid = true;
  input_state.acc.x = x;
  input_state.acc.y = y;
  input_state.acc.z = z;
}
