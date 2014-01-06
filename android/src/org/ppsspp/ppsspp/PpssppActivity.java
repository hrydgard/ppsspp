package org.ppsspp.ppsspp;

import android.os.Bundle;

import com.henrikrydgard.libnative.NativeActivity;
import com.google.analytics.tracking.android.EasyTracker;

public class PpssppActivity extends NativeActivity {
	static {
		System.loadLibrary("ppsspp_jni");
	}

	// Key used by shortcut.
	public static final String SHORTCUT_EXTRA_KEY = "org.ppsspp.ppsspp.Shortcuts";

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
}