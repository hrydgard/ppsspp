package org.ppsspp.ppsspp;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
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

	private String copyAndDownscaleToCache(Uri selectedImage) {
		Log.i(TAG, "Selected image: " + selectedImage);
		File cacheDir = getExternalCacheDir();
		if (cacheDir == null) {
			Log.e(TAG, "External cache directory not available");
			return null;
		}
		File tempFile = new File(cacheDir, "temp_import.jpg");

		try {
			// First, just get the dimensions.
			BitmapFactory.Options options = new BitmapFactory.Options();
			options.inJustDecodeBounds = true;
			try (InputStream in = getContentResolver().openInputStream(selectedImage)) {
				if (in != null) {
					BitmapFactory.decodeStream(in, null, options);
				} else {
					Log.e(TAG, "Failed to open input stream for dimensions");
					return null;
				}
			}

			int width = options.outWidth;
			int height = options.outHeight;
			Log.i(TAG, "Image dimensions: " + width + "x" + height);

			int maxDim = 2048;
			if (width <= maxDim && height <= maxDim && width > 0 && height > 0) {
				// No downscaling needed, just copy for maximum quality.
				try (InputStream in = getContentResolver().openInputStream(selectedImage);
					 OutputStream out = new FileOutputStream(tempFile)) {
					if (in == null) {
						Log.e(TAG, "Failed to open input stream for copying");
						return null;
					}
					byte[] buf = new byte[65536];
					int len;
					while ((len = in.read(buf)) > 0) {
						out.write(buf, 0, len);
					}
				}
			} else if (width > 0 && height > 0) {
				// Downscaling needed.
				float ratio = Math.min((float) maxDim / width, (float) maxDim / height);
				int targetWidth = Math.round(width * ratio);
				int targetHeight = Math.round(height * ratio);

				// Calculate inSampleSize to save memory during decoding.
				options.inJustDecodeBounds = false;
				options.inSampleSize = 1;
				while (width / (options.inSampleSize * 2) >= targetWidth && height / (options.inSampleSize * 2) >= targetHeight) {
					options.inSampleSize *= 2;
				}

				try (InputStream in = getContentResolver().openInputStream(selectedImage)) {
					if (in == null) {
						Log.e(TAG, "Failed to open input stream for downscaling");
						return null;
					}
					Bitmap bitmap = BitmapFactory.decodeStream(in, null, options);
					if (bitmap != null) {
						Bitmap scaledBitmap = Bitmap.createScaledBitmap(bitmap, targetWidth, targetHeight, true);
						try (OutputStream out = new FileOutputStream(tempFile)) {
							scaledBitmap.compress(Bitmap.CompressFormat.JPEG, 90, out);
						}
						if (scaledBitmap != bitmap) {
							bitmap.recycle();
						}
						scaledBitmap.recycle();
					} else {
						Log.e(TAG, "Failed to decode bitmap for downscaling");
						return null;
					}
				}
			} else {
				Log.e(TAG, "Invalid image dimensions: " + width + "x" + height);
				return null;
			}

			// Pass the local path back instead of the content:// URI
			String returnPath = tempFile.getAbsolutePath();
			Log.i(TAG, "Return path: " + returnPath);
			return returnPath;
		} catch (IOException e) {
			Log.e(TAG, "Error processing image: " + e.getMessage());
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

		Log.i(TAG, "Setting up activity launch, requestId = " + requestId + (savedInstanceState == null ? " (new)" : " (recreated)"));

		ActivityResultLauncher<Intent> launcher = registerForActivityResult(
			new ActivityResultContracts.StartActivityForResult(),
			result -> {
				Log.i(TAG, "Packing return intent, requestId = " + requestId);

				Intent returnIntent = new Intent(this, PpssppActivity.class);
				if (result.getResultCode() == Activity.RESULT_OK && result.getData() != null) {
					String localPath = copyAndDownscaleToCache(result.getData().getData());
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

		// Only launch the picker if we are starting fresh. If we're being recreated,
		// the ActivityResultRegistry will handle delivering the pending result to the launcher.
		if (savedInstanceState == null) {
			if (pickerIntent != null) {
				launcher.launch(pickerIntent);
			} else {
				Log.e(TAG, "No picker intent provided");
				finish();
			}
		}
	}
}
