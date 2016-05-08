package org.ppsspp.ppsspp;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import android.graphics.Point;
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

	private double dpi_scale_x;
	private double dpi_scale_y;

	int last_width, last_height;

	NativeRenderer(NativeActivity act) {
		mActivity = act;
		DisplayMetrics metrics = new DisplayMetrics();
		Display display = act.getWindowManager().getDefaultDisplay();
		display.getMetrics(metrics);
		dpi = metrics.densityDpi;

		refreshRate = display.getRefreshRate();
	}

	double getDpiScaleX() {
		return dpi_scale_x;
	}
	double getDpiScaleY() {
		return dpi_scale_y;
	}

	public void setDark(boolean d) {
		isDark = d;
	}
	
	public void setFixedSize(int xres, int yres, GLSurfaceView surfaceView) {
		Log.i(TAG, "Setting surface to fixed size " + xres + "x" + yres);
		surfaceView.getHolder().setFixedSize(xres, yres);
	}

	public void onDrawFrame(GL10 unused /*use GLES20*/) {
		if (isDark) {
			GLES20.glDisable(GLES20.GL_SCISSOR_TEST);
			GLES20.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT | GLES20.GL_DEPTH_BUFFER_BIT | GLES20.GL_STENCIL_BUFFER_BIT);
		} else {
			displayRender();
		}
	}

	public void onSurfaceCreated(GL10 unused, EGLConfig config) {
		// Log.i(TAG, "onSurfaceCreated - EGL context is new or was lost");
		// Actually, it seems that it is here we should recreate lost GL objects.
		displayInit();
	}
 
	public void onSurfaceChanged(GL10 unused, int width, int height) {
		Point sz = new Point();
		mActivity.GetScreenSize(sz);
		double actualW = sz.x;
		double actualH = sz.y;
		dpi_scale_x = ((double)width / (double)actualW);
		dpi_scale_y = ((double)height / (double)actualH);
		Log.i(TAG, "onSurfaceChanged: " + dpi_scale_x + "x" + dpi_scale_y + " (width=" + width + ", actualW=" + actualW);
		int scaled_dpi = (int)((double)dpi * dpi_scale_x);
		displayResize(width, height, scaled_dpi, refreshRate);
		last_width = width;
		last_height = height;
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
}