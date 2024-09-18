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
	public static final int DEVICE_TYPE_VR = 3;

	public static native void init(String model, int deviceType, String languageRegion, String apkPath, String dataDir, String externalStorageDir, String extFilesDir, String nativeLibDir, String additionalStorageDirs, String cacheDir, String shortcutParam, int androidVersion, String board);
	public static native void audioInit();
	public static native void audioShutdown();
	public static native void audioConfig(int optimalFramesPerBuffer, int optimalSampleRate);

	public static native void audioRecording_SetSampleRate(int sampleRate);
	public static native void audioRecording_Start();
	public static native void audioRecording_Stop();

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

	public static native void joystickAxis(int deviceId, int []axis, float []value, int count);

	public static native boolean mouseWheelEvent(float x, float y);

	// Sensor/input data. These are asynchronous, beware!
	public static native void touch(float x, float y, int data, int pointerId);

	public static native void accelerometer(float x, float y, float z);

	public static native void mouseDelta(float x, float y);
	public static native void sendMessageFromJava(String msg, String arg);
	public static native void sendRequestResult(int seqID, boolean result, String value, int iValue);
	public static native String queryConfig(String queryName);

	public static native int getSelectedCamera();
	public static native int getDisplayFramerateMode();
	public static native void setGpsDataAndroid(long time, float hdop, float latitude, float longitude, float altitude, float speed, float bearing);
	public static native void setSatInfoAndroid(short index, short id, short elevation, short azimuth, short snr, short good);
	public static native void pushCameraImageAndroid(byte[] image);

	// Wrappers
	public static void reportException(Exception e, String data) {
		String str = e.toString() + "\n" + e.getMessage() + "\n";
		if (data != null) {
			str += data + "\n";
		}
		// could also use import android.util.Log; String stackTrace = Log.getStackTraceString(exception);
		int count = 0;
		for (StackTraceElement ste : e.getStackTrace()) {
			str += ste + "\n";
			// Only bother with the top of the stack.
			if (count > 3) {
				break;
			}
			count++;
		}
		NativeApp.sendMessageFromJava("exception", str);
	}

	public static void reportError(String errorStr) {
		NativeApp.sendMessageFromJava("exception", errorStr);
	}
}
