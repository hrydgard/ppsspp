package org.ppsspp.ppsspp;

import android.opengl.GLSurfaceView.EGLConfigChooser;
import android.util.Log;
import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLDisplay;

public class NativeEGLConfigChooser implements EGLConfigChooser {
	private static final String TAG = "NativeEGLConfigChooser";

	private static final int EGL_OPENGL_ES2_BIT = 4;

	NativeEGLConfigChooser() {
	}

	private class ConfigAttribs {
		EGLConfig config;
		public int red;
		public int green;
		public int blue;
		public int alpha;
		public int stencil;
		public int depth;
		public int samples;

		public void Log() {
			Log.i(TAG, "EGLConfig: red=" + red + " green=" + green + " blue=" + blue + " alpha=" + alpha + " depth=" + depth + " stencil=" + stencil + " samples=" + samples);
		}
	}

	int getEglConfigAttrib(EGL10 egl, EGLDisplay display, EGLConfig config, int attr) {
		int[] value = new int[1];
		try {
			if (egl.eglGetConfigAttrib(display, config, attr, value))
				return value[0];
			else
				return -1;
		} catch (IllegalArgumentException e) {
			if (config == null) {
				Log.e(TAG, "Called getEglConfigAttrib with null config. Bad developer.");
			} else {
				Log.e(TAG, "Illegal argument to getEglConfigAttrib: attr=" + attr);
			}
			return -1;
		}
	}

	ConfigAttribs[] getConfigAttribs(EGL10 egl, EGLDisplay display, EGLConfig[] configs) {
		ConfigAttribs[] attr = new ConfigAttribs[configs.length];
		for (int i = 0; i < configs.length; i++) {
			ConfigAttribs cfg = new ConfigAttribs();
			cfg.config = configs[i];
			cfg.red = getEglConfigAttrib(egl, display, configs[i], EGL10.EGL_RED_SIZE);
			cfg.green = getEglConfigAttrib(egl, display, configs[i], EGL10.EGL_GREEN_SIZE);
			cfg.blue = getEglConfigAttrib(egl, display, configs[i], EGL10.EGL_BLUE_SIZE);
			cfg.alpha = getEglConfigAttrib(egl, display, configs[i], EGL10.EGL_ALPHA_SIZE);
			cfg.depth = getEglConfigAttrib(egl, display, configs[i], EGL10.EGL_DEPTH_SIZE);
			cfg.stencil = getEglConfigAttrib(egl, display, configs[i], EGL10.EGL_STENCIL_SIZE);
			cfg.samples = getEglConfigAttrib(egl, display, configs[i], EGL10.EGL_SAMPLES);
			attr[i] = cfg;
		}
		return attr;
	}

	@Override
	public EGLConfig chooseConfig(EGL10 egl, EGLDisplay display) {
		// The absolute minimum. We will do our best to choose a better config though.
		int[] configSpec = {
			EGL10.EGL_RED_SIZE, 5,
			EGL10.EGL_GREEN_SIZE, 6,
			EGL10.EGL_BLUE_SIZE, 5,
			EGL10.EGL_DEPTH_SIZE, 16,
			EGL10.EGL_STENCIL_SIZE, 0,
			EGL10.EGL_SURFACE_TYPE, EGL10.EGL_WINDOW_BIT,
			EGL10.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			// EGL10.EGL_TRANSPARENT_TYPE, EGL10.EGL_NONE
			EGL10.EGL_NONE
		};

		int[] num_config = new int[1];
		if (!egl.eglChooseConfig(display, configSpec, null, 0, num_config)) {
			throw new IllegalArgumentException("eglChooseConfig failed when counting");
		}

		int numConfigs = num_config[0];
		Log.i(TAG, "There are " + numConfigs + " egl configs");
		if (numConfigs <= 0) {
			throw new IllegalArgumentException("No configs match configSpec");
		}

		EGLConfig[] eglConfigs = new EGLConfig[numConfigs];
		if (!egl.eglChooseConfig(display, configSpec, eglConfigs, numConfigs, num_config)) {
			throw new IllegalArgumentException("eglChooseConfig failed when retrieving");
		}

		ConfigAttribs[] configs = getConfigAttribs(egl, display, eglConfigs);

		ConfigAttribs chosen = null;

		// Log them all.
		for (int i = 0; i < configs.length; i++) {
			configs[i].Log();
		}

		// We now ignore destination alpha as a workaround for the Mali issue
		// where we get badly composited if we use it.
		// Though, that may be possible to fix by using EGL10.EGL_TRANSPARENT_TYPE, EGL10.EGL_NONE.

		// First, find our ideal configuration. Prefer depth.
		for (int i = 0; i < configs.length; i++) {
			ConfigAttribs c = configs[i];
			if (c.red == 8 && c.green == 8 && c.blue == 8 && c.alpha == 0 && c.stencil >= 8 && c.depth >= 24) {
				chosen = c;
				break;
			}
		}

		if (chosen == null) {
			// Then, prefer one with 20-bit depth (Tegra 3)
			for (int i = 0; i < configs.length; i++) {
				ConfigAttribs c = configs[i];
				if (c.red == 8 && c.green == 8 && c.blue == 8 && c.alpha == 0 && c.stencil >= 8 && c.depth >= 20) {
					chosen = c;
					break;
				}
			}
		}

		if (chosen == null) {
			// Second, accept one with 16-bit depth.
			for (int i = 0; i < configs.length; i++) {
				ConfigAttribs c = configs[i];
				if (c.red == 8 && c.green == 8 && c.blue == 8 && c.alpha == 0 && c.stencil >= 8 && c.depth >= 16) {
					chosen = c;
					break;
				}
			}
		}

		if (chosen == null) {
			// Third, accept one with no stencil.
			for (int i = 0; i < configs.length; i++) {
				ConfigAttribs c = configs[i];
				if (c.red == 8 && c.green == 8 && c.blue == 8 && c.alpha == 0 && c.depth >= 16) {
					chosen = c;
					break;
				}
			}
		}

		if (chosen == null) {
			// Third, accept one with alpha but with stencil, 24-bit depth.
			for (int i = 0; i < configs.length; i++) {
				ConfigAttribs c = configs[i];
				if (c.red == 8 && c.green == 8 && c.blue == 8 && c.alpha == 8 && c.stencil >= 8 && c.depth >= 24) {
					chosen = c;
					break;
				}
			}
		}

		if (chosen == null) {
			// Third, accept one with alpha but with stencil, 16-bit depth.
			for (int i = 0; i < configs.length; i++) {
				ConfigAttribs c = configs[i];
				if (c.red == 8 && c.green == 8 && c.blue == 8 && c.alpha == 8 && c.stencil >= 8 && c.depth >= 16) {
					chosen = c;
					break;
				}
			}
		}

		if (chosen == null) {
			// Fourth, accept one with 16-bit color but depth and stencil required.
			for (int i = 0; i < configs.length; i++) {
				ConfigAttribs c = configs[i];
				if (c.red >= 5 && c.green >= 6 && c.blue >= 5 && c.depth >= 16 && c.stencil >= 8) {
					chosen = c;
					break;
				}
			}
		}

		if (chosen == null) {
			// Fifth, accept one with 16-bit color but depth required.
			for (int i = 0; i < configs.length; i++) {
				ConfigAttribs c = configs[i];
				if (c.red >= 5 && c.green >= 6 && c.blue >= 5 && c.depth >= 16) {
					chosen = c;
					break;
				}
			}
		}

		if (chosen == null) {
			// Final, accept the first one in the list.
			if (configs.length > 0)
				chosen = configs[0];
		}

		if (chosen == null) {
			throw new IllegalArgumentException("Failed to find a valid EGL config");
		}

		Log.i(TAG, "Final chosen config: ");
		chosen.Log();
		return chosen.config;
	}
}
