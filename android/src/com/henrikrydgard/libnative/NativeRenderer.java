package com.henrikrydgard.libnative;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import android.content.Context;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;

public class NativeRenderer implements GLSurfaceView.Renderer {
	private static String TAG = "NativeRenderer";
	private NativeActivity mActivity;
	private boolean isDark = false;
	private int dpi;
	private float refreshRate;

	NativeRenderer(NativeActivity act) {
		mActivity = act;

		DisplayMetrics metrics = new DisplayMetrics();
		Display display = act.getWindowManager().getDefaultDisplay();
		display.getMetrics(metrics);
		dpi = metrics.densityDpi;
		refreshRate = display.getRefreshRate();

		// Log.i(TAG, "Display name: " + display.getName());
		Log.i(TAG, " rate: " + refreshRate + " dpi: " + dpi);
	}

	public void setDark(boolean d) {
		isDark = d;
	}
	
	@Override
	public void onDrawFrame(GL10 unused /*use GLES20*/) {
		if (isDark) {
			GLES20.glDisable(GLES20.GL_SCISSOR_TEST);
			GLES20.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT | GLES20.GL_DEPTH_BUFFER_BIT | GLES20.GL_STENCIL_BUFFER_BIT);
		} else {
			displayRender();
		}
	}

	@Override
	public void onSurfaceCreated(GL10 unused, EGLConfig config) {
		// Log.i(TAG, "onSurfaceCreated - EGL context is new or was lost");
		// Actually, it seems that it is here we should recreate lost GL objects.
		displayInit();
	}
 
	@Override
	public void onSurfaceChanged(GL10 unused, int width, int height) {
		Log.i(TAG, "onSurfaceChanged");
		displayResize(width, height, dpi, refreshRate);
	}

	// Not override, it's custom.
	public void onDestroyed() {
		displayShutdown();
	}
	
	// NATIVE METHODS

	// Note: This also means "device lost" and you should reload
	// all buffered objects. 
	public native void displayInit(); 
	public native void displayResize(int w, int h, int dpi, float refreshRate);
	public native void displayRender();
	public native void displayShutdown();
	
	// called by the C++ code through JNI. Dispatch anything we can't directly handle
	// on the gfx thread to the UI thread.
	public void postCommand(String command, String parameter) {
		final String cmd = command;
		final String param = parameter;
		mActivity.runOnUiThread(new Runnable() {
			public void run() {
				NativeRenderer.this.mActivity.processCommand(cmd, param);
			}
		});
	}
}