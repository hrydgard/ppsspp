package org.ppsspp.ppsspp;

import android.annotation.TargetApi;
import android.app.Presentation;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.util.Log;
import android.view.Display;

import java.lang.annotation.Native;

@TargetApi(Build.VERSION_CODES.JELLY_BEAN_MR1)
class NativeDisplayListener implements DisplayManager.DisplayListener {

	private static String TAG = "NativeDisplayListener";

	private DisplayManager displayManager;
	private NativeActivity nativeActivity;

	NativeDisplayListener(DisplayManager displayManager, NativeActivity nativeActivity) {
		this.displayManager = displayManager;
		this.nativeActivity = nativeActivity;
	}

	@Override
	public void onDisplayAdded(int displayId) {
		Display display = displayManager.getDisplay(displayId);
		Log.i(TAG, String.format("display connected: %s %s", displayId, display.getName()));
		nativeActivity.updatePresentation(display);
	}

	@Override
	public void onDisplayRemoved(int displayId) {

	}

	@Override
	public void onDisplayChanged(int displayId) {

	}
}
