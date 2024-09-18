package org.ppsspp.ppsspp;

import android.annotation.TargetApi;
import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.graphics.Point;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.DisplayCutout;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import java.lang.Runnable;

public class SizeManager implements SurfaceHolder.Callback {
	private static String TAG = "PPSSPPSizeManager";

	final NativeActivity activity;
	SurfaceView surfaceView = null;

	private int safeInsetLeft = 0;
	private int safeInsetRight = 0;
	private int safeInsetTop = 0;
	private int safeInsetBottom = 0;

	private float densityDpi;
	private float refreshRate;
	private int pixelWidth;
	private int pixelHeight;

	private boolean navigationHidden = false;
	private boolean displayUpdatePending = false;

	private Point desiredSize = new Point();
	private int badOrientationCount = 0;


	private boolean paused = false;

	public SizeManager(final NativeActivity a) {
		activity = a;
	}


	public void setPaused(boolean p) {
		paused = p;
	}

	@TargetApi(Build.VERSION_CODES.P)
	public void setSurfaceView(SurfaceView view) {
		surfaceView = view;
		if (surfaceView == null)
			return;

		surfaceView.getHolder().addCallback(this);

		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
			surfaceView.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
				@Override
				public WindowInsets onApplyWindowInsets(View view, WindowInsets windowInsets) {
					updateInsets(windowInsets);
					return windowInsets;
				}
			});
		}
	}

	@Override
	public void surfaceCreated(SurfaceHolder holder) {
		pixelWidth = holder.getSurfaceFrame().width();
		pixelHeight = holder.getSurfaceFrame().height();

		// Workaround for terrible bug when locking and unlocking the screen in landscape mode on Nexus 5X.
		int requestedOr = activity.getRequestedOrientation();
		boolean requestedPortrait = requestedOr == ActivityInfo.SCREEN_ORIENTATION_PORTRAIT || requestedOr == ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT;
		boolean detectedPortrait = pixelHeight > pixelWidth;
		if (Build.VERSION.SDK_INT < Build.VERSION_CODES.KITKAT && badOrientationCount < 3 && requestedPortrait != detectedPortrait && requestedOr != ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED) {
			Log.e(TAG, "Bad orientation detected (w=" + pixelWidth + " h=" + pixelHeight + "! Recreating activity.");
			badOrientationCount++;
			activity.recreate();
			return;
		} else if (requestedPortrait == detectedPortrait) {
			Log.i(TAG, "Correct orientation detected, resetting orientation counter.");
			badOrientationCount = 0;
		} else {
			Log.i(TAG, "Bad orientation detected but ignored" + (Build.VERSION.SDK_INT < Build.VERSION_CODES.KITKAT ? " (sdk version)" : ""));
		}

		Display display = activity.getWindowManager().getDefaultDisplay();

		refreshRate = display.getRefreshRate();

		Log.d(TAG, "Surface created. pixelWidth=" + pixelWidth + ", pixelHeight=" + pixelHeight + " holder: " + holder.toString() + " or: " + requestedOr + " " + refreshRate + "Hz");
		NativeApp.setDisplayParameters(pixelWidth, pixelHeight, (int)densityDpi, refreshRate);
		getDesiredBackbufferSize(desiredSize);

		// Note that desiredSize might be 0,0 here - but that's fine when calling setFixedSize! It means auto.
		Log.d(TAG, "Setting fixed size " + desiredSize.x + " x " + desiredSize.y);
		holder.setFixedSize(desiredSize.x, desiredSize.y);
	}

	@Override
	public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
		Log.v(TAG, "surfaceChanged: isCreating:" + holder.isCreating() + " holder: " + holder.toString());
		if (holder.isCreating() && desiredSize.x > 0 && desiredSize.y > 0) {
			// We have called setFixedSize which will trigger another surfaceChanged after the initial
			// one. This one is the original one and we don't care about it.
			Log.w(TAG, "holder.isCreating = true, ignoring. width=" + width + " height=" + height + " desWidth=" + desiredSize.x + " desHeight=" + desiredSize.y);
			return;
		}

		Log.w(TAG, "Surface changed. Resolution: " + width + "x" + height + " Format: " + format);
		// The window size might have changed (immersive mode, native fullscreen on some devices)
		NativeApp.backbufferResize(width, height, format);
		updateDisplayMeasurements();

		if (!paused) {
			activity.notifySurface(holder.getSurface());
		} else {
			Log.i(TAG, "Skipping notifySurface while paused");
		}
	}

	@Override
	public void surfaceDestroyed(SurfaceHolder holder) {
		activity.notifySurface(null);

		// Autosize the next created surface.
		holder.setSizeFromLayout();
	}

	public void checkDisplayMeasurements() {
		if (displayUpdatePending) {
			return;
		}
		displayUpdatePending = true;

		activity.runOnUiThread(() -> {
			Log.d(TAG, "checkDisplayMeasurements: checking now");
			updateDisplayMeasurements();
		});
	}

	@TargetApi(Build.VERSION_CODES.JELLY_BEAN_MR1)
	public void updateDisplayMeasurements() {
		displayUpdatePending = false;

		Display display = activity.getWindowManager().getDefaultDisplay();
		// Early in startup, we don't have a view to query. Do our best to get some kind of size
		// that can be used by config default heuristics, and so on.
		DisplayMetrics metrics = new DisplayMetrics();
		if (navigationHidden && Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1) {
			display.getRealMetrics(metrics);
		} else {
			display.getMetrics(metrics);
		}

		// Later on, we have the exact pixel size so let's just use it.
		if (surfaceView != null) {
			metrics.widthPixels = surfaceView.getWidth();
			metrics.heightPixels = surfaceView.getHeight();
		}
		densityDpi = metrics.densityDpi;
		refreshRate = display.getRefreshRate();

		NativeApp.setDisplayParameters(metrics.widthPixels, metrics.heightPixels, (int)densityDpi, refreshRate);
	}

	@TargetApi(Build.VERSION_CODES.KITKAT)
	public void setupSystemUiCallback(final View view) {
		view.setOnSystemUiVisibilityChangeListener(new View.OnSystemUiVisibilityChangeListener() {
			@Override
			public void onSystemUiVisibilityChange(int visibility) {
				// Called when the system UI's visibility changes, regardless of
				// whether it's because of our or system actions.
				// We will try to force it to follow our preference but will not stupidly
				// act as if it's visible if it's not.
				navigationHidden = ((visibility & View.SYSTEM_UI_FLAG_HIDE_NAVIGATION) != 0);
				// TODO: Check here if it's the state we want.
				Log.i(TAG, "SystemUiVisibilityChange! visibility=" + visibility + " navigationHidden: " + navigationHidden);
				Log.i(TAG, "decorView: " + view.getWidth() + "x" + view.getHeight());
				checkDisplayMeasurements();
			}
		});
	}

	public void updateDpi(float dpi) {
		densityDpi = dpi;
	}

	private void getDesiredBackbufferSize(Point sz) {
		NativeApp.computeDesiredBackbufferDimensions();
		sz.x = NativeApp.getDesiredBackbufferWidth();
		sz.y = NativeApp.getDesiredBackbufferHeight();
	}

	@TargetApi(Build.VERSION_CODES.P)
	private void updateInsets(WindowInsets insets) {
		if (insets == null) {
			return;
		}
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
			DisplayCutout cutout = insets.getDisplayCutout();
			if (cutout != null) {
				safeInsetLeft = cutout.getSafeInsetLeft();
				safeInsetRight = cutout.getSafeInsetRight();
				safeInsetTop = cutout.getSafeInsetTop();
				safeInsetBottom = cutout.getSafeInsetBottom();
				Log.i(TAG, "Safe insets: left: " + safeInsetLeft + " right: " + safeInsetRight + " top: " + safeInsetTop + " bottom: " + safeInsetBottom);
			} else {
				Log.i(TAG, "Safe insets: Cutout was null");
				safeInsetLeft = 0;
				safeInsetRight = 0;
				safeInsetTop = 0;
				safeInsetBottom = 0;
			}
			NativeApp.sendMessageFromJava("safe_insets", safeInsetLeft + ":" + safeInsetRight + ":" + safeInsetTop + ":" + safeInsetBottom);
		}
	}
}
