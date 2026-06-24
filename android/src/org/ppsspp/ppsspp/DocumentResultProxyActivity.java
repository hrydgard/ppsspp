package org.ppsspp.ppsspp;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;
import androidx.documentfile.provider.DocumentFile;

public class DocumentResultProxyActivity extends AppCompatActivity {
	public static final String TAG = "PPSSPP";

	private void returnWithResult(int resultCode, int requestId, String resultPath) {
		Intent returnIntent = new Intent(this, PpssppActivity.class);
		returnIntent.putExtra("result_code", resultCode);
		returnIntent.putExtra("request_id", requestId);
		if (resultPath != null) {
			returnIntent.putExtra("result_path", resultPath);
		}
		returnIntent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
		startActivity(returnIntent);
		finish();
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		// Get the Intent that was meant for the picker
		Intent pickerIntent = getIntent().getParcelableExtra("picker_intent");
		int requestId = getIntent().getIntExtra("request_id", -1);

		Log.i(TAG, "DocumentResultProxy: Setting up activity launch, requestId = " + requestId + (savedInstanceState == null ? " (new)" : " (recreated)"));

		ActivityResultLauncher<Intent> launcher = registerForActivityResult(
			new ActivityResultContracts.StartActivityForResult(),
			result -> {
				Log.i(TAG, "DocumentResultProxy: Packing return intent, requestId = " + requestId);

				String resultPath = null;
				if (result.getResultCode() == Activity.RESULT_OK && result.getData() != null) {
					Intent data = result.getData();
					Uri uri = data.getData();
					if (uri == null && data.getClipData() != null && data.getClipData().getItemCount() > 0) {
						uri = data.getClipData().getItemAt(0).getUri();
					}

					if (uri != null) {
						Log.i(TAG, "DocumentResultProxy: Selected URI: " + uri);
						try {
							if (pickerIntent != null && Intent.ACTION_OPEN_DOCUMENT_TREE.equals(pickerIntent.getAction())) {
								getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
								DocumentFile documentFile = DocumentFile.fromTreeUri(this, uri);
								if (documentFile != null) {
									uri = documentFile.getUri();
									Log.i(TAG, "DocumentResultProxy: DocumentFile URI: " + uri);
								}
							} else {
								getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
							}
						} catch (Exception e) {
							Log.w(TAG, "DocumentResultProxy: Exception getting permissions or DocumentFile: " + e);
						}
						resultPath = uri.toString();
					} else {
						Log.w(TAG, "DocumentResultProxy: URI is null in result data");
					}
				}

				returnWithResult(result.getResultCode(), requestId, resultPath);
			}
		);

		// Only launch the picker if we are starting fresh. If we're being recreated (e.g. rotation),
		// the ActivityResultRegistry will handle delivering the pending result to the launcher
		// automatically, and we don't want to launch the picker a second time.
		if (savedInstanceState == null) {
			if (pickerIntent != null) {
				try {
					// throw new ActivityNotFoundException();  // Use this for testing the fallback.
					launcher.launch(pickerIntent);
				} catch (ActivityNotFoundException e) {
					NativeApp.reportException(e, "DocumentResultProxy: failed to launch picker intent, activity not found: " + pickerIntent.getAction());
					returnWithResult(NativeApp.RESULT_ERROR_ACTIVITY_NOT_FOUND, requestId, null);
				} catch (Exception e) {
					NativeApp.reportException(e, "DocumentResultProxy: failed to launch picker intent, other error: " + pickerIntent.getAction());
					returnWithResult(NativeApp.RESULT_ERROR_OTHER_ACTIVITY_ERROR, requestId, null);
				}
			} else {
				Log.e(TAG, "DocumentResultProxy: No picker intent provided");
				finish();
			}
		}
	}
}
