package org.ppsspp.ppsspp;

import android.app.Activity;
import android.content.Intent;
import android.content.Intent.ShortcutIconResource;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.documentfile.provider.DocumentFile;

import java.io.File;

/**
 * This class will respond to android.intent.action.CREATE_SHORTCUT intent from launcher homescreen.
 * Register this class in AndroidManifest.xml.
 */
public class ShortcutActivity extends Activity {
	private static final String TAG = "PPSSPP";

	private static final int RESULT_OPEN_DOCUMENT = 2;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		// Ensure library is loaded before any native methods are called.
		PpssppActivity.CheckABIAndLoadLibrary();

		// Show file selector dialog here. If Android version is more than or equal to 11,
		// use the native document file browser instead of our SimpleFileChooser.
		boolean scoped = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R);
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
				Log.i(TAG, "Taking URI permission");
				getContentResolver().takePersistableUriPermission(selectedFile, Intent.FLAG_GRANT_READ_URI_PERMISSION);
				Log.i(TAG, "Browse file finished:" + selectedFile);
				respondToShortcutRequest(selectedFile);  // finishes.
				return;
			}
		}

		// We're done, no matter how it went.
		finish();
	}

	private static native Object[] queryGameInfo(android.app.Activity activity, String path);

	// Create shortcut as response for ACTION_CREATE_SHORTCUT intent.
	private void respondToShortcutRequest(@NonNull Uri uri) {
		// This is Intent that will be sent when user execute our shortcut on
		// homescreen. Set our app as target Context. Set Main activity as
		// target class. Add any parameter as data.
		Intent shortcutIntent = new Intent(getApplicationContext(), PpssppActivity.class);
		shortcutIntent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
		Log.i(TAG, "Shortcut URI: " + uri);
		shortcutIntent.setData(uri);
		shortcutIntent.putExtra(PpssppActivity.SHORTCUT_EXTRA_KEY, uri.toString());

		String name = "PPSSPP Game";
		DocumentFile docFile = DocumentFile.fromSingleUri(this, uri);
		if (docFile != null && docFile.getName() != null) {
			name = docFile.getName();
		}

		String gameName = name;
		Log.i(TAG, "Fallback name from URI: " + name);

		byte[] iconData;
		Object[] result = queryGameInfo(this, uri.toString());
		if (result != null && result.length >= 2) {
			if (result[0] != null) {
				gameName = (String) result[0];     // index 0 = name
			}
			iconData = (byte[]) result[1];     // index 1 = raw PNG/JPEG bytes, or null
			Log.i(TAG, "Game name: " + gameName);
		} else {
			iconData = null;
			Log.e(TAG, "Bad return value from queryGameInfo");
		}

		Log.i(TAG, "Game name: " + gameName + " : Creating shortcut to " + uri);

		// This is Intent that will be returned by this method, as response to
		// ACTION_CREATE_SHORTCUT. Wrap shortcut intent inside this intent.
		Intent responseIntent = new Intent();
		responseIntent.putExtra(Intent.EXTRA_SHORTCUT_INTENT, shortcutIntent);
		responseIntent.putExtra(Intent.EXTRA_SHORTCUT_NAME, gameName);

		if (iconData != null) {
			try {
				// Try to create a PNG from the iconData.
				Bitmap bmp = BitmapFactory.decodeByteArray(iconData, 0, iconData.length);
				if (bmp != null) {
					// Pad the bitmap into a square, to keep it a nice 2:1 aspect ratio.
					Bitmap paddedBitmap = Bitmap.createBitmap(
						bmp.getWidth(), bmp.getWidth(),
						Bitmap.Config.ARGB_8888);
					Canvas canvas = new Canvas(paddedBitmap);
					canvas.drawARGB(0, 0, 0, 0);
					int y = (bmp.getWidth() - bmp.getHeight()) / 2;
					if (y < 0) {
						// To be safe from wacky-aspect-ratio bitmaps.
						y = 0;
					}
					canvas.drawBitmap(bmp, 0, y, new Paint(Paint.FILTER_BITMAP_FLAG));
					// Bitmap scaledBitmap = Bitmap.createScaledBitmap(bmp, 144, 72, true);
					responseIntent.putExtra(Intent.EXTRA_SHORTCUT_ICON, paddedBitmap);
					bmp.recycle();
				}
			} catch (Exception e) {
				NativeApp.reportException(e, "Error assigning generated icon");
				Log.e(TAG, "Error assigning generated icon: " + e);
			}
		} else {
			Log.i(TAG, "No icon available, falling back to PPSSPP icon");
			try {
				// Fall back to the PPSSPP icon.
				ShortcutIconResource iconResource = ShortcutIconResource.fromContext(this, R.mipmap.ic_launcher);
				responseIntent.putExtra(Intent.EXTRA_SHORTCUT_ICON_RESOURCE, iconResource);
			} catch (Exception e) {
				NativeApp.reportException(e, "Error assigning default icon");
				Log.e(TAG, "Error assigning default icon: " + e);
			}
		}

		setResult(RESULT_OK, responseIntent);

		// Must call finish for result to be returned immediately
		try {
			finish();
		} catch (Exception e) {
			NativeApp.reportException(e, "Error finishing respondToShortcutRequest");
		}
		Log.i(TAG, "End of respondToShortcutRequest");
	}

	// Event when a file is selected on file dialog.
	private final SimpleFileChooser.FileSelectedListener onFileSelectedListener = file -> {
		// create shortcut using file path
		Uri uri = Uri.fromFile(new File(file.getAbsolutePath()));
		respondToShortcutRequest(uri);
	};
}
