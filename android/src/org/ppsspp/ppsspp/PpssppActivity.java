package org.ppsspp.ppsspp;

import androidx.annotation.Keep;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.system.StructStatVfs;
import android.system.Os;
import android.os.storage.StorageManager;
import android.content.ContentResolver;
import android.database.Cursor;
import android.provider.DocumentsContract;

import androidx.annotation.RequiresApi;
import androidx.documentfile.provider.DocumentFile;

import java.util.ArrayList;
import java.util.UUID;
import java.io.File;

public class PpssppActivity extends NativeActivity {
	private static final String TAG = "PpssppActivity";
	// Key used by shortcut.
	public static final String SHORTCUT_EXTRA_KEY = "org.ppsspp.ppsspp.Shortcuts";
	// Key used for debugging.
	public static final String ARGS_EXTRA_KEY = "org.ppsspp.ppsspp.Args";

	private static boolean m_hasNoNativeBinary = false;

	public static boolean libraryLoaded = false;

	public static void CheckABIAndLoadLibrary() {
		try {
			System.loadLibrary("ppsspp_jni");
			libraryLoaded = true;
		} catch (UnsatisfiedLinkError e) {
			Log.e(TAG, "LoadLibrary failed, UnsatifiedLinkError: " + e);
			m_hasNoNativeBinary = true;
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
		if (m_hasNoNativeBinary) {
			new Thread() {
				@Override
				public void run() {
					Looper.prepare();
					AlertDialog.Builder builder = new AlertDialog.Builder(PpssppActivity.this);
					builder.setMessage("The native part of PPSSPP for ABI " + Build.CPU_ABI + " is missing. Try downloading an official build?").setTitle("Error starting PPSSPP").create().show();
					Looper.loop();
				}
			}.start();

			try {
				Thread.sleep(3000);
			} catch (InterruptedException e) {
				e.printStackTrace();
			}

			// We don't call super.onCreate, we just bail in an ugly way.
			System.exit(-1);
			return;
		}

		// In case app launched from homescreen shortcut, get shortcut parameter
		// using Intent extra string. Intent extra will be null if launch normal
		// (from app drawer or file explorer).
		String shortcutParam = parseIntent(getIntent());
		if (shortcutParam != null) {
			Log.i(TAG, "Found Shortcut Parameter in data, passing on: " + shortcutParam);
			super.setShortcutParam(shortcutParam);
		}
		super.onCreate(savedInstanceState);
	}

	private static String parseIntent(Intent intent) {
		Uri data = intent.getData();
		if (data != null) {
			String path = data.toString();
			// Do some unescaping. Not really sure why needed.
			return "\"" + path.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
		} else {
			String param = intent.getStringExtra(SHORTCUT_EXTRA_KEY);
			String args = intent.getStringExtra(ARGS_EXTRA_KEY);
			if (param != null) {
				Log.i(TAG, "Found Shortcut Parameter in extra-data: " + param);
				return "\"" + param.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
			} else if (args != null) {
				Log.i(TAG, "Found args parameter in extra-data: " + args);
				return args;
			} else {
				return null;
			}
		}
	}

	@Override
	public void onNewIntent(Intent intent) {
		super.onNewIntent(intent);
		String value = parseIntent(intent);
		if (value != null) {
			// TODO: Actually send a command to the native code to launch the new game.
			Log.i(TAG, "NEW INTENT AT RUNTIME: " + value);
			Log.i(TAG, "Posting a 'shortcutParam' message to the C++ code.");
			NativeApp.sendMessageFromJava("shortcutParam", value);
		}
	}

	// called by the C++ code through JNI. Dispatch anything we can't directly handle
	// on the gfx thread to the UI thread.
	@Keep
	@SuppressWarnings("unused")
	public void postCommand(String command, String parameter) {
		final String cmd = command;
		final String param = parameter;
		runOnUiThread(new Runnable() {
			@Override
			public void run() {
				if (!processCommand(cmd, param)) {
					Log.e(TAG, "processCommand failed: cmd: '" + cmd + "' param: '" + param + "'");
				}
			}
		});
	}

	@Keep
	@SuppressWarnings("unused")
	public String getDebugString(String str) {
		if (str.equals("InputDevice")) {
			return getInputDeviceDebugString();
		} else {
			return "bad debug string: " + str;
		}
	}
}
