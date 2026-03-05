package org.ppsspp.ppsspp;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class ImageResultProxyActivity extends AppCompatActivity {
	public static final String TAG = "PPSSPP";

	private String copyToCache(Uri selectedImage) {
		Log.i(TAG, "Selected image: " + selectedImage);
		File tempFile = new File(getExternalCacheDir(), "temp_import.jpg");

		try (InputStream in = getContentResolver().openInputStream(selectedImage);
			 OutputStream out = new FileOutputStream(tempFile)) {

			byte[] buf = new byte[8192];
			int len;
			while ((len = in.read(buf)) > 0) {
				out.write(buf, 0, len);
			}

			// Pass the local path back instead of the content:// URI
			String returnPath = tempFile.getAbsolutePath();
			Log.i(TAG, "Return path: " + returnPath);
			return returnPath;
		} catch (IOException e) {
			e.printStackTrace();
			return null;
		}
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		// Get the Intent that was meant for the picker
		Intent pickerIntent = getIntent().getParcelableExtra("picker_intent");
		int requestId = getIntent().getIntExtra("request_id", -1);

		Log.i(TAG, "Setting up activity launch, requestId = " + requestId);

		ActivityResultLauncher<Intent> launcher = registerForActivityResult(
			new ActivityResultContracts.StartActivityForResult(),
			result -> {
				Log.i(TAG, "Packing return intent, requestId = " + requestId);

				Intent returnIntent = new Intent(this, PpssppActivity.class);
				if (result.getResultCode() == Activity.RESULT_OK && result.getData() != null) {
					String localPath = copyToCache(result.getData().getData());
					Log.i(TAG, "Putting extra: " + localPath);
					// Pass the result back to the SingleInstance activity.
					returnIntent.putExtra("result_path", localPath);
				}

				returnIntent.putExtra("result_code", result.getResultCode());
				returnIntent.putExtra("request_id", requestId);

				// This flag is key: it finds the existing instance of your main activity
				returnIntent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
				startActivity(returnIntent);
				finish();
			}
		);

		launcher.launch(pickerIntent);
	}
}
