package com.henrikrydgard.libnative;

public class NativeApp {
	public static native void init(int xxres, int yyres, int dpi, String apkPath, String dataDir, String externalDir, String libraryDir, String installID, boolean useOpenSL);
	public static native void audioConfig(int optimalFramesPerBuffer, int optimalSampleRate);
	
	public static native boolean isLandscape();

	public static native boolean isAtTopLevel();

	// These have Android semantics: Resume is always called on bootup, after init
	public static native void pause();

	public static native void resume();

	public static native void shutdown();

	public static native void keyDown(int key);

	public static native void keyUp(int key);

	public static native void joystickEvent(int stick, float x, float y);
	public static native void mouseWheelEvent(float x, float y);

	// will only be called between init() and shutdown()
	public static native int audioRender(short[] buffer);

	// Sensor/input data. These are asynchronous, beware!
	public static native void touch(float x, float y, int data, int pointerId);

	public static native void accelerometer(float x, float y, float z);

	public static native void sendMessage(String msg, String arg);
}
