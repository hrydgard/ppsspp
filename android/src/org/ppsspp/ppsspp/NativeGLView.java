package org.ppsspp.ppsspp;

// Touch- and sensor-enabled GLSurfaceView.
// Used when javaGL = true.
//
// Supports simple multitouch and pressure.
// DPI scaling is handled by the native code.

import android.annotation.SuppressLint;
import android.app.Activity;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.opengl.GLSurfaceView;
import android.os.Handler;
import android.util.Log;
import android.view.InputDevice;
import android.view.MotionEvent;

public class NativeGLView extends GLSurfaceView implements SensorEventListener {
	private final static String TAG = "NativeGLView";
	private final SensorManager mSensorManager;
	private final Sensor mAccelerometer;

	NativeActivity mActivity;

	public NativeGLView(NativeActivity activity) {
		super(activity);
		mActivity = activity;

		mSensorManager = (SensorManager) activity.getSystemService(Activity.SENSOR_SERVICE);
		mAccelerometer = mSensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
	}

	private int getToolType(final MotionEvent ev, int pointer) {
		return ev.getToolType(pointer);
	}

	private void onMouseEventMotion(final MotionEvent ev) {
		switch (ev.getActionMasked()) {
			case MotionEvent.ACTION_DOWN: {
				if (NativeActivity.useModernMouseEvents) {
					return;
				}
				Log.i(TAG, "GL motion action down. button state: " + ev.getButtonState());
				NativeApp.mouse(ev.getX(), ev.getY(), 1, 1);
				break;
			}
			case MotionEvent.ACTION_UP: {
				if (NativeActivity.useModernMouseEvents) {
					return;
				}
				Log.i(TAG, "GL motion action up. button state: " + ev.getButtonState());
				NativeApp.mouse(ev.getX(), ev.getY(), 1, 2);
				break;
			}
			case MotionEvent.ACTION_MOVE: {
				// This still needs handling here, even if new events are used.
				Log.i(TAG, "GL motion action move. button state: " + ev.getButtonState());
				NativeApp.mouse(ev.getX(), ev.getY(), 0, 0);
				break;
			}
			default: {
				Log.i(TAG, "Unhandled modern mouse action: " + ev.getAction());
				break;
			}
		}
	}

	@SuppressLint("ClickableViewAccessibility")
	@Override
	public boolean onTouchEvent(final MotionEvent ev) {
		if (NativeSurfaceView.isFromSource(ev, InputDevice.SOURCE_MOUSE)) {
			// This is where workable mouse support arrived.
			onMouseEventMotion(ev);
			return true;
		}

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
				int tool = getToolType(ev, i);
				code |= tool << 10; // We use the Android tool type codes
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
	}

	@Override
	public void onResume() {
		super.onResume();
		mSensorManager.registerListener(this, mAccelerometer, SensorManager.SENSOR_DELAY_GAME);
	}
}
