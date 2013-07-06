package com.henrikrydgard.libnative;

import android.annotation.TargetApi;
import android.os.Build;
import android.util.SparseIntArray;
import android.view.InputDevice;
import android.view.InputDevice.MotionRange;
import android.view.KeyEvent;
import android.view.MotionEvent;

@TargetApi(Build.VERSION_CODES.HONEYCOMB_MR1)
public class InputDeviceState {
	// DEVICE_ID_PAD_0 from the cpp code. TODO: allocate these sequentially if we get more controllers.
	private static int deviceId = 10;
	
	private InputDevice mDevice;
	private int[] mAxes;
		
	InputDevice getDevice() { return mDevice; }
	
	public InputDeviceState(InputDevice device) {
	     mDevice = device;
	     int numAxes = 0;
	     for (MotionRange range : device.getMotionRanges()) {
	          if ((range.getSource() & InputDevice.SOURCE_CLASS_JOYSTICK) != 0) {
	               numAxes += 1;
	          }
	     }

	     mAxes		= new int[numAxes];

	     int i = 0;
	     for (MotionRange range : device.getMotionRanges()) {
	          if ((range.getSource() & InputDevice.SOURCE_CLASS_JOYSTICK) != 0) {
	               mAxes[i++] = range.getAxis();
	          }
	     }
	}
	
	public static float ProcessAxis(InputDevice.MotionRange range, float axisvalue) {
		float absaxisvalue = Math.abs(axisvalue);
		float deadzone = range.getFlat();
		if (absaxisvalue <= deadzone) {
			return 0.0f;
		}
		float normalizedvalue;
		if (axisvalue < 0.0f) {
			normalizedvalue = absaxisvalue / range.getMin();
		} else {
			normalizedvalue = absaxisvalue / range.getMax();
		}

		return normalizedvalue;
	}
	
	private static boolean isGameKey(int keyCode) {
		switch (keyCode) {
		case KeyEvent.KEYCODE_DPAD_UP:
		case KeyEvent.KEYCODE_DPAD_DOWN:
		case KeyEvent.KEYCODE_DPAD_LEFT:
		case KeyEvent.KEYCODE_DPAD_RIGHT:
			return true;
		default:
			return KeyEvent.isGamepadButton(keyCode);
		}
	}
	
	public boolean onKeyDown(KeyEvent event) {
		int keyCode = event.getKeyCode();
		if (event.getRepeatCount() == 0) {
			if (isGameKey(keyCode)) {
				NativeApp.keyDown(deviceId, keyCode);
				return true;
			}
		}
		return false;
	}
	
	public boolean onKeyUp(KeyEvent event) {
	     int keyCode = event.getKeyCode();
	     if (isGameKey(keyCode)) {
	    	 NativeApp.keyUp(deviceId, keyCode);
	         return true;
	     }
	     return false;
	}
	
	public boolean onJoystickMotion(MotionEvent event) {
		if ((event.getSource() & InputDevice.SOURCE_CLASS_JOYSTICK) == 0) {
			return false;
		}
		NativeApp.beginJoystickEvent();
		for (int i = 0; i < mAxes.length; i++) {
			int axisId = mAxes[i];
			float value = event.getAxisValue(axisId);
			// TODO: Use processAxis or move that to the C++ code
			NativeApp.joystickAxis(deviceId, axisId, value);
		}
		NativeApp.endJoystickEvent();

		return true;
	}
}
