package org.ppsspp.ppsspp;

import android.app.Presentation;
import android.content.Context;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

// Shows the game on a second display (a TV on HDMI, or a wireless display that apps can draw to), as a
// Game output. The phone keeps the touch controls and the menus. See docs/multi-swapchain.md.
public class ExternalDisplayPresentation extends Presentation implements SurfaceHolder.Callback {
	private static final String TAG = "PPSSPPExternalDisplay";

	private int outputId = 0;

	public ExternalDisplayPresentation(Context outerContext, Display display) {
		super(outerContext, display);
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		SurfaceView view = new SurfaceView(getContext());
		view.getHolder().addCallback(this);
		setContentView(view);
	}

	@Override
	public void surfaceCreated(SurfaceHolder holder) {
	}

	@Override
	public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
		if (outputId != 0) {
			NativeApp.resizeOutput(outputId, width, height);
			return;
		}
		outputId = NativeApp.addOutput(holder.getSurface(), width, height);
		Log.i(TAG, "Showing the game on display " + getDisplay().getDisplayId() + " (" + getDisplay().getName() + "), " + width + "x" + height + ", output " + outputId);
	}

	@Override
	public void surfaceDestroyed(SurfaceHolder holder) {
		if (outputId == 0) {
			return;
		}
		Log.i(TAG, "Surface destroyed, removing output " + outputId);
		NativeApp.removeOutput(outputId);
		outputId = 0;
	}
}
