// This is generic code that is included in all Android apps that use the
// Native framework by Henrik Rydgård (https://github.com/hrydgard/native).

// It calls a set of methods defined in NativeApp.h. These should be implemented
// by your game or app.

#include <jni.h>

#include <android/sensor.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android_native_app_glue.h>
#include <android/log.h>
#include <stdlib.h>
#include <stdint.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <Common/GL/GLInterfaceBase.h>
#include <queue>

#include "base/basictypes.h"
#include "base/display.h"
#include "base/mutex.h"
#include "base/NativeApp.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "thread/threadutil.h"
#include "file/zip_read.h"
#include "input/input_state.h"
#include "profiler/profiler.h"
#include "math/math_util.h"
#include "net/resolve.h"
#include "android/jni/native_audio.h"
#include "gfx/gl_common.h"

#include "app-android.h"

// HACK: Use NativeQueryConfig
#include "Core/Config.h"


// Remember that all of these need initialization on init! The process
// may be reused when restarting the game. Globals are DANGEROUS.

float dp_xscale = 1.0f;
float dp_yscale = 1.0f;

InputState input_state;

static bool first_lost = true;
static std::string library_path;

struct saved_state {
	int x;
};

// This is called "engine" because it was in the sample code.
struct engine {
	struct android_app* app;

	bool initialized;

	JNIEnv* jniEnv;
	jobject jniActivity;

	struct saved_state state;

	ASensorManager* sensorManager;
	const ASensor* accelerometerSensor;
	ASensorEventQueue* sensorEventQueue;

	// TODO: Move out of here.
	cInterfaceBase* glInterface;

	// We will always be animating.
	int animating;

	int32_t width;
	int32_t height;

	jmethodID postCommand;
};

static engine android_engine;

void ProcessFrame(engine *state);
void ProcessResize(engine *state);
/**
* Initialize an EGL context for the current display.
*/
static int engine_init_display(struct engine* engine) {
	// initialize OpenGL ES and EGL
	ILOG("engine_init_display!");

	engine->glInterface = HostGL_CreateGLInterface();
	engine->glInterface->SetMode(MODE_DETECT);
	if (!engine->glInterface->Create(engine->app->window, false)) {
		ELOG("glInterface->Create failed");
		return -1;
	}
	engine->glInterface->MakeCurrent();
	
	//eglQuerySurface(display, surface, EGL_WIDTH, &w);
	//eglQuerySurface(display, surface, EGL_HEIGHT, &h);

	// ILOG("EGL surface dim: %dx%d", w, h);

	engine->width = engine->glInterface->GetBackBufferWidth();
	engine->height = engine->glInterface->GetBackBufferHeight();

	ILOG("Reported width/height: %dx%d", engine->width, engine->height);

	pixel_xres = engine->width;
	pixel_yres = engine->height;

	ProcessResize(engine);

	return 0;
}

/**
* Just the current frame in the display.
*/
static void engine_draw_frame(struct engine* engine) {
	if (!engine->glInterface) {
		// No display.
		return;
	}

	ProcessFrame(engine);

	engine->glInterface->Swap();
}

/**
* Tear down the EGL context currently associated with the display.
*/
static void engine_term_display(struct engine* engine) {
	delete engine->glInterface;
	engine->glInterface = nullptr;
	engine->animating = 0;
}

bool ProcessPointerMotion(const AInputEvent *input_event) {
	int pointerCount = AMotionEvent_getPointerCount(input_event);
	bool retval = false;
	for (int i = 0; i < pointerCount; i++) {
		int pid = AMotionEvent_getPointerId(input_event, i);
		int action = AMotionEvent_getAction(input_event);
		int actionMasked = action & AMOTION_EVENT_ACTION_MASK;
		int actionIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
		int flags = 0;
		switch (action & AMOTION_EVENT_ACTION_MASK) {
		case AMOTION_EVENT_ACTION_DOWN:
		case AMOTION_EVENT_ACTION_POINTER_DOWN:
			if (actionIndex == i) {
				flags = TOUCH_DOWN;
			}
			break;
		case AMOTION_EVENT_ACTION_UP:
		case AMOTION_EVENT_ACTION_POINTER_UP:
			if (actionIndex == i) {
				flags = TOUCH_UP;
			}
			break;
		case AMOTION_EVENT_ACTION_MOVE:
			flags = TOUCH_MOVE;
			break;
		case AMOTION_EVENT_ACTION_SCROLL:
			flags = TOUCH_WHEEL;
			break;
		}

		if (flags != 0) {
			int tool = AMotionEvent_getToolType(input_event, i);
			flags |= tool << 10;

			TouchInput touch;
			touch.id = pid;
			touch.x = AMotionEvent_getX(input_event, i) * dp_xscale;
			touch.y = AMotionEvent_getY(input_event, i) * dp_yscale;
			touch.flags = flags;
			if (touch.flags & TOUCH_DOWN) {
				input_state.pointer_down[pid] = true;
			}
			if (touch.flags & TOUCH_UP) {
				input_state.pointer_down[pid] = false;
			}

			retval = NativeTouch(touch) || retval;

			if (pid >= MAX_POINTERS) {
				ELOG("Too many pointers: %i", pid);
				return false;	// We ignore 8+ pointers entirely.
			}

			input_state.pointer_x[pid] = touch.x;
			input_state.pointer_y[pid] = touch.y;
			input_state.mouse_valid = true;
		}
	}

	return retval;
}

bool ProcessButton(AInputEvent *input_event, int deviceId) {
	KeyInput keyInput;
	keyInput.deviceId = deviceId;
	keyInput.keyCode = AKeyEvent_getKeyCode(input_event);
	switch (AKeyEvent_getAction(input_event)) {
	case AKEY_EVENT_ACTION_DOWN:
		keyInput.flags = KEY_DOWN;
		break;
	case AKEY_EVENT_ACTION_UP:
		keyInput.flags = KEY_UP;
		break;
	default:
		return false;
	}
	if ((keyInput.flags & KEY_DOWN) && AKeyEvent_getRepeatCount(input_event) > 0) {
		keyInput.flags |= KEY_IS_REPEAT;
	}
	return NativeKey(keyInput);
}

bool ProcessKey(AInputEvent *input_event) {
	if (AKeyEvent_getKeyCode(input_event) == AKEYCODE_BACK) {
		if (NativeIsAtTopLevel()) {
			ILOG("Passing back button press through to Android!");
			return false;
		} else {
			ProcessButton(input_event, DEVICE_ID_DEFAULT);
			return true;
		}
	}
	return ProcessButton(input_event, DEVICE_ID_KEYBOARD);
}

bool ProcessJoystickMotion(AInputEvent *joy_event) {
	AxisInput axis;
	axis.deviceId = DEVICE_ID_PAD_0;

	return false;
}

bool ProcessTouchpadMotion(AInputEvent *touchpad_event) {
	// Xperia Play uses this.
	ILOG("Xperia Play Touchpad Motion");
	return false;
}

// Process the next input event. We can return 1 to prevent Android from further processing the event,
// such as when catching the back button.
static int32_t engine_handle_input(struct android_app* app, AInputEvent* input_event) {
	struct engine* engine = (struct engine*)app->userData;
	int source = AInputEvent_getSource(input_event) & AINPUT_SOURCE_CLASS_MASK;
	bool handled = false;
	if (AInputEvent_getType(input_event) == AINPUT_EVENT_TYPE_MOTION) {
		if (source & AINPUT_SOURCE_CLASS_JOYSTICK) {
			handled = ProcessJoystickMotion(input_event);
		} else if (source & AINPUT_SOURCE_CLASS_POINTER) {
			handled = ProcessPointerMotion(input_event);
		} else if (source & AINPUT_SOURCE_TOUCHPAD) {
			handled = ProcessTouchpadMotion(input_event);
		}
	} else if (AInputEvent_getType(input_event) == AINPUT_EVENT_TYPE_KEY) {
		if (source & (AINPUT_SOURCE_CLASS_JOYSTICK)) {
			handled = ProcessButton(input_event, DEVICE_ID_PAD_0);
		} else {
			handled = ProcessKey(input_event);
		}
	}
	return handled ? 1 : 0;
}

// Android native glue: Process the next app command.
static void engine_handle_cmd(struct android_app* app, int32_t cmd) {
	struct engine* engine = (struct engine*)app->userData;
	switch (cmd) {
	case APP_CMD_SAVE_STATE:
		ILOG("APP_CMD_SAVE_STATE");
		// The system has asked us to save our current state. Do so.
		engine->app->savedState = malloc(sizeof(struct saved_state));
		*((struct saved_state*)engine->app->savedState) = engine->state;
		engine->app->savedStateSize = sizeof(struct saved_state);
		break;

	case APP_CMD_INIT_WINDOW:
		ILOG("APP_CMD_INIT_WINDOW");
		// The window is being shown, get it ready.
		if (engine->app->window) {
			engine_init_display(engine);
			NativeInitGraphics();
			engine_draw_frame(engine);
		} else {
			ELOG("APP_CMD_INIT_WINDOW but window == null")
		}
		break;

	case APP_CMD_TERM_WINDOW:
		ILOG("APP_CMD_TERM_WINDOW");
		// The window is being hidden or closed, clean it up.
		NativeShutdownGraphics();
		engine_term_display(engine);
		break;

	case APP_CMD_GAINED_FOCUS:
		ILOG("APP_CMD_GAINED_FOCUS");
		// When our app gains focus, we start monitoring the accelerometer.
		if (engine->accelerometerSensor) {
			ASensorEventQueue_enableSensor(engine->sensorEventQueue, engine->accelerometerSensor);
			// We'd like to get 60 events per second (in us).
			ASensorEventQueue_setEventRate(engine->sensorEventQueue, engine->accelerometerSensor, (1000L / 60) * 1000);
		}
		// We also start animating again.
		engine->animating = 1;
		break;

	case APP_CMD_LOST_FOCUS:
		ILOG("APP_CMD_LOST_FOCUS");
		// When our app loses focus, we stop monitoring the accelerometer.
		// This is to avoid consuming battery while not being used.
		if (engine->accelerometerSensor) {
			ASensorEventQueue_disableSensor(engine->sensorEventQueue, engine->accelerometerSensor);
		}
		// Also stop animating.
		engine->animating = 0;
		engine_draw_frame(engine);
		break;
	
	case APP_CMD_INPUT_CHANGED:
		ILOG("APP_CMD_INPUT_CHANGED");
		break;

	case APP_CMD_WINDOW_RESIZED:
		// TODO: Reinitialize EGL?
		ILOG("APP_CMD_WINDOW_RESIZED");
		break;

	case APP_CMD_WINDOW_REDRAW_NEEDED:
		ILOG("APP_CMD_WINDOW_REDRAW_NEEDED");
		break;

	case APP_CMD_CONTENT_RECT_CHANGED:
		ILOG("APP_CMD_CONTENT_RECT_CHANGED");
		break;

	case APP_CMD_CONFIG_CHANGED:
		ILOG("APP_CMD_CONFIG_CHANGED");
		break;

	case APP_CMD_LOW_MEMORY:
		ILOG("APP_CMD_LOW_MEMORY");
		break;

	case APP_CMD_START:
		ILOG("APP_CMD_START");
		break;

	case APP_CMD_RESUME:
		ILOG("APP_CMD_RESUME");
		AndroidAudio_Resume();
		break;

	case APP_CMD_PAUSE:
		ILOG("APP_CMD_PAUSE");
		AndroidAudio_Pause();
		break;

	case APP_CMD_STOP:
		ILOG("APP_CMD_STOP");
		break;

	case APP_CMD_DESTROY:
		ILOG("APP_CMD_DESTROY");
		break;

	default:
		ILOG("app cmd unhandled: %d", cmd);
		break;
	}
}

struct FrameCommand {
	FrameCommand() {}
	FrameCommand(std::string cmd, std::string prm) : command(cmd), params(prm) {}

	std::string command;
	std::string params;
};

static recursive_mutex frameCommandLock;
static std::queue<FrameCommand> frameCommands;

std::string systemName;
std::string langRegion;
std::string mogaVersion;

static float left_joystick_x_async;
static float left_joystick_y_async;
static float right_joystick_x_async;
static float right_joystick_y_async;
static float hat_joystick_x_async;
static float hat_joystick_y_async;

static int optimalFramesPerBuffer = 0;
static int optimalSampleRate = 0;
static int sampleRate = 0;
static int framesPerBuffer = 0;
static int androidVersion;
static int deviceType;

// Should only be used for display detection during startup (for config defaults etc)
static int display_xres;
static int display_yres;

// Android implementation of callbacks to the Java part of the app
void SystemToast(const char *text) {
	lock_guard guard(frameCommandLock);
	frameCommands.push(FrameCommand("toast", text));
}

void ShowKeyboard() {
	lock_guard guard(frameCommandLock);
	frameCommands.push(FrameCommand("showKeyboard", ""));
}

void Vibrate(int length_ms) {
	lock_guard guard(frameCommandLock);
	char temp[32];
	sprintf(temp, "%i", length_ms);
	frameCommands.push(FrameCommand("vibrate", temp));
}

void LaunchBrowser(const char *url) {
	lock_guard guard(frameCommandLock);
	frameCommands.push(FrameCommand("launchBrowser", url));
}

void LaunchMarket(const char *url) {
	lock_guard guard(frameCommandLock);
	frameCommands.push(FrameCommand("launchMarket", url));
}

void LaunchEmail(const char *email_address) {
	lock_guard guard(frameCommandLock);
	frameCommands.push(FrameCommand("launchEmail", email_address));
}

void System_SendMessage(const char *command, const char *parameter) {
	lock_guard guard(frameCommandLock);

	// We are moving a lot of this type of functionality out of the Java code into C++.
	if (!strcmp(command, "showKeyboard")) {
		// ANativeActivity_showSoftInput()
	} else if (!strcmp(command, "hideKeyboard")) {

	} else {
		frameCommands.push(FrameCommand(command, parameter));
	}
}

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME:
		return systemName;
	case SYSPROP_LANGREGION:  // "en_US"
		return langRegion;
	case SYSPROP_MOGA_VERSION:
		return mogaVersion;
	default:
		return "";
	}
}

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_SYSTEMVERSION:
		return androidVersion;
	case SYSPROP_DEVICE_TYPE:
		return deviceType;
	case SYSPROP_DISPLAY_XRES:
		return display_xres;
	case SYSPROP_DISPLAY_YRES:
		return display_yres;
	case SYSPROP_AUDIO_SAMPLE_RATE:
		return sampleRate;
	case SYSPROP_AUDIO_FRAMES_PER_BUFFER:
		return framesPerBuffer;
	case SYSPROP_AUDIO_OPTIMAL_SAMPLE_RATE:
		return optimalSampleRate;
	case SYSPROP_AUDIO_OPTIMAL_FRAMES_PER_BUFFER:
		return optimalFramesPerBuffer;
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return (int)(display_hz * 1000.0);
	default:
		return -1;
	}
}

std::string GetJavaString(JNIEnv *env, jstring jstr) {
	const char *str = env->GetStringUTFChars(jstr, 0);
	std::string cpp_string = std::string(str);
	env->ReleaseStringUTFChars(jstr, str);
	return cpp_string;
}

