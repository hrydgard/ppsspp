package org.ppsspp.ppsspp;

import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import androidx.documentfile.provider.DocumentFile;
import java.util.ArrayList;

public class PpssppActivity extends NativeActivity {
	private static final String TAG = "PpssppActivity";
	// Key used by shortcut.
	public static final String SHORTCUT_EXTRA_KEY = "org.ppsspp.ppsspp.Shortcuts";
	// Key used for debugging.
	public static final String ARGS_EXTRA_KEY = "org.ppsspp.ppsspp.Args";

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
		// String action = intent.getAction();
		Uri data = intent.getData();
		if (data != null) {
			String path = intent.getData().getPath();
			Log.i(TAG, "Found Shortcut Parameter in data: " + path);
			super.setShortcutParam("\"" + path.replace("\\", "\\\\").replace("\"", "\\\"") + "\"");
			// Toast.makeText(getApplicationContext(), path, Toast.LENGTH_SHORT).show();
		} else {
			String param = getIntent().getStringExtra(SHORTCUT_EXTRA_KEY);
			String args = getIntent().getStringExtra(ARGS_EXTRA_KEY);
			Log.e(TAG, "Got ACTION_VIEW without a valid uri, trying param");
			if (param != null) {
				Log.i(TAG, "Found Shortcut Parameter in extra-data: " + param);
				super.setShortcutParam("\"" + param.replace("\\", "\\\\").replace("\"", "\\\"") + "\"");
			} else if (args != null) {
				Log.i(TAG, "Found args parameter in extra-data: " + args);
				super.setShortcutParam(args);
			} else {
				Log.e(TAG, "Shortcut missing parameter!");
				super.setShortcutParam("");
			}
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

	public int openContentUri(String uriString) {
		try {
			Uri uri = Uri.parse(uriString);
			ParcelFileDescriptor filePfd = getContentResolver().openFileDescriptor(uri, "r");
			if (filePfd == null) {
				Log.e(TAG, "Failed to get file descriptor for " + uriString);
				return -1;
			}
			return filePfd.detachFd();  // Take ownership of the fd.
		} catch (Exception e) {
			Log.e(TAG, "Exception opening content uri: " + e.toString());
			return -1;
		}
	}

	private static String fileInfoToString(DocumentFile file) {
		String str = "F|";
		if (file.isDirectory()) {
			str = "D|";
		}
		// TODO: Should we do something with child.isVirtual()?.
		str += file.length() + "|" + file.getName() + "|" + file.getUri() + "|" + file.lastModified();
		return str;
	}

	public String[] listContentUriDir(String uriString) {
		try {
			Uri uri = Uri.parse(uriString);
			DocumentFile documentFile = DocumentFile.fromTreeUri(this, uri);
			DocumentFile[] children = documentFile.listFiles();
			ArrayList<String> listing = new ArrayList<String>();
			// Encode entries into strings for JNI simplicity.
			for (DocumentFile file : children) {
				String str = fileInfoToString(file);
				listing.add(str);
			}
			// Is ArrayList weird or what?
			String[] strings = new String[listing.size()];
			return listing.toArray(strings);
		} catch (Exception e) {
			Log.e(TAG, "Exception opening content uri: " + e.toString());
			return new String[]{};
		}
	}

	public boolean contentUriCreateDirectory(String rootTreeUri, String dirName) {
		try {
			Uri uri = Uri.parse(rootTreeUri);
			DocumentFile documentFile = DocumentFile.fromTreeUri(this, uri);
			if (documentFile != null) {
				DocumentFile createdDir = documentFile.createDirectory(dirName);
				return createdDir != null;
			} else {
				return false;
			}
		} catch (Exception e) {
			Log.e(TAG, "Exception opening content uri: " + e.toString());
			return false;
		}
	}

	public boolean contentUriCreateFile(String rootTreeUri, String fileName) {
		try {
			Uri uri = Uri.parse(rootTreeUri);
			DocumentFile documentFile = DocumentFile.fromTreeUri(this, uri);
			if (documentFile != null) {
				DocumentFile createdFile = documentFile.createFile("application/arbitrary", fileName);
				return createdFile != null;
			} else {
				return false;
			}
		} catch (Exception e) {
			Log.e(TAG, "Exception opening content uri: " + e.toString());
			return false;
		}
	}

	public boolean contentUriRemoveFile(String fileName) {
		try {
			Uri uri = Uri.parse(fileName);
			DocumentFile documentFile = DocumentFile.fromSingleUri(this, uri);
			if (documentFile != null) {
				return documentFile.delete();
			} else {
				return false;
			}
		} catch (Exception e) {
			Log.e(TAG, "Exception opening content uri: " + e.toString());
			return false;
		}
	}

	public String contentUriGetFileInfo(String fileName) {
		try {
			Uri uri = Uri.parse(fileName);
			DocumentFile documentFile = DocumentFile.fromSingleUri(this, uri);
			if (documentFile != null) {
				String str = fileInfoToString(documentFile);
				return str;
			} else {
				return null;
			}
		} catch (Exception e) {
			Log.e(TAG, "Exception opening content uri: " + e.toString());
			return null;
		}
	}
}
