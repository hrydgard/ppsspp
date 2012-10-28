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
#include "math/math_util.h"
#include "net/resolve.h"
#include "android/native_audio.h"

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

void ShowKeyboard() {
	frameCommand = "showkeyboard";
	frameCommandParam = "";
}

void Vibrate(int length_ms) {
  frameCommand = "vibrate";
  frameCommandParam = "100";
}

void LaunchBrowser(const char *url) {
  frameCommand = "launchBrowser";
  frameCommandParam = url;
}

void LaunchMarket(const char *url) {
  frameCommand = "launchMarket";
  frameCommandParam = url;
}

void LaunchEmail(const char *email_address) {
  frameCommand = "launchEmail";
  frameCommandParam = email_address;
}

// Remember that all of these need initialization on init! The process
// may be reused when restarting the game. Globals are DANGEROUS.

float dp_xscale = 1;
float dp_yscale = 1;

InputState input_state;

static bool renderer_inited = false;
static bool first_lost = true;
static bool use_native_audio = false;

std::string GetJavaString(JNIEnv *env, jstring jstr)
{
  const char *str = env->GetStringUTFChars(jstr, 0);
  std::string cpp_string = std::string(str);
  env->ReleaseStringUTFChars(jstr, str);
  return cpp_string;
}

extern "C" jboolean Java_com_turboviking_libnative_NativeApp_isLandscape(JNIEnv *env, jclass)
{
	std::string app_name, app_nice_name;
	bool landscape;
	NativeGetAppInfo(&app_name, &app_nice_name, &landscape);
	return landscape;
}

// For the Back button to work right.
extern "C" jboolean Java_com_turboviking_libnative_NativeApp_isAtTopLevel(JNIEnv *env, jclass) {
  return NativeIsAtTopLevel();
}

extern "C" void Java_com_turboviking_libnative_NativeApp_init
  (JNIEnv *env, jclass, jint xxres, jint yyres, jint dpi, jstring japkpath,
   jstring jdataDir, jstring jexternalDir, jstring jlibraryDir, jstring jinstallID, jboolean juseNativeAudio) {
  jniEnvUI = env;

  memset(&input_state, 0, sizeof(input_state));
  renderer_inited = false;
  first_lost = true;

  std::string apkPath = GetJavaString(env, japkpath);
  ILOG("APK path: %s", apkPath.c_str());
  VFSRegister("", new ZipAssetReader(apkPath.c_str(), "assets/"));

  std::string externalDir = GetJavaString(env, jexternalDir);
  std::string user_data_path = GetJavaString(env, jdataDir) + "/";
  std::string library_path = GetJavaString(env, jlibraryDir) + "/";
  std::string installID = GetJavaString(env, jinstallID);

  ILOG("External storage path: %s", externalDir.c_str());

	std::string app_name;
	std::string app_nice_name;
	bool landscape;

  net::Init();

  g_dpi = dpi;
	g_dpi_scale = 240.0f / (float)g_dpi;
  pixel_xres = xxres;
  pixel_yres = yyres;

	NativeGetAppInfo(&app_name, &app_nice_name, &landscape);

	const char *argv[2] = {app_name.c_str(), 0};
  NativeInit(1, argv, user_data_path.c_str(), externalDir.c_str(), installID.c_str());

  use_native_audio = juseNativeAudio;
	if (use_native_audio) {
		AndroidAudio_Init(&NativeMix, library_path);
	}
}  

extern "C" void Java_com_turboviking_libnative_NativeApp_resume(JNIEnv *, jclass) {
	ILOG("NativeResume");
	if (use_native_audio) {
		AndroidAudio_Resume();
	}
}

extern "C" void Java_com_turboviking_libnative_NativeApp_pause(JNIEnv *, jclass) {
	ILOG("NativePause");
	if (use_native_audio) {
		AndroidAudio_Pause();
	}
}
 
