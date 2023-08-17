package org.ppsspp.ppsspp;

import android.opengl.GLSurfaceView;
import android.util.Log;
import android.widget.Toast;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.opengles.GL10;

// Only used for the OpenGL backend.
public class NativeRenderer implements GLSurfaceView.Renderer {
	private static String TAG = "NativeRenderer";
	private NativeActivity mActivity;
	private boolean inFrame = false;
	private boolean failed = false;

	NativeRenderer(NativeActivity act) {
		mActivity = act;
	}

	public boolean isRenderingFrame() {
		return inFrame;
	}

	// TODO: Make use of this somehow.
	public boolean hasFailedInit() { return failed; }

	public void onDrawFrame(GL10 unused /*use GLES20*/) {
		inFrame = true;
		displayRender();
		inFrame = false;
	}

	public void onSurfaceCreated(GL10 gl, EGLConfig config) {
		failed = false;
		Log.i(TAG, "NativeRenderer (OpenGL): onSurfaceCreated");

		EGL10 egl = (EGL10)EGLContext.getEGL();
		if (egl != null) {
			EGLDisplay dpy = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);
			if (dpy != null) {
				int[] val = new int[1];
				egl.eglGetConfigAttrib(dpy, config, EGL10.EGL_DEPTH_SIZE, val);
				int depthBits = val[0];
				egl.eglGetConfigAttrib(dpy, config, EGL10.EGL_STENCIL_SIZE, val);
				int stencilBits = val[0];
				Log.i(TAG, "EGL reports " + depthBits + " bits of depth and " + stencilBits + " bits of stencil.");
			} else {
				Log.e(TAG, "dpy == null");
			}
		} else {
			Log.e(TAG, "egl == null");
		}

		if (!displayInit()) {
			Log.e(TAG, "Display init failed");
			failed = true;
		}
	}

	public void onSurfaceChanged(GL10 unused, int width, int height) {}

	// Note: This also means "device lost" and you should reload
	// all buffered objects.
	public native boolean displayInit();
	public native void displayRender();
}
