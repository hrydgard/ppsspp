package org.ppsspp.ppsspp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.Intent.ShortcutIconResource;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Looper;
import android.util.Log;
import java.io.File;

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
		// use the native file browser instead of our SimpleFileChooser.
		scoped = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q);

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
		if (resultCode == RESULT_OPEN_DOCUMENT) {
			Uri selectedFile = data.getData();
			if (selectedFile != null) {
				// Grab permanent permission so we can show it in recents list etc.
				if (Build.VERSION.SDK_INT >= 19) {
					getContentResolver().takePersistableUriPermission(selectedFile, Intent.FLAG_GRANT_READ_URI_PERMISSION);
				}
				Log.i(TAG, "Browse file finished:" + selectedFile.toString());

			}
		}
	}

	public static native String queryGameName(String path);

	// Create shortcut as response for ACTION_CREATE_SHORTCUT intent.
	private void respondToShortcutRequest(Uri uri) {
		// This is Intent that will be sent when user execute our shortcut on
		// homescreen. Set our app as target Context. Set Main activity as
		// target class. Add any parameter as data.
		Intent shortcutIntent = new Intent(this, PpssppActivity.class);
		Log.i(TAG, "Shortcut URI: " + uri.toString());
		shortcutIntent.setData(uri);

		shortcutIntent.putExtra(PpssppActivity.SHORTCUT_EXTRA_KEY, path);

		PpssppActivity.CheckABIAndLoadLibrary();
		String name = queryGameName(uri.toString());
		if (name.equals("")) {
			showBadGameMessage();
			return;
		}

		// This is Intent that will be returned by this method, as response to
		// ACTION_CREATE_SHORTCUT. Wrap shortcut intent inside this intent.
		Intent responseIntent = new Intent();
		responseIntent.putExtra(Intent.EXTRA_SHORTCUT_INTENT, shortcutIntent);
		responseIntent.putExtra(Intent.EXTRA_SHORTCUT_NAME, name);
		ShortcutIconResource iconResource = ShortcutIconResource.fromContext(this, R.drawable.ic_launcher);
		responseIntent.putExtra(Intent.EXTRA_SHORTCUT_ICON_RESOURCE, iconResource);

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

		System.exit(-1);
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