extern "C" void Java_com_turboviking_libnative_NativeApp_shutdown(JNIEnv *, jclass) {
  ILOG("NativeShutdown.");
 	if (use_native_audio) {
		AndroidAudio_Shutdown();
	}
	if (renderer_inited) {
    NativeShutdownGraphics();
		renderer_inited = false;
  }
  NativeShutdown();
  ILOG("VFSShutdown.");
  VFSShutdown();
  net::Shutdown();
}

static jmethodID postCommand;

extern "C" void Java_com_turboviking_libnative_NativeRenderer_displayInit(JNIEnv * env, jobject obj) {
	ILOG("displayInit()");
  if (!renderer_inited) {

    // We default to 240 dpi and all UI code is written to assume it. (DENSITY_HIGH, like Nexus S).
    // Note that we don't compute dp_xscale and dp_yscale until later! This is so that NativeGetAppInfo
    // can change the dp resolution if it feels like it.
    dp_xres = pixel_xres * g_dpi_scale;
    dp_yres = pixel_yres * g_dpi_scale;

		ILOG("Calling NativeInitGraphics();  dpi = %i, dp_xres = %i, dp_yres = %i", g_dpi, dp_xres, dp_yres);
    NativeInitGraphics();

    dp_xscale = (float)dp_xres / pixel_xres;
    dp_yscale = (float)dp_yres / pixel_yres;
		renderer_inited = true;
  } else {
    ILOG("Calling NativeDeviceLost();");
		NativeDeviceLost();
  }
  jclass cls = env->GetObjectClass(obj);
  postCommand = env->GetMethodID(cls, "postCommand", "(Ljava/lang/String;Ljava/lang/String;)V");
  ILOG("MethodID: %i", (int)postCommand);
}

extern "C" void Java_com_turboviking_libnative_NativeRenderer_displayResize(JNIEnv *, jobject clazz, jint w, jint h) {
  ILOG("displayResize (%i, %i)!", w, h);
}

extern "C" void Java_com_turboviking_libnative_NativeRenderer_displayRender(JNIEnv *env, jobject obj) {
  if (renderer_inited) {
    UpdateInputState(&input_state);
    NativeUpdate(input_state);
    NativeRender();
    EndInputState(&input_state);
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
  // The audio thread can pretty safely enable Flush-to-Zero mode on the FPU.
  EnableFZ();

  int buf_size = env->GetArrayLength(array);
	if (buf_size) {
    short *data = env->GetShortArrayElements(array, 0);
	  int samples = buf_size / 2;
    NativeMix(data, samples);
	  env->ReleaseShortArrayElements(array, data, 0);
  }
}

extern "C" void JNICALL Java_com_turboviking_libnative_NativeApp_touch
  (JNIEnv *, jclass, float x, float y, int code, int pointerId) {
  lock_guard guard(input_state.lock);

	if (pointerId >= MAX_POINTERS) {
		ELOG("Too many pointers: %i", pointerId);
		return;  // We ignore 8+ pointers entirely.
	}
  float scaledX = (int)(x * dp_xscale);  // why the (int) cast?
  float scaledY = (int)(y * dp_yscale);
  input_state.pointer_x[pointerId] = scaledX;
  input_state.pointer_y[pointerId] = scaledY;
  if (code == 1) {
  	input_state.pointer_down[pointerId] = true;
    NativeTouch(pointerId, scaledX, scaledY, 0, TOUCH_DOWN);
  } else if (code == 2) {
  	input_state.pointer_down[pointerId] = false;
    NativeTouch(pointerId, scaledX, scaledY, 0, TOUCH_UP);
  } else {
    NativeTouch(pointerId, scaledX, scaledY, 0, TOUCH_MOVE);
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
  	  input_state.pad_buttons |= PAD_BUTTON_MENU;
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
  	  input_state.pad_buttons &= ~PAD_BUTTON_MENU;
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

extern "C" void Java_com_turboviking_libnative_NativeApp_sendMessage
  (JNIEnv *env, jclass, jstring message, jstring param) {
	jboolean isCopy;
	std::string msg = GetJavaString(env, message);
	std::string prm = GetJavaString(env, param);
	ILOG("Message received: %s %s", msg.c_str(), prm.c_str());
}


