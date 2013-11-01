package org.ppsspp.ppsspp;

import android.os.Bundle;

import com.henrikrydgard.libnative.NativeActivity;
import com.google.analytics.tracking.android.EasyTracker;

public class PpssppActivity extends NativeActivity {
	static { 
		System.loadLibrary("ppsspp_jni"); 
	}

	public PpssppActivity() {
		super();
	}

	@Override
	public void onCreate(Bundle savedInstanceState) {
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