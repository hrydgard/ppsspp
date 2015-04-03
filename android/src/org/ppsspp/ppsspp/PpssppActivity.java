package org.ppsspp.ppsspp;

import android.app.AlertDialog;
import android.app.UiModeManager;
import android.content.res.Configuration;
import android.graphics.Point;
import android.os.Build;
import android.os.Bundle;
import android.os.Looper;
import android.util.Log;

import com.henrikrydgard.libnative.NativeActivity;
import com.henrikrydgard.libnative.NativeApp;

public class PpssppActivity extends NativeActivity {
	
	private static boolean m_hasUnsupportedABI = false;
	private static boolean m_hasNoNativeBinary = false;
	static {
		
		if(Build.CPU_ABI.equals("armeabi")) {
			m_hasUnsupportedABI = true;
		} else {
			try {
				System.loadLibrary("ppsspp_jni");
			} catch (UnsatisfiedLinkError e) {
				m_hasNoNativeBinary = true;
			}
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
		
		if(m_hasUnsupportedABI || m_hasNoNativeBinary) {
			
			new Thread() {
				@Override
				public void run() {
					Looper.prepare();
					AlertDialog.Builder builder = new AlertDialog.Builder(PpssppActivity.this);
					if (m_hasUnsupportedABI) {
						builder.setMessage(Build.CPU_ABI + " target is not supported.").setTitle("Error starting PPSSPP").create().show();
					} else {
						builder.setMessage("The native part of PPSSPP for ABI " + Build.CPU_ABI + " is missing. Try downloading an official build?").setTitle("Error starting PPSSPP").create().show();
					}
					Looper.loop();
				}
				
			}.start();
			
			try {
				Thread.sleep(3000);
			} catch (InterruptedException e) {
				e.printStackTrace();
			}
			
			System.exit(-1);
			return;
		}

		// In case app launched from homescreen shortcut, get shortcut parameter
		// using Intent extra string. Intent extra will be null if launch normal
		// (from app drawer).
		super.setShortcutParam(getIntent().getStringExtra(SHORTCUT_EXTRA_KEY));

		super.onCreate(savedInstanceState);
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