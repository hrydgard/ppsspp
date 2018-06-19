package org.ppsspp.ppsspp;

// Note that the display* methods are in NativeRenderer.java

public class NativeApp {
	public static final int DEVICE_ID_DEFAULT = 0;
	public static final int DEVICE_ID_KEYBOARD = 1;
	public static final int DEVICE_ID_MOUSE = 2;
	public static final int DEVICE_ID_PAD_0 = 10;

	public static final int DEVICE_TYPE_MOBILE = 0;
	public static final int DEVICE_TYPE_TV = 1;
	public static final int DEVICE_TYPE_DESKTOP = 2;

	public static native void init(String model, int deviceType, String languageRegion, String apkPath, String dataDir, String externalDir, String libraryDir, String cacheDir, String shortcutParam, int androidVersion, String board);
	public static native void audioInit();
	public static native void audioShutdown();
	public static native void audioConfig(int optimalFramesPerBuffer, int optimalSampleRate);

	public static native void computeDesiredBackbufferDimensions();
	public static native int getDesiredBackbufferWidth();
	public static native int getDesiredBackbufferHeight();

	public static native void setDisplayParameters(int display_xres, int display_yres, int dpi, float refreshRate);
	public static native void backbufferResize(int bufferWidth, int bufferHeight, int format);

	public static native boolean isLandscape();
	public static native boolean isAtTopLevel();

	// These have Android semantics: Resume is always called on bootup, after init
	public static native void pause();
	public static native void resume();

	public static native void shutdown();

	public static native boolean keyDown(int deviceId, int key, boolean isRepeat);
	public static native boolean keyUp(int deviceId, int key);

	public static native void beginJoystickEvent();
	public static native void joystickAxis(int deviceId, int axis, float value);
	public static native void endJoystickEvent();

	public static native boolean mouseWheelEvent(float x, float y);

	// Sensor/input data. These are asynchronous, beware!
	public static native boolean touch(float x, float y, int data, int pointerId);

	public static native boolean accelerometer(float x, float y, float z);

	public static native void sendMessage(String msg, String arg);

	public static native String queryConfig(String queryName);

	public static native void pushNewGpsData(float latitude, float longitude, float altitude, float speed, float bearing, long time);

	public static native void pushCameraImage(byte[] image);
}
