package org.ppsspp.ppsspp;

import android.annotation.TargetApi;
import android.os.Build;
import android.util.Log;
import android.view.InputDevice;
import android.view.InputDevice.MotionRange;
import android.view.KeyEvent;
import android.view.MotionEvent;

import java.lang.annotation.Target;

@TargetApi(Build.VERSION_CODES.HONEYCOMB_MR1)
public class InputDeviceState {
	private static final String TAG = "InputDeviceState";

	private int deviceId = NativeApp.DEVICE_ID_DEFAULT;

	private InputDevice mDevice;
	private int[] mAxes;

	private int sources;

	InputDevice getDevice() {
		return mDevice;
	}

	@TargetApi(19)
	static void logAdvanced(InputDevice device) {
		Log.i(TAG, "Vendor ID:" + device.getVendorId() + " productId: " + device.getProductId() + " sources: " + String.format("%08x", device.getSources()));
	}

	@TargetApi(19)
	public String getDebugString() {
		String str = mDevice.getName() + " sources: " + String.format("%08x", sources) + "\n  classes: ";

		String[] classes = { "BUTTON ", "POINTER ", "TRACKBALL ", "POSITION ", "JOYSTICK " };
		for (int i = 0; i < 5; i++) {
			if ((sources & (1 << i)) != 0) {
				str += classes[i];
			}
		}
		str += "\n  ";

		// Check the full identifications.
		if ((sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) {
			str += "GAMEPAD ";
		}

		if ((sources & InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD) {
			str += "KEYBOARD";
			if (mDevice.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC) {
				str += "(alpha) ";
			} else {
				str += " ";
			}
		}

		if ((sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
			str += "JOYSTICK ";
		}
		if ((sources & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD) {
			str += "DPAD ";
		}
		if ((sources & InputDevice.SOURCE_HDMI) == InputDevice.SOURCE_HDMI) {
			// what?
			str += "HDMI ";
		}
		if ((sources & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE) {
			str += "MOUSE ";
		}
		if ((sources & InputDevice.SOURCE_MOUSE_RELATIVE) == InputDevice.SOURCE_MOUSE_RELATIVE) {
			str += "MOUSE_RELATIVE ";
		}
		if ((sources & InputDevice.SOURCE_ROTARY_ENCODER) == InputDevice.SOURCE_ROTARY_ENCODER) {
			str += "ROTARY_ENCODER ";
		}
		if ((sources & InputDevice.SOURCE_STYLUS) == InputDevice.SOURCE_STYLUS) {
			str += "STYLUS ";
		}
		if ((sources & InputDevice.SOURCE_TOUCHPAD) == InputDevice.SOURCE_TOUCHPAD) {
			str += "TOUCHPAD ";
		}
		if ((sources & InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN) {
			str += "TOUCHSCREEN ";
		}
		if ((sources & InputDevice.SOURCE_TOUCH_NAVIGATION) == InputDevice.SOURCE_TOUCH_NAVIGATION) {
			str += "TOUCH_NAVIGATION ";
		}
		if ((sources & InputDevice.SOURCE_BLUETOOTH_STYLUS) == InputDevice.SOURCE_BLUETOOTH_STYLUS) {
			str += "BLUETOOTH_STYLUS ";
		}
		str += "\n";
		return str;
	}

	public InputDeviceState(InputDevice device) {
		sources = device.getSources();
		// First, anything that's a gamepad is a gamepad, even if it has a keyboard or pointer.
		if ((sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) {
			this.deviceId = NativeApp.DEVICE_ID_PAD_0;
		} else if ((sources & InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD && device.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC) {
			this.deviceId = NativeApp.DEVICE_ID_KEYBOARD;
		} else if ((sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK ||
				(sources & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD) {
			this.deviceId = NativeApp.DEVICE_ID_PAD_0;
		} else if ((sources & InputDevice.SOURCE_CLASS_POINTER) == InputDevice.SOURCE_CLASS_POINTER) {
			this.deviceId = NativeApp.DEVICE_ID_MOUSE;
		} else {
			// Built-in device buttons like Back etc.
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
		NativeApp.sendMessage("inputDeviceConnectedID", String.valueOf(this.deviceId));
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
