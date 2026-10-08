package org.ppsspp.ppsspp;

import android.graphics.Point;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

// Keeps the native side informed of the display and surface size, and applies the hardware
// scaling (fixed surface size) setting.
public class NativeSurfaceManager implements SurfaceHolder.Callback {
	private static final String TAG = "PPSSPPSurfaceManager";

	final PpssppActivity activity;
	SurfaceView surfaceView = null;

	private float densityDpi;

	// The fixed surface size we last asked for, or 0,0 for "size from layout".
	private final Point desiredSize = new Point();

	// What we last told the native side about the surface, and the density we told it with.
	// A density change has to re-report it, see densityChanged().
	private int reportedWidth;
	private int reportedHeight;
	private int reportedFormat;
	private float reportedDensityDpi;

	public NativeSurfaceManager(final PpssppActivity a) {
		activity = a;
	}

	public void setSurfaceView(SurfaceView view) {
		surfaceView = view;
		if (surfaceView == null)
			return;

		surfaceView.getHolder().addCallback(this);
	}

	@Override
	public void surfaceCreated(SurfaceHolder holder) {
		int pixelWidth = holder.getSurfaceFrame().width();
		int pixelHeight = holder.getSurfaceFrame().height();
		Display display = activity.getWindowManager().getDefaultDisplay();

		Log.d(TAG, "Surface created. pixelWidth=" + pixelWidth + ", pixelHeight=" + pixelHeight + " holder: " + holder + " " + display.getRefreshRate() + "Hz");
		NativeApp.setDisplayParameters(pixelWidth, pixelHeight, (int)densityDpi, display.getRefreshRate());
		getDesiredBackbufferSize(desiredSize);

		// Note that desiredSize might be 0,0 here - but that's fine when calling setFixedSize! It means auto.
		if (desiredSize.x == 0) {
			Log.d(TAG, "Setting auto surface size (not fixed)");
		} else {
			Log.d(TAG, "Setting fixed surface size " + desiredSize.x + " x " + desiredSize.y);
		}
		holder.setFixedSize(desiredSize.x, desiredSize.y);
	}

	@Override
	public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
		Log.v(TAG, "surfaceChanged: isCreating:" + holder.isCreating() + " holder: " + holder);
		if (holder.isCreating() && desiredSize.x > 0 && desiredSize.y > 0) {
			// We have called setFixedSize which will trigger another surfaceChanged after the initial
			// one. This one is the original one, and we don't care about it.
			Log.w(TAG, "holder.isCreating = true, ignoring. width=" + width + " height=" + height + " desWidth=" + desiredSize.x + " desHeight=" + desiredSize.y);
			return;
		}

		Log.i(TAG, "Surface changed. Resolution: " + width + "x" + height + " Format: " + format);
		// The window size might have changed (rotation, immersive mode, multi-window)
		updateDisplayMeasurements();

		// A rotation or a window resize doesn't recreate the activity, so a fixed-size (hardware scaled)
		// surface has to be re-fixed here for the new display size and aspect. The fixed size is what the
		// surface will report, so only do it when it doesn't already match, or this would loop.
		if (!holder.isCreating()) {
			Point newDesired = new Point();
			getDesiredBackbufferSize(newDesired);
			boolean fixed = desiredSize.x > 0 && desiredSize.y > 0;
			boolean wantFixed = newDesired.x > 0 && newDesired.y > 0;
			if (wantFixed && (newDesired.x != width || newDesired.y != height)) {
				Log.i(TAG, "Display changed, re-fixing surface size to " + newDesired.x + " x " + newDesired.y);
				desiredSize.set(newDesired.x, newDesired.y);
				holder.setFixedSize(newDesired.x, newDesired.y);
				return;  // Another surfaceChanged is coming, with the new size.
			} else if (!wantFixed && fixed) {
				Log.i(TAG, "Display changed, surface no longer needs a fixed size");
				desiredSize.set(0, 0);
				holder.setSizeFromLayout();
				return;
			}
		}

		reportSurfaceSize(width, height, format);
		activity.notifySurface(holder.getSurface());
	}

	@Override
	public void surfaceDestroyed(SurfaceHolder holder) {
		activity.notifySurface(null);
		reportedWidth = 0;
		reportedHeight = 0;

		// Autosize the next created surface.
		holder.setSizeFromLayout();
	}

	private void reportSurfaceSize(int width, int height, int format) {
		reportedWidth = width;
		reportedHeight = height;
		reportedFormat = format;
		reportedDensityDpi = densityDpi;
		NativeApp.backbufferResize(width, height, format, activity.getWindowManager().getDefaultDisplay().getRotation());
	}

	// A density change rescales the UI but leaves the surface size alone, so no surfaceChanged
	// follows it to carry the new density to the native side - the DPI is only recalculated in
	// backbufferResize. Re-report the surface we already have, or the UI keeps the old scale
	// until the next real resize. Call after updateDisplayMeasurements has picked up the change.
	public void densityChanged() {
		if (reportedWidth <= 0 || densityDpi == reportedDensityDpi) {
			return;
		}
		Log.i(TAG, "Density is now " + densityDpi + ", re-reporting surface " + reportedWidth + "x" + reportedHeight);
		reportSurfaceSize(reportedWidth, reportedHeight, reportedFormat);
	}

	public void updateDisplayMeasurements() {
		Display display = activity.getWindowManager().getDefaultDisplay();
		DisplayMetrics metrics = new DisplayMetrics();
		display.getRealMetrics(metrics);

		// Early in startup, we don't have a view to query, and the display size is the best we have.
		// It's only used for config default heuristics then. Later on, we have the exact size.
		if (surfaceView != null && surfaceView.getWidth() > 0) {
			metrics.widthPixels = surfaceView.getWidth();
			metrics.heightPixels = surfaceView.getHeight();
		}
		densityDpi = metrics.densityDpi;

		NativeApp.setDisplayParameters(metrics.widthPixels, metrics.heightPixels, (int)densityDpi, display.getRefreshRate());
	}

	private void getDesiredBackbufferSize(Point sz) {
		NativeApp.computeDesiredBackbufferDimensions();
		sz.x = NativeApp.getDesiredBackbufferWidth();
		sz.y = NativeApp.getDesiredBackbufferHeight();
	}
}
