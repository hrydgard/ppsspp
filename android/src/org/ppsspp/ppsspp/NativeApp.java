package org.ppsspp.ppsspp;

// Note that the display* methods are in NativeRenderer.java

import android.annotation.SuppressLint;
import android.os.Build;
import android.util.Log;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.MotionEvent;

import androidx.annotation.RequiresApi;

public class NativeApp {
	private static final String TAG = "PPSSPPNativeActivity";

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

	public static native void mouse(float x, float y, int button, int action);
	public static native void mouseDelta(float x, float y);
	public static native void sendMessageFromJava(String msg, String arg);
	public static native void sendRequestResult(int seqID, boolean result, String value, int iValue);
	public static native String queryConfig(String queryName);

	public static native int getSelectedCamera();
	public static native int getDisplayFramerateMode();
	public static native void setGpsDataAndroid(long time, float hdop, float latitude, float longitude, float altitude, float speed, float bearing);
	public static native void setSatInfoAndroid(short index, short id, short elevation, short azimuth, short snr, short good);
	public static native void pushCameraImageAndroid(byte[] image);

	// From the choreographer.
	public static native void vsync(long frameTimeNanos, long vsyncId, long expectedPresentationTimeNanos);

	// Wrappers
	public static void reportException(Exception e, String data) {
		StringBuilder str = new StringBuilder(e.toString() + "\n" + e.getMessage() + "\n");
		if (data != null) {
			str.append(data).append("\n");
		}
		// could also use import android.util.Log; String stackTrace = Log.getStackTraceString(exception);
		int count = 0;
		for (StackTraceElement ste : e.getStackTrace()) {
			str.append(ste).append("\n");
			// Only bother with the top of the stack.
			if (count > 3) {
				break;
			}
			count++;
		}
		NativeApp.sendMessageFromJava("exception", str.toString());
	}

	public static void reportError(String errorStr) {
		NativeApp.sendMessageFromJava("exception", errorStr);
	}

	@RequiresApi(Build.VERSION_CODES.N)
	private static void processMouseDelta(final MotionEvent ev) {
		if ((ev.getSource() & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE) {
			float dx = ev.getAxisValue(MotionEvent.AXIS_RELATIVE_X);
			float dy = ev.getAxisValue(MotionEvent.AXIS_RELATIVE_Y);
			Log.i(TAG, "Mouse delta: " + dx + " " + dy);
			NativeApp.mouseDelta(dx, dy);
		}
	}

	public static boolean isFromSource(final InputEvent ev, int source) {
		return (ev.getSource() & source) == source;
	}

	private static void onMouseEventMotion(final MotionEvent ev) {
		Log.i(TAG, "motion mouse event");
		switch (ev.getActionMasked()) {
			case MotionEvent.ACTION_DOWN: {
				if (PpssppActivity.useModernMouseEvents) {
					return;
				}
				//Log.i(TAG, "Surface Action down. button state: " + ev.getButtonState());
				NativeApp.mouse(ev.getX(), ev.getY(), 1, 1);
				break;
			}
			case MotionEvent.ACTION_UP: {
				if (PpssppActivity.useModernMouseEvents) {
					return;
				}
				//Log.i(TAG, "Surface Action up. button state: " + ev.getButtonState());
				NativeApp.mouse(ev.getX(), ev.getY(), 1, 2);
				break;
			}
			case MotionEvent.ACTION_MOVE: {
				// This still needs handling here, even if new events are used.
				//Log.i(TAG, "Surface Action move. button state: " + ev.getButtonState());
				NativeApp.mouse(ev.getX(), ev.getY(), 0, 0);
				break;
			}
			default: {
				Log.i(TAG, "Unhandled modern mouse action: " + ev.getAction());
				break;
			}
		}
	}

	// Forwarded from onTouchEvent in the SurfaceView classes.
	public static void processTouchEvent(final MotionEvent ev) {
		if (isFromSource(ev, InputDevice.SOURCE_MOUSE)) {
			// This is where workable mouse support arrived.
			onMouseEventMotion(ev);
		}

		// Log.i(TAG, "processing touch event");
		for (int i = 0; i < ev.getPointerCount(); i++) {
			int pid = ev.getPointerId(i);
			int code = 0;

			final int action = ev.getActionMasked();

			// These code bits are now the same as the constants in input_state.h.
			switch (action) {
				case MotionEvent.ACTION_DOWN:
				case MotionEvent.ACTION_POINTER_DOWN:
					// Log.i(TAG, "ACTION_DOWN");
					if (ev.getActionIndex() == i)
						code = 2;
					break;
				case MotionEvent.ACTION_UP:
				case MotionEvent.ACTION_POINTER_UP:
					// Log.i(TAG, "ACTION_UP");
					if (ev.getActionIndex() == i)
						code = 4;
					break;
				case MotionEvent.ACTION_MOVE: {
					code = 1;
					if (Build.VERSION.SDK_INT >= 24) {
						processMouseDelta(ev);
					}
					break;
				}
				default:
					break;
			}

			if (code != 0) {
				int tool = ev.getToolType(i);
				code |= tool << 10; // We use the Android tool type codes
				NativeApp.touch(ev.getX(i), ev.getY(i), code, pid);
			}
		}
	}
}
