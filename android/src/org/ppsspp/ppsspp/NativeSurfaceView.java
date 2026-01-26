package org.ppsspp.ppsspp;

// Touch-enabled SurfaceView.
// Supports simple multitouch and pressure.
// Used by the Vulkan backend.

import android.annotation.SuppressLint;
import android.content.Context;
import android.view.MotionEvent;
import android.view.SurfaceView;

public class NativeSurfaceView extends SurfaceView {
	public NativeSurfaceView(Context context) {
		super(context);
	}

	@SuppressLint("ClickableViewAccessibility")
	@Override
	public boolean onTouchEvent(final MotionEvent ev) {
		if (ev.getAction() == MotionEvent.ACTION_UP) {
			super.performClick();
		}
		NativeApp.processTouchEvent(ev);
		return true;
	}
}
