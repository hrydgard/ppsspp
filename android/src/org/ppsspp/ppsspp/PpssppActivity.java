package org.ppsspp.ppsspp;

import android.graphics.Point;
import android.os.Bundle;
import android.util.Log;

import com.henrikrydgard.libnative.NativeActivity;
import com.henrikrydgard.libnative.NativeApp;
import com.google.analytics.tracking.android.EasyTracker;

public class PpssppActivity extends NativeActivity {
	static {
		System.loadLibrary("ppsspp_jni");
	}

	// Key used by shortcut.
	public static final String SHORTCUT_EXTRA_KEY = "org.ppsspp.ppsspp.Shortcuts";
	
	public static final String TAG = "PpssppActivity";
	
	public PpssppActivity() {
		super();
	}

	@Override
	public void onCreate(Bundle savedInstanceState) {
		// In case app launched from homescreen shortcut, get shortcut parameter
		// using Intent extra string. Intent extra will be null if launch normal
		// (from app drawer).
		super.setShortcutParam(getIntent().getStringExtra(SHORTCUT_EXTRA_KEY));

		super.onCreate(savedInstanceState);
	}

	@Override
	public void onStart() {
		super.onStart();
		EasyTracker.getInstance(this).activityStart(this);
	}

	@Override
	public void onStop() {
		super.onStop();
		EasyTracker.getInstance(this).activityStop(this);
	}
	
	@Override
	public void getDesiredBackbufferSize(Point sz) {
		GetScreenSize(sz);
		String config = NativeApp.queryConfig("hwScale");
		int scale;
		try {
			scale = Integer.parseInt(config);
			if (scale == 0) {
				sz.x = 0;
				sz.y = 0;
				return;
			}
		}
		catch (NumberFormatException e) {
			sz.x = 0;
			sz.y = 0;
			return;
		}
		
		float ratio = (float)sz.x / (float)sz.y;
		//Log.i(TAG, "GetScreenSize returned: " + sz.x + "x" + sz.y + "=" + ratio);
		float targetRatio;
		if (sz.x > sz.y) {
			targetRatio = 480.0f / 272.0f;
			sz.x = 480 * scale;
			sz.y = 272 * scale;
		} else {
			targetRatio = 272.0f / 480.0f;
			sz.x = 272 * scale;
			sz.y = 480 * scale;
		}
		//Log.i(TAG, "Target ratio: " + targetRatio + " ratio: " + ratio);
		float correction = targetRatio / ratio;
		if (ratio < targetRatio) {
			sz.y *= correction;
		} else {
			sz.x *= correction;
		}
		//Log.i(TAG, "Corrected ratio: " + sz.x + " x " + sz.y);
	}
}