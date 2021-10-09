package org.ppsspp.ppsspp;

import android.annotation.TargetApi;
import android.os.Build;
import android.util.Log;
import android.view.InputDevice;
import android.view.InputDevice.MotionRange;
import android.view.KeyEvent;
import android.view.MotionEvent;

@TargetApi(Build.VERSION_CODES.HONEYCOMB_MR1)
public class InputDeviceState {
	private static final String TAG = "InputDeviceState";

	private int deviceId = NativeApp.DEVICE_ID_DEFAULT;

	private InputDevice mDevice;
	private int[] mAxes;

	InputDevice getDevice() {
		return mDevice;
	}

	@TargetApi(19)
	void logAdvanced(InputDevice device) {
		Log.i(TAG, "Vendor ID:" + device.getVendorId() + " productId: " + device.getProductId());
	}

	public InputDeviceState(InputDevice device) {
		int sources = device.getSources();
		// First, anything that's a gamepad is a gamepad, even if it has a keyboard or pointer.
		if ((sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) {
			this.deviceId = NativeApp.DEVICE_ID_PAD_0;
		} else if ((sources & InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD && device.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC) {
			this.deviceId = NativeApp.DEVICE_ID_KEYBOARD;
		} else if ((sources & InputDevice.SOURCE_CLASS_POINTER) == InputDevice.SOURCE_CLASS_POINTER) {
			this.deviceId = NativeApp.DEVICE_ID_MOUSE;
		} else if ((sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK ||
				(sources & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD) {
			this.deviceId = NativeApp.DEVICE_ID_PAD_0;
		} else {
			// Built-in buttons like Back etc.
			this.deviceId = NativeApp.DEVICE_ID_DEFAULT;
		}

		mDevice = device;
		int numAxes = 0;
		for (MotionRange range : device.getMotionRanges()) {
			numAxes += 1;
		}

		mAxes = new int[numAxes];

		int i = 0;
		for (MotionRange range : device.getMotionRanges()) {
			mAxes[i++] = range.getAxis();
		}

		Log.i(TAG, "Registering input device with " + numAxes + " axes: " + device.getName());
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
			logAdvanced(device);
		}
		NativeApp.sendMessage("inputDeviceConnected", device.getName());
	}

	public boolean onKeyDown(KeyEvent event) {
		int keyCode = event.getKeyCode();
		boolean repeat = event.getRepeatCount() > 0;
		return NativeApp.keyDown(deviceId, keyCode, repeat);
	}

	public boolean onKeyUp(KeyEvent event) {
		int keyCode = event.getKeyCode();
		return NativeApp.keyUp(deviceId, keyCode);
	}

	public boolean onJoystickMotion(MotionEvent event) {
		if ((event.getSource() & InputDevice.SOURCE_CLASS_JOYSTICK) == 0) {
			return false;
		}
		NativeApp.beginJoystickEvent();
		for (int i = 0; i < mAxes.length; i++) {
			int axisId = mAxes[i];
			float value = event.getAxisValue(axisId);
			NativeApp.joystickAxis(deviceId, axisId, value);
		}
		NativeApp.endJoystickEvent();
		return true;
	}
}
