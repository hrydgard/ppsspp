package org.ppsspp.ppsspp;

// Touch-enabled GLSurfaceView.
// Used when javaGL = true.

import android.annotation.SuppressLint;
import android.content.Context;
import android.opengl.GLSurfaceView;
import android.view.MotionEvent;

public class NativeGLSurfaceView extends GLSurfaceView {
	public NativeGLSurfaceView(Context context) {
		super(context);
	}

	@SuppressLint("ClickableViewAccessibility")
	@Override
	public boolean onTouchEvent(final MotionEvent ev) {
		NativeApp.processTouchEvent(ev);
		return true;
	}
}
