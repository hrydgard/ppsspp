package com.turboviking.libnative;

// Touch- and sensor-enabled GLSurfaceView.
// Supports simple multitouch and pressure.

import android.app.Activity;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.opengl.GLSurfaceView;
import android.util.Log;
import android.view.MotionEvent;

public class NativeGLView extends GLSurfaceView implements SensorEventListener {
	private static String TAG = "NativeGLView";
	private SensorManager mSensorManager;
	private Sensor mAccelerometer;
	
	public NativeGLView(NativeActivity activity) {
		super(activity);
		setEGLContextClientVersion(2);
     // setEGLConfigChooser(5, 5, 5, 0, 16, 0);
     // setDebugFlags(DEBUG_CHECK_GL_ERROR | DEBUG_LOG_GL_CALLS);
		mSensorManager = (SensorManager)activity.getSystemService(Activity.SENSOR_SERVICE);
		mAccelerometer = mSensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
	}

	// This needs fleshing out. A lot.
	// Going to want multitouch eventually.
	public boolean onTouchEvent(final MotionEvent ev) {
		for (int i = 0; i < ev.getPointerCount(); i++) {
			int pid = ev.getPointerId(i);
			int code = 0;
			
			final int action = ev.getActionMasked();
			
			switch (action) {
			case MotionEvent.ACTION_DOWN:
			case MotionEvent.ACTION_POINTER_DOWN:
				if (ev.getActionIndex() == i)
					code = 1;
				break;
			case MotionEvent.ACTION_UP:
			case MotionEvent.ACTION_POINTER_UP:
				if (ev.getActionIndex() == i)
					code = 2;
				break;
			case MotionEvent.ACTION_MOVE:
				code = 3;
				break;
			default:
				break;
			}
			if (code != 0) 
			{
				float x = ev.getX(i);
				float y = ev.getY(i);
				NativeApp.touch(x, y, code, pid);
			}
		}
		return true;
	} 

	// Sensor management
	@Override
	public void onAccuracyChanged(Sensor sensor, int arg1) {
		Log.i(TAG, "onAccuracyChanged");
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
