package org.ppsspp.ppsspp;

// Touch- and sensor-enabled SurfaceView.
// Supports simple multitouch and pressure.
// DPI scaling is handled by the native code.

import android.annotation.SuppressLint;
import android.annotation.TargetApi;
import android.app.Activity;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.os.Handler;
import android.util.Log;
import android.view.MotionEvent;
import android.view.SurfaceView;
import com.bda.controller.*;

public class NativeSurfaceView extends SurfaceView implements SensorEventListener, ControllerListener {
	private static String TAG = "NativeSurfaceView";
	private SensorManager mSensorManager;
	private Sensor mAccelerometer;

	// Moga controller
	private Controller mController = null;
	private boolean isMogaPro = false;

	public NativeSurfaceView(NativeActivity activity) {
		super(activity);

		Log.i(TAG, "NativeSurfaceView");

		mSensorManager = (SensorManager) activity.getSystemService(Activity.SENSOR_SERVICE);
		mAccelerometer = mSensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);

		mController = Controller.getInstance(activity);

		// this.getHolder().setFormat(PixelFormat.RGBA_8888);

		try {
			MogaHack.init(mController, activity);
			Log.i(TAG, "MOGA initialized");
			mController.setListener(this, new Handler());
		} catch (Exception e) {
			// Ignore.
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

		int numTouchesHandled = 0;
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
				numTouchesHandled += NativeApp.touch(ev.getX(i), ev.getY(i), code, pid) ? 1 : 0;
			}
		}
		return numTouchesHandled > 0;
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

	public void onPause() {
		mSensorManager.unregisterListener(this);
		if (mController != null) {
			mController.onPause();
		}
	}

	public void onResume() {
		mSensorManager.registerListener(this, mAccelerometer, SensorManager.SENSOR_DELAY_GAME);
		if (mController != null) {
			mController.onResume();
			// According to the docs, the Moga's state can be inconsistent here.
			// Should we do a one time poll?
		}
	}

	public void onDestroy() {
		Log.i(TAG, "onDestroy");
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
		NativeApp.joystickAxis(NativeApp.DEVICE_ID_PAD_0, com.bda.controller.MotionEvent.AXIS_X, event.getAxisValue(com.bda.controller.MotionEvent.AXIS_X));
		NativeApp.joystickAxis(NativeApp.DEVICE_ID_PAD_0, com.bda.controller.MotionEvent.AXIS_Y, event.getAxisValue(com.bda.controller.MotionEvent.AXIS_Y));
		NativeApp.joystickAxis(NativeApp.DEVICE_ID_PAD_0, com.bda.controller.MotionEvent.AXIS_Z, event.getAxisValue(com.bda.controller.MotionEvent.AXIS_Z));
		NativeApp.joystickAxis(NativeApp.DEVICE_ID_PAD_0, com.bda.controller.MotionEvent.AXIS_RZ, event.getAxisValue(com.bda.controller.MotionEvent.AXIS_RZ));
		NativeApp.joystickAxis(NativeApp.DEVICE_ID_PAD_0, com.bda.controller.MotionEvent.AXIS_LTRIGGER, event.getAxisValue(com.bda.controller.MotionEvent.AXIS_LTRIGGER));
		NativeApp.joystickAxis(NativeApp.DEVICE_ID_PAD_0, com.bda.controller.MotionEvent.AXIS_RTRIGGER, event.getAxisValue(com.bda.controller.MotionEvent.AXIS_RTRIGGER));
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
					NativeApp.sendMessage("moga", "Moga");
				} else {
					Log.i(TAG, "MOGA Pro detected");
					isMogaPro = true;
					NativeApp.sendMessage("moga", "MogaPro");
				}
				break;
			case StateEvent.ACTION_CONNECTING:
				Log.i(TAG, "Moga Connecting...");
				break;
			case StateEvent.ACTION_DISCONNECTED:
				Log.i(TAG, "Moga Disconnected (or simply Not connected)");
				NativeApp.sendMessage("moga", "");
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