// This is now only used as a trigger for GetAppInfo as a function to all before Init.
// On Android we don't use any of the values it returns.
extern "C" jboolean Java_org_ppsspp_ppsspp_NativeApp_isLandscape(JNIEnv *env, jclass) {
	std::string app_name, app_nice_name, version;
	bool landscape;
	NativeGetAppInfo(&app_name, &app_nice_name, &landscape, &version);
	return landscape;
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_audioConfig
	(JNIEnv *env, jclass, jint optimalFPB, jint optimalSR) {
	optimalFramesPerBuffer = optimalFPB;
	optimalSampleRate = optimalSR;
}

extern "C" jstring Java_org_ppsspp_ppsspp_NativeApp_queryConfig
	(JNIEnv *env, jclass, jstring jquery) {
	std::string query = GetJavaString(env, jquery);
	std::string result = NativeQueryConfig(query);
	jstring jresult = env->NewStringUTF(result.c_str());
	return jresult;
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_init
  (JNIEnv *env, jclass, jstring jmodel, jint jdeviceType, jint jxres, jint jyres, jstring jlangRegion, jstring japkpath,
		jstring jdataDir, jstring jexternalDir, jstring jlibraryDir, jstring jshortcutParam,
		jstring jinstallID, jint jAndroidVersion) {

	ILOG("NativeApp.init() -- begin");
	PROFILE_INIT();

	memset(&input_state, 0, sizeof(input_state));
	first_lost = true;
	androidVersion = jAndroidVersion;
	deviceType = jdeviceType;

	g_buttonTracker.Reset();

	left_joystick_x_async = 0;
	left_joystick_y_async = 0;
	right_joystick_x_async = 0;
	right_joystick_y_async = 0;
	hat_joystick_x_async = 0;
	hat_joystick_y_async = 0;
	display_xres = jxres;
	display_yres = jyres;

	std::string apkPath = GetJavaString(env, japkpath);
	VFSRegister("", new ZipAssetReader(apkPath.c_str(), "assets/"));

	systemName = GetJavaString(env, jmodel);
	langRegion = GetJavaString(env, jlangRegion);

	std::string externalDir = GetJavaString(env, jexternalDir);
	std::string user_data_path = GetJavaString(env, jdataDir) + "/";
	library_path = GetJavaString(env, jlibraryDir) + "/";
	std::string shortcut_param = GetJavaString(env, jshortcutParam);
	std::string installID = GetJavaString(env, jinstallID);

	ILOG("NativeApp.init(): External storage path: %s", externalDir.c_str());
	ILOG("NativeApp.init(): Launch shortcut parameter: %s", shortcut_param.c_str());

	std::string app_name;
	std::string app_nice_name;
	std::string version;
	bool landscape;

	ILOG("NativeApp.init() - performing initialization");
	net::Init();

	NativeGetAppInfo(&app_name, &app_nice_name, &landscape, &version);

	// If shortcut_param is not empty, pass it as additional varargs argument to NativeInit() method.
	// NativeInit() is expected to treat extra argument as boot_filename, which in turn will start game immediately.
	// NOTE: Will only work if ppsspp started from Activity.onCreate(). Won't work if ppsspp app start from onResume().

	if (shortcut_param.empty()) {
		const char *argv[2] = { app_name.c_str(), 0 };
		NativeInit(1, argv, user_data_path.c_str(), externalDir.c_str(), installID.c_str());
	} else {
		const char *argv[3] = { app_name.c_str(), shortcut_param.c_str(), 0 };
		NativeInit(2, argv, user_data_path.c_str(), externalDir.c_str(), installID.c_str());
	}
	android_engine.initialized = true;
	ILOG("NativeApp.init() -- end");
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_displayConfig(JNIEnv *, jclass, float dpi, float refreshRate) {
	ILOG("NativeApp.displayConfig(dpi=%0.2f, refresh=%0.2f)", dpi, refreshRate);
	g_dpi = dpi;
	g_dpi_scale = 240.0f / (float)g_dpi;
	display_hz = refreshRate;
}

void ProcessResize(engine *state) {
	ILOG("NativeApp.displayResize(%d x %d)", state->width, state->height);

	dp_xres = pixel_xres * g_dpi_scale;
	dp_yres = pixel_yres * g_dpi_scale;
	dp_xscale = (float)dp_xres / pixel_xres;
	dp_yscale = (float)dp_yres / pixel_yres;

	NativeResized();
}

void ProcessFrame(engine *state) {
	static bool hasSetThreadName = false;
	if (!hasSetThreadName) {
		hasSetThreadName = true;
		setCurrentThreadName("AndroidRender");
	}

	if (state->glInterface) {
		// TODO: Look into if these locks are a perf loss
		{
			lock_guard guard(input_state.lock);

			input_state.pad_lstick_x = left_joystick_x_async;
			input_state.pad_lstick_y = left_joystick_y_async;
			input_state.pad_rstick_x = right_joystick_x_async;
			input_state.pad_rstick_y = right_joystick_y_async;

			UpdateInputState(&input_state);
		}
		NativeUpdate(input_state);

		{
			lock_guard guard(input_state.lock);
			EndInputState(&input_state);
		}

		NativeRender();
		time_update();
	} else {
		ELOG("BAD: Ended up in nativeRender even though app has quit.%s", "");
		// Shouldn't really get here. Let's draw magenta if we have GL access.
		glDepthMask(GL_TRUE);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(1.0, 0.0, 1.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}

	lock_guard guard(frameCommandLock);
	while (!frameCommands.empty()) {
		FrameCommand frameCmd;
		frameCmd = frameCommands.front();
		frameCommands.pop();

		DLOG("frameCommand %s %s", frameCmd.command.c_str(), frameCmd.params.c_str());

		jstring cmd = state->jniEnv->NewStringUTF(frameCmd.command.c_str());
		jstring param = state->jniEnv->NewStringUTF(frameCmd.params.c_str());
		state->jniEnv->CallVoidMethod(state->jniActivity, state->postCommand, cmd, param);
		state->jniEnv->DeleteLocalRef(cmd);
		state->jniEnv->DeleteLocalRef(param);
	}
}

extern "C" void Java_org_ppsspp_ppsspp_NativeApp_sendMessage(JNIEnv *env, jclass, jstring message, jstring param) {
	std::string msg = GetJavaString(env, message);
	std::string prm = GetJavaString(env, param);

	if (msg == "moga") {
		mogaVersion = prm;
	}
	NativeMessageReceived(msg.c_str(), prm.c_str());
}

void ProcessAccelerometer(const ASensorEvent &sensor_event) {
	// Theoretically this needs locking but I doubt it matters. Worst case, the X
	// from one "sensor frame" will be used together with Y from the next.
	// Should look into quantization though, for compressed movement storage.
	input_state.accelerometer_valid = true;
	input_state.acc.x = sensor_event.acceleration.x;
	input_state.acc.y = sensor_event.acceleration.y;
	input_state.acc.z = sensor_event.acceleration.z;

	AxisInput axis;
	axis.deviceId = DEVICE_ID_ACCELEROMETER;
	axis.flags = 0;

	axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_X;
	axis.value = sensor_event.acceleration.x;
	NativeAxis(axis);

	axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_Y;
	axis.value = sensor_event.acceleration.y;
	NativeAxis(axis);

	axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_Z;
	axis.value = sensor_event.acceleration.z;
	NativeAxis(axis);
}

void correctRatio(int &sz_x, int &sz_y, float scale) {
	float x = sz_x;
	float y = sz_y;
	float ratio = x / y;
	// Log.i(TAG, "Considering size: " + sz.x + "x" + sz.y + "=" + ratio);
	float targetRatio;
	if (x >= y) {
		targetRatio = 480.0f / 272.0f;
		x = 480.f * scale;
		y = 272.f * scale;
	} else {
		targetRatio = 272.0f / 480.0f;
		x = 272.0f * scale;
		y = 480.0f * scale;
	}

	float correction = targetRatio / ratio;
	// Log.i(TAG, "Target ratio: " + targetRatio + " ratio: " + ratio + " correction: " + correction);
	if (ratio < targetRatio) {
		y *= correction;
	} else {
		x /= correction;
	}

	sz_x = (int)x;
	sz_y = (int)y;
	// Log.i(TAG, "Corrected ratio: " + sz.x + "x" + sz.y);
}

// TODO: Use this!
void getDesiredBackbufferSize(int &sz_x, int &sz_y) {
	sz_x = pixel_xres;
	sz_y = pixel_yres;

	int scale = g_Config.iAndroidHwScale;
	if (scale == 0) {
		sz_x = 0;
		sz_y = 0;
		return;
	} else {
		correctRatio(sz_x, sz_y, (float)scale);
	}
}

/**
* This is the main entry point of a native application that is using
* android_native_app_glue.  It runs in its own thread, with its own
* event loop for receiving input events and doing other things.
*
* This will be exited on things like orientation changes.
* Note that the rest of the app can then often live on in the background.
* When exiting this, only graphics and audio state is torn down.
*/

/*  We should probably never call these, possibly unless the user clicks Exit.
NativeShutdown();
VFSShutdown();
net::Shutdown();
*/

void android_main(struct android_app* state) {
	ILOG("Entering android_main");

	// Make sure glue isn't stripped.
	app_dummy();

	JNIEnv *jni;
	state->activity->vm->AttachCurrentThread(&jni, NULL);
	ILOG("JNI: %p", jni);

	engine &engine = android_engine;

	engine.jniEnv = jni;
	engine.jniActivity = state->activity->clazz;

	ILOG("Old state userdata: %p  engine: %p", state->userData, &android_engine);
	state->userData = &engine;
	state->onAppCmd = engine_handle_cmd;
	state->onInputEvent = engine_handle_input;
	engine.app = state;

	// Prepare to monitor accelerometer
	engine.sensorManager = ASensorManager_getInstance();
	engine.accelerometerSensor = ASensorManager_getDefaultSensor(engine.sensorManager, ASENSOR_TYPE_ACCELEROMETER);
	engine.sensorEventQueue = ASensorManager_createEventQueue(engine.sensorManager, state->looper, LOOPER_ID_USER, NULL, NULL);

	ANativeActivity *activity = state->activity;

	ILOG("Fetching method ID to postCommand...");
	engine.postCommand = engine.jniEnv->GetMethodID(engine.jniEnv->GetObjectClass(engine.jniActivity), "postCommand", "(Ljava/lang/String;Ljava/lang/String;)V");
	ILOG("Fetched method ID %p to postCommand...", engine.postCommand);

	if (state->savedState != NULL) {
		// We are starting with a previous saved state; restore from it.
		engine.state = *(struct saved_state*)state->savedState;
	}

	sampleRate = optimalSampleRate;
	if (NativeQueryConfig("force44khz") != "0" || optimalSampleRate == 0) {
		sampleRate = 44100;
	}
	if (optimalFramesPerBuffer > 0) {
		framesPerBuffer = optimalFramesPerBuffer;
	} else {
		framesPerBuffer = 512;
	}

	// Some devices have totally bonkers buffer sizes like 8192. They will have terrible latency anyway, so to avoid having to
	// create extra smart buffering code, we'll just let their regular mixer deal with it, missing the fast path (as if they had one...)
	if (framesPerBuffer > 512) {
		framesPerBuffer = 512;
		sampleRate = 44100;
	}

	ILOG("NativeApp.audioInit() -- Using OpenSL audio! frames/buffer: %i   optimal sr: %i   actual sr: %i", optimalFramesPerBuffer, optimalSampleRate, sampleRate);
	AndroidAudio_Init(&NativeMix, library_path, framesPerBuffer, sampleRate);

	// Setup hardware scaling using getDesiredBackbufferSize
	// ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
	// ANativeWindow_setBuffersGeometry(window, bufferWidth, bufferHeight, RGB_565);

	// loop waiting for stuff to do.
	while (1) {
		// Read all pending events.
		int ident;
		int events;
		struct android_poll_source* source;

		// If not animating, we will block forever waiting for events.
		// If animating, we loop until all events are read, then continue
		// to draw the next frame of animation.
		while ((ident = ALooper_pollAll(engine.animating ? 0 : -1, NULL, &events, (void**)&source)) >= 0) {

			// Process this event.
			if (source != NULL) {
				source->process(state, source);
			}

			// If a sensor has data, process it immediately.
			if (ident == LOOPER_ID_USER) {
				if (engine.accelerometerSensor) {
					ASensorEvent sensor_event;
					while (ASensorEventQueue_getEvents(engine.sensorEventQueue, &sensor_event, 1) > 0) {
						ProcessAccelerometer(sensor_event);
					}
				}
			}

			// Check if we are exiting.
			if (state->destroyRequested != 0) {
				ILOG("Destroy requested! Leaving.");
				engine_term_display(&engine);
				goto quit;
			}
		}

		if (engine.animating) {
			// Drawing is throttled to the screen update rate, so there
			// is no need to do timing here.
			engine_draw_frame(&engine);
		}
	}

quit:
	ASensorManager_destroyEventQueue(engine.sensorManager, engine.sensorEventQueue);
	engine.sensorManager = nullptr;
	engine.sensorEventQueue = nullptr;
	engine.postCommand = nullptr;

	AndroidAudio_Shutdown();

	state->activity->vm->DetachCurrentThread();

	ILOG("Leaving android_main");
}

