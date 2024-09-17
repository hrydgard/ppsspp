package org.ppsspp.ppsspp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.Intent.ShortcutIconResource;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Looper;
import android.util.Log;
import java.io.File;
import java.nio.charset.StandardCharsets;

/**
 * This class will respond to android.intent.action.CREATE_SHORTCUT intent from launcher homescreen.
 * Register this class in AndroidManifest.xml.
 */
public class ShortcutActivity extends Activity {
	private static final String TAG = "PPSSPP";

	private boolean scoped = false;
	private static final int RESULT_OPEN_DOCUMENT = 2;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		// Show file selector dialog here. If Android version is more than or equal to 11,
		// use the native document file browser instead of our SimpleFileChooser.
		scoped = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R);

		if (scoped) {
			try {
				Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
				intent.addCategory(Intent.CATEGORY_OPENABLE);
				intent.setType("*/*");
				intent.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
				// Possible alternative approach:
				// String[] mimeTypes = {"application/octet-stream", "/x-iso9660-image"};
				// intent.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes);
				startActivityForResult(intent, RESULT_OPEN_DOCUMENT);
				// intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, pickerInitialUri);
				Log.i(TAG, "Starting open document activity");
			} catch (Exception e) {
				Log.e(TAG, e.toString());
			}
		} else {
			SimpleFileChooser fileDialog = new SimpleFileChooser(this, Environment.getExternalStorageDirectory(), onFileSelectedListener);
			fileDialog.showDialog();
		}
	}

	// Respond to native file dialog.
	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);
		if (requestCode == RESULT_OPEN_DOCUMENT && data != null) {
			Uri selectedFile = data.getData();
			if (selectedFile != null) {
				// Grab permanent permission so we can show it in recents list etc.
				if (Build.VERSION.SDK_INT >= 19) {
					Log.i(TAG, "Taking URI permission");
					getContentResolver().takePersistableUriPermission(selectedFile, Intent.FLAG_GRANT_READ_URI_PERMISSION);
				}
				Log.i(TAG, "Browse file finished:" + selectedFile.toString());
				respondToShortcutRequest(selectedFile);  // finishes.
				return;
			}
		}

		// We're done, no matter how it went.
		finish();
	}

	public static native String queryGameName(String path);
	public static native byte[] queryGameIcon(String path);

	// Create shortcut as response for ACTION_CREATE_SHORTCUT intent.
	private void respondToShortcutRequest(Uri uri) {
		// This is Intent that will be sent when user execute our shortcut on
		// homescreen. Set our app as target Context. Set Main activity as
		// target class. Add any parameter as data.
		Intent shortcutIntent = new Intent(getApplicationContext(), PpssppActivity.class);
		shortcutIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
		shortcutIntent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP);
		Log.i(TAG, "Shortcut URI: " + uri.toString());
		shortcutIntent.setData(uri);
		String path = uri.toString();
		shortcutIntent.putExtra(PpssppActivity.SHORTCUT_EXTRA_KEY, path);

		// We can't call C++ functions here that use storage APIs since there's no
		// NativeActivity and all the AndroidStorage methods are methods on that.
		// Should probably change that. In the meantime, let's just process the URI to make
		// up a name.

		String name = "PPSSPP Game";
		String pathStr = "PPSSPP Game";
		if (path.startsWith("content://")) {
			String [] segments = path.split("/");
			try {
				pathStr = java.net.URLDecoder.decode(segments[segments.length - 1], "UTF-8");
			} catch (Exception e) {
				Log.i(TAG, "Exception getting name: " + e);
			}
		} else if (path.startsWith("file:///")) {
			try {
				pathStr = java.net.URLDecoder.decode(path.substring(7), "UTF-8");
			} catch (Exception e) {
				Log.i(TAG, "Exception getting name: " + e);
			}
		} else {
			pathStr = path;
		}

		String[] pathSegments = pathStr.split("/");
		name = pathSegments[pathSegments.length - 1];

		PpssppActivity.CheckABIAndLoadLibrary();
		String gameName = queryGameName(path);
		byte [] iconData = null;
		if (gameName.equals("")) {
			Log.i(TAG, "Failed to retrieve game name - ignoring.");
			// This probably happened because PPSSPP isn't running so the GameInfoCache isn't working.
			// Let's just continue with our fallback name until we can fix that.
			// showBadGameMessage();
			// return;
		} else {
			name = gameName;
			iconData = queryGameIcon(path);
		}

		Log.i(TAG, "Game name: " + name + " : Creating shortcut to " + uri.toString());

		// This is Intent that will be returned by this method, as response to
		// ACTION_CREATE_SHORTCUT. Wrap shortcut intent inside this intent.
		Intent responseIntent = new Intent();
		responseIntent.putExtra(Intent.EXTRA_SHORTCUT_INTENT, shortcutIntent);
		responseIntent.putExtra(Intent.EXTRA_SHORTCUT_NAME, name);

		boolean setIcon = false;
		if (iconData != null) {
			// Try to create a PNG from the iconData.
			Bitmap bmp = BitmapFactory.decodeByteArray(iconData, 0, iconData.length);
			if (bmp != null) {
				// Scale it to a square.
				Bitmap scaledBitmap = Bitmap.createScaledBitmap(bmp, 144, 144, true);
				responseIntent.putExtra(Intent.EXTRA_SHORTCUT_ICON, scaledBitmap);
			}
			setIcon = true;
		}
		if (!setIcon) {
			// Fall back to the PPSSPP icon.
			ShortcutIconResource iconResource = ShortcutIconResource.fromContext(this, R.drawable.ic_launcher);
			responseIntent.putExtra(Intent.EXTRA_SHORTCUT_ICON_RESOURCE, iconResource);
		}
		setResult(RESULT_OK, responseIntent);

		// Must call finish for result to be returned immediately
		finish();
	}

	private void showBadGameMessage() {
		new Thread() {
			@Override
			public void run() {
				Looper.prepare();
				AlertDialog.Builder builder = new AlertDialog.Builder(ShortcutActivity.this);
				builder.setMessage(getResources().getString(R.string.bad_disc_message));
				builder.setTitle(getResources().getString(R.string.bad_disc_title));
				builder.create().show();
				Looper.loop();
			}
		}.start();

		try {
			Thread.sleep(3000);
		} catch (InterruptedException e) {
			e.printStackTrace();
		}
	}

	// Event when a file is selected on file dialog.
	private SimpleFileChooser.FileSelectedListener onFileSelectedListener = new SimpleFileChooser.FileSelectedListener() {
		@Override
		public void onFileSelected(File file) {
			// create shortcut using file path
			Uri uri = Uri.fromFile(new File(file.getAbsolutePath()));
			respondToShortcutRequest(uri);
		}
	};
}
