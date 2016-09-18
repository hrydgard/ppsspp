package org.ppsspp.ppsspp;

import android.app.AlertDialog;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Looper;
import android.util.Log;
import android.widget.Toast;

public class PpssppActivity extends NativeActivity {
	private static final String TAG = "PpssppActivity";
	// Key used by shortcut.
	public static final String SHORTCUT_EXTRA_KEY = "org.ppsspp.ppsspp.Shortcuts";

	private static boolean m_hasUnsupportedABI = false;
	private static boolean m_hasNoNativeBinary = false;

	public static boolean libraryLoaded = false;

	@SuppressWarnings("deprecation")
	public static void CheckABIAndLoadLibrary() {
		if (Build.CPU_ABI.equals("armeabi")) {
			m_hasUnsupportedABI = true;
		} else {
			try {
				System.loadLibrary("ppsspp_jni");
				libraryLoaded = true;
			} catch (UnsatisfiedLinkError e) {
				Log.e(TAG, "LoadLibrary failed, UnsatifiedLinkError: " + e.toString());
				m_hasNoNativeBinary = true;
			}
		}
	}

	static {
		CheckABIAndLoadLibrary();
	}

	public PpssppActivity() {
		super();
	}

	@Override
	public void onCreate(Bundle savedInstanceState) {
		if (m_hasUnsupportedABI || m_hasNoNativeBinary) {
			new Thread() {
				@SuppressWarnings("deprecation")
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
		// (from app drawer or file explorer).
		Intent intent = getIntent();
		String action = intent.getAction();
		if (Intent.ACTION_VIEW.equals(action)) {
			String path = intent.getData().getPath();
			super.setShortcutParam(path);
			Toast.makeText(getApplicationContext(), path, Toast.LENGTH_SHORT).show();
		} else {
			super.setShortcutParam(getIntent().getStringExtra(SHORTCUT_EXTRA_KEY));
		}

		super.onCreate(savedInstanceState);
	}

	// called by the C++ code through JNI. Dispatch anything we can't directly handle
	// on the gfx thread to the UI thread.
	public void postCommand(String command, String parameter) {
		final String cmd = command;
		final String param = parameter;
		runOnUiThread(new Runnable() {
			@Override
			public void run() {
				processCommand(cmd, param);
			}
		});
	}
}
