package org.ppsspp.ppsspp;

import android.app.AlertDialog;
import android.graphics.Point;
import android.os.Build;
import android.os.Bundle;
import android.os.Looper;

import com.google.analytics.tracking.android.EasyTracker;
import com.henrikrydgard.libnative.NativeActivity;
import com.henrikrydgard.libnative.NativeApp;

public class PpssppActivity extends NativeActivity {
	
	private static boolean m_hasUnsopportedABI = false;
	
	static {
		
		if(Build.CPU_ABI.equals("armeabi")) {
			m_hasUnsopportedABI = true;
		} else {
			System.loadLibrary("ppsspp_jni");
		}
	}

	// Key used by shortcut.
	public static final String SHORTCUT_EXTRA_KEY = "org.ppsspp.ppsspp.Shortcuts";
	
	public static final String TAG = "PpssppActivity";
	
	public PpssppActivity() {
		super();
	}

	@Override
	public void onCreate(Bundle savedInstanceState) {
		
		if(m_hasUnsopportedABI) {
			
			new Thread() {
				@Override
				public void run() {
					Looper.prepare();
					AlertDialog.Builder builder = new AlertDialog.Builder(PpssppActivity.this);
					builder.setMessage(Build.CPU_ABI + " target is not supported.").setTitle("Error").create().show();
					Looper.loop();
				}
				
			}.start();
			
			try {
				Thread.sleep(3000);
			} catch (InterruptedException e) {
				e.printStackTrace();
			}
			
			System.exit(-1);
		}
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

	private void correctRatio(Point sz, float scale) {
		float x = sz.x;
		float y = sz.y;
		float ratio = x / y;
		// Log.i(TAG, "Considering size: " + sz.x + "x" + sz.y + "=" + ratio);
		float targetRatio;
		if (x >= y) {
			targetRatio = 480.0f / 272.0f;
			x = 480.f * scale;
			y = 272.f * scale;
		} else {
			targetRatio = 272.0f / 480.0f;
			x = 272.0f * scale;
			y = 480.0f * scale;
		}
		float correction = targetRatio / ratio;
		// Log.i(TAG, "Target ratio: " + targetRatio + " ratio: " + ratio + " correction: " + correction);
		if (ratio < targetRatio) {
			y *= correction;
		} else {
			x /= correction;
		}
		sz.x = (int)x;
		sz.y = (int)y;
		// Log.i(TAG, "Corrected ratio: " + sz.x + "x" + sz.y);
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
		correctRatio(sz, (float)scale);
	}
}  