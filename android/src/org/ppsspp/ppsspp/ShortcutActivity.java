package org.ppsspp.ppsspp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.Intent.ShortcutIconResource;
import android.net.Uri;
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

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		// Show file selector dialog here.
		SimpleFileChooser fileDialog = new SimpleFileChooser(this, Environment.getExternalStorageDirectory(), onFileSelectedListener);
		fileDialog.showDialog();
	}

	public static native String queryGameName(String path);

	// Create shortcut as response for ACTION_CREATE_SHORTCUT intent.
	private void respondToShortcutRequest(String path) {
		// This is Intent that will be sent when user execute our shortcut on
		// homescreen. Set our app as target Context. Set Main activity as
		// target class. Add any parameter as data.
		Intent shortcutIntent = new Intent(this, PpssppActivity.class);
		Uri uri = Uri.fromFile(new File(path));
		Log.i(TAG, "Shortcut URI: " + uri.toString());
		shortcutIntent.setData(uri);

		shortcutIntent.putExtra(PpssppActivity.SHORTCUT_EXTRA_KEY, path);

		PpssppActivity.CheckABIAndLoadLibrary();
		String name = queryGameName(path);
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
			respondToShortcutRequest(file.getAbsolutePath());
		}
	};
}
