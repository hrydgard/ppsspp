package org.ppsspp.ppsspp;

// Touch- and sensor-enabled GLSurfaceView.
// Used when javaGL = true.
//
// Supports simple multitouch and pressure.
// DPI scaling is handled by the native code.

import android.annotation.SuppressLint;
import android.content.Context;
import android.opengl.GLSurfaceView;
import android.util.Log;
import android.view.InputDevice;
import android.view.MotionEvent;

public class NativeGLSurfaceView extends GLSurfaceView {
	private final static String TAG = "NativeGLView";

	public NativeGLSurfaceView(Context context) {
		super(context);
		Log.i(TAG, "NativeGLSurfaceView constructor");
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
				int tool = ev.getToolType(i);
				code |= tool << 10; // We use the Android tool type codes
				// Can't use || due to short circuit evaluation
				NativeApp.touch(ev.getX(i), ev.getY(i), code, pid);
			}
		}
		return true;
	}
}
