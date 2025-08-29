package org.ppsspp.ppsspp;

import android.util.Log;
import android.view.InputDevice;
import android.view.InputDevice.MotionRange;
import android.view.KeyEvent;
import android.view.MotionEvent;

public class InputDeviceState {
	private static final String TAG = "InputDeviceState";

	private int deviceId = NativeApp.DEVICE_ID_DEFAULT;

	private final InputDevice mDevice;
	private final int[] mAxes;
	private final float[] mAxisPrevValue;

	// Buffers for the native calls.
	private final int[] mAxisIds;
	private final float[] mValues;

	private final int sources;

	InputDevice getDevice() {
		return mDevice;
	}

	static void logAdvanced(InputDevice device) {
		Log.i(TAG, "Vendor ID:" + device.getVendorId() + " productId: " + device.getProductId() + " sources: " + String.format("%08x", device.getSources()));
	}

	public String getDebugString() {
		StringBuilder str = new StringBuilder(mDevice.getName() + " sources: " + String.format("%08x", sources) + "\n  classes: ");

		String[] classes = { "BUTTON ", "POINTER ", "TRACKBALL ", "POSITION ", "JOYSTICK " };
		for (int i = 0; i < 5; i++) {
			if ((sources & (1 << i)) != 0) {
				str.append(classes[i]);
			}
		}
		str.append("\n  ");

		// Check the full identifications.
		if ((sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) {
			str.append("GAMEPAD ");
		}

		if ((sources & InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD) {
			str.append("KEYBOARD");
			if (mDevice.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC) {
				str.append("(alpha) ");
			} else {
				str.append(" ");
			}
		}

		if ((sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
			str.append("JOYSTICK ");
		}
		if ((sources & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD) {
			str.append("DPAD ");
		}
		if ((sources & InputDevice.SOURCE_HDMI) == InputDevice.SOURCE_HDMI) {
			// what?
			str.append("HDMI ");
		}
		if ((sources & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE) {
			str.append("MOUSE ");
		}
		if ((sources & InputDevice.SOURCE_MOUSE_RELATIVE) == InputDevice.SOURCE_MOUSE_RELATIVE) {
			str.append("MOUSE_RELATIVE ");
		}
		if ((sources & InputDevice.SOURCE_ROTARY_ENCODER) == InputDevice.SOURCE_ROTARY_ENCODER) {
			str.append("ROTARY_ENCODER ");
		}
		if ((sources & InputDevice.SOURCE_STYLUS) == InputDevice.SOURCE_STYLUS) {
			str.append("STYLUS ");
		}
		if ((sources & InputDevice.SOURCE_TOUCHPAD) == InputDevice.SOURCE_TOUCHPAD) {
			str.append("TOUCHPAD ");
		}
		if ((sources & InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN) {
			str.append("TOUCHSCREEN ");
		}
		if ((sources & InputDevice.SOURCE_TOUCH_NAVIGATION) == InputDevice.SOURCE_TOUCH_NAVIGATION) {
			str.append("TOUCH_NAVIGATION ");
		}
		if ((sources & InputDevice.SOURCE_BLUETOOTH_STYLUS) == InputDevice.SOURCE_BLUETOOTH_STYLUS) {
			str.append("BLUETOOTH_STYLUS ");
		}
		str.append("\n");
		return str.toString();
	}

	public static boolean inputSourceIsJoystick(int source) {
		return (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK ||
				(source & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD ||
				(source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD;
	}

	public InputDeviceState(InputDevice device) {
		sources = device.getSources();
		// First, anything that's a gamepad is a gamepad, even if it has a keyboard or pointer.
		if ((sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) {
			this.deviceId = NativeApp.DEVICE_ID_PAD_0;
		} else if ((sources & InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD && device.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC) {
			this.deviceId = NativeApp.DEVICE_ID_KEYBOARD;
		} else if (inputSourceIsJoystick(sources)) {
			this.deviceId = NativeApp.DEVICE_ID_PAD_0;
		} else if ((sources & InputDevice.SOURCE_CLASS_POINTER) == InputDevice.SOURCE_CLASS_POINTER) {
			this.deviceId = NativeApp.DEVICE_ID_MOUSE;
		} else {
			// Built-in device buttons like Back etc.
			this.deviceId = NativeApp.DEVICE_ID_DEFAULT;
		}

		mDevice = device;
		final int numAxes = device.getMotionRanges().size();

		mAxes = new int[numAxes];
		mAxisPrevValue = new float[numAxes];
		mAxisIds = new int[numAxes];
		mValues = new float[numAxes];

		int i = 0;
		for (MotionRange range : device.getMotionRanges()) {
			mAxes[i++] = range.getAxis();
		}

		Log.i(TAG, "Registering input device with " + numAxes + " axes: " + device.getName());
		logAdvanced(device);
		NativeApp.sendMessageFromJava("inputDeviceConnectedID", String.valueOf(this.deviceId));
		NativeApp.sendMessageFromJava("inputDeviceConnected", device.getName());
	}

	// This is called from dispatchKeyEvent.
	public boolean onKeyDown(KeyEvent event) {
		int keyCode = event.getKeyCode();
		boolean repeat = event.getRepeatCount() > 0;
		return NativeApp.keyDown(deviceId, keyCode, repeat);
	}

	// This is called from dispatchKeyEvent.
	public boolean onKeyUp(KeyEvent event) {
		int keyCode = event.getKeyCode();
		return NativeApp.keyUp(deviceId, keyCode);
	}

	public boolean onJoystickMotion(MotionEvent event) {
		if (!inputSourceIsJoystick(event.getSource())) {
			Log.i(TAG, "Not a joystick event: source = " + event.getSource());
			return false;
		}
		Log.i(TAG, "onjoystick");
		int count = 0;
		for (int i = 0; i < mAxes.length; i++) {
			int axisId = mAxes[i];
			float value = event.getAxisValue(axisId);
			if (value != mAxisPrevValue[i]) {
				mAxisIds[count] = axisId;
				mValues[count] = value;
				count++;
				mAxisPrevValue[i] = value;
			}
		}
		NativeApp.joystickAxis(deviceId, mAxisIds, mValues, count);
		return true;
	}
}
