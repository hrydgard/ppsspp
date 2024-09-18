package org.ppsspp.ppsspp;

// Touch- and sensor-enabled GLSurfaceView.
// Used when javaGL = true.
//
// Supports simple multitouch and pressure.
// DPI scaling is handled by the native code.

import android.annotation.SuppressLint;
import android.annotation.TargetApi;
import android.app.Activity;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Handler;
import android.util.Log;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceControl;

import com.bda.controller.Controller;
import com.bda.controller.ControllerListener;
import com.bda.controller.KeyEvent;
import com.bda.controller.StateEvent;

public class NativeGLView extends GLSurfaceView implements SensorEventListener, ControllerListener {
	private static String TAG = "NativeGLView";
	private SensorManager mSensorManager;
	private Sensor mAccelerometer;

	// Moga controller
	private Controller mController = null;
	private boolean isMogaPro = false;

	NativeActivity mActivity;

	public NativeGLView(NativeActivity activity) {
		super(activity);
		mActivity = activity;

		mSensorManager = (SensorManager) activity.getSystemService(Activity.SENSOR_SERVICE);
		mAccelerometer = mSensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);

		mController = Controller.getInstance(activity);

		try {
			MogaHack.init(mController, activity);
			Log.i(TAG, "MOGA initialized");
			mController.setListener(this, new Handler());
		} catch (Exception e) {
			// Log.d(TAG, "MOGA failed to initialize");
		}
	}

	@TargetApi(Build.VERSION_CODES.ICE_CREAM_SANDWICH)
	private int getToolType(final MotionEvent ev, int pointer) {
		return ev.getToolType(pointer);
	}

	@SuppressLint("ClickableViewAccessibility")
	@Override
	public boolean onTouchEvent(final MotionEvent ev) {
		boolean canReadToolType = Build.VERSION.SDK_INT >= Build.VERSION_CODES.ICE_CREAM_SANDWICH;

		for (int i = 0; i < ev.getPointerCount(); i++) {
			int pid = ev.getPointerId(i);
			int code = 0;

			final int action = ev.getActionMasked();

			// These code bits are now the same as the constants in input_state.h.
			switch (action) {
			case MotionEvent.ACTION_DOWN:
			case MotionEvent.ACTION_POINTER_DOWN:
				if (ev.getActionIndex() == i)
					code = 2;
				break;
			case MotionEvent.ACTION_UP:
			case MotionEvent.ACTION_POINTER_UP:
				if (ev.getActionIndex() == i)
					code = 4;
				break;
			case MotionEvent.ACTION_MOVE:
				code = 1;
				break;
			default:
				break;
			}

			if (code != 0) {
				if (canReadToolType) {
					int tool = getToolType(ev, i);
					code |= tool << 10; // We use the Android tool type codes
				}
				// Can't use || due to short circuit evaluation
				NativeApp.touch(ev.getX(i), ev.getY(i), code, pid);
			}
		}
		return true;
	}

	// Sensor management
	@Override
	public void onAccuracyChanged(Sensor sensor, int arg1) {
	}

	@Override
	public void onSensorChanged(SensorEvent event) {
		if (event.sensor.getType() != Sensor.TYPE_ACCELEROMETER) {
			return;
		}
		// Can also look at event.timestamp for accuracy magic
		NativeApp.accelerometer(event.values[0], event.values[1], event.values[2]);
	}

	@Override
	public void onPause() {
		super.onPause();
		mSensorManager.unregisterListener(this);
		if (mController != null) {
			mController.onPause();
		}
	}

	@Override
	public void onResume() {
		super.onResume();
		mSensorManager.registerListener(this, mAccelerometer, SensorManager.SENSOR_DELAY_GAME);
		if (mController != null) {
			mController.onResume();
			// According to the docs, the Moga's state can be inconsistent here.
			// We should do a one time poll. TODO
		}
	}

	public void onDestroy() {
		if (mController != null) {
			mController.exit();
		}
	}

	// MOGA Controller - from ControllerListener
	@Override
	public void onKeyEvent(KeyEvent event) {
		// The Moga left stick doubles as a D-pad. This creates mapping conflicts so let's turn it off.
		// Unfortunately this breaks menu navigation in PPSSPP currently but meh.
		// This is different on Moga Pro though.

		if (!isMogaPro) {
			switch (event.getKeyCode()) {
			case KeyEvent.KEYCODE_DPAD_DOWN:
			case KeyEvent.KEYCODE_DPAD_UP:
			case KeyEvent.KEYCODE_DPAD_LEFT:
			case KeyEvent.KEYCODE_DPAD_RIGHT:
				return;
			default:
				break;
			}
		}

		boolean repeat = false; // Moga has no repeats?
		switch (event.getAction()) {
		case KeyEvent.ACTION_DOWN:
			NativeApp.keyDown(NativeApp.DEVICE_ID_PAD_0, event.getKeyCode(), repeat);
			break;
		case KeyEvent.ACTION_UP:
			NativeApp.keyUp(NativeApp.DEVICE_ID_PAD_0, event.getKeyCode());
			break;
		}
	}

	// MOGA Controller - from ControllerListener
	@Override
	public void onMotionEvent(com.bda.controller.MotionEvent event) {
		int [] axisIds = new int[]{
			com.bda.controller.MotionEvent.AXIS_X,
			com.bda.controller.MotionEvent.AXIS_Y,
			com.bda.controller.MotionEvent.AXIS_Z,
			com.bda.controller.MotionEvent.AXIS_RZ,
			com.bda.controller.MotionEvent.AXIS_LTRIGGER,
			com.bda.controller.MotionEvent.AXIS_RTRIGGER,
		};
		float [] values = new float[]{
			event.getAxisValue(com.bda.controller.MotionEvent.AXIS_X),
			event.getAxisValue(com.bda.controller.MotionEvent.AXIS_Y),
			event.getAxisValue(com.bda.controller.MotionEvent.AXIS_Z),
			event.getAxisValue(com.bda.controller.MotionEvent.AXIS_RZ),
			event.getAxisValue(com.bda.controller.MotionEvent.AXIS_LTRIGGER),
			event.getAxisValue(com.bda.controller.MotionEvent.AXIS_RTRIGGER),
		};
		NativeApp.joystickAxis(NativeApp.DEVICE_ID_PAD_0, axisIds, values, 6);
	}

	// MOGA Controller - from ControllerListener
	@Override
	public void onStateEvent(StateEvent state) {
		switch (state.getState()) {
		case StateEvent.STATE_CONNECTION:
			switch (state.getAction()) {
			case StateEvent.ACTION_CONNECTED:
				Log.i(TAG, "Moga Connected");
				if (mController.getState(Controller.STATE_CURRENT_PRODUCT_VERSION) == Controller.ACTION_VERSION_MOGA) {
					NativeApp.sendMessageFromJava("moga", "Moga");
				} else {
					Log.i(TAG, "MOGA Pro detected");
					isMogaPro = true;
					NativeApp.sendMessageFromJava("moga", "MogaPro");
				}
				break;
			case StateEvent.ACTION_CONNECTING:
				Log.i(TAG, "Moga Connecting...");
				break;
			case StateEvent.ACTION_DISCONNECTED:
				Log.i(TAG, "Moga Disconnected (or simply Not connected)");
				NativeApp.sendMessageFromJava("moga", "");
				break;
			}
			break;

		case StateEvent.STATE_POWER_LOW:
			switch (state.getAction()) {
			case StateEvent.ACTION_TRUE:
				Log.i(TAG, "Moga Power Low");
				break;
			case StateEvent.ACTION_FALSE:
				Log.i(TAG, "Moga Power OK");
				break;
			}
			break;

		default:
			break;
		}
	}
}
