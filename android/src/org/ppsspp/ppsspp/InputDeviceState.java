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

	private static final int deviceId = NativeApp.DEVICE_ID_PAD_0;

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
		mDevice = device;
		int numAxes = 0;
		for (MotionRange range : device.getMotionRanges()) {
			if ((range.getSource() & InputDevice.SOURCE_CLASS_JOYSTICK) != 0) {
				numAxes += 1;
			}
		}

		mAxes = new int[numAxes];

		int i = 0;
		for (MotionRange range : device.getMotionRanges()) {
			if ((range.getSource() & InputDevice.SOURCE_CLASS_JOYSTICK) != 0) {
				mAxes[i++] = range.getAxis();
			}
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
