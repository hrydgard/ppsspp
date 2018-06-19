package org.ppsspp.ppsspp;

import android.annotation.TargetApi;
import android.content.Context;
import android.graphics.ImageFormat;
import android.graphics.Rect;
import android.graphics.SurfaceTexture;
import android.graphics.YuvImage;
import android.hardware.Camera;
import android.util.Log;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.List;

@TargetApi(23)
@SuppressWarnings("deprecation")
class CameraHelper {
	private static final String TAG = "CameraHelper";
	private Camera mCamera = null;
	private int previewWidth = 0;
	private int previewHeight = 0;
	private long lastTime = 0;
	private SurfaceTexture mSurfaceTexture;

	private Camera.PreviewCallback mPreviewCallback = new Camera.PreviewCallback() {
		@Override
		public void onPreviewFrame(byte[] previewData, Camera camera) {
			// throttle at 100 ms
			long currentTime = System.currentTimeMillis();
			if (currentTime - lastTime < 100) {
				return;
			}
			lastTime = currentTime;

			YuvImage yuvImage = new YuvImage(previewData, ImageFormat.NV21, previewWidth, previewHeight, null);
			ByteArrayOutputStream baos = new ByteArrayOutputStream();

			// the expected values arrives in sceUsbCamSetupVideo
			int targetW = 480;
			int targetH = 272;

			// crop to expected size and convert to Jpeg
			Rect rect = new Rect(
				(previewWidth - targetW) / 2,
				(previewHeight - targetH) / 2,
				previewWidth - (previewWidth - targetW) / 2,
				previewHeight - (previewHeight - targetH) / 2
			);
			yuvImage.compressToJpeg(rect, 80, baos);
			NativeApp.pushCameraImage(baos.toByteArray());
		}
	};

	@SuppressWarnings("unused")
	CameraHelper(Context context) {
		mSurfaceTexture = new SurfaceTexture(10);
	}

	void startCamera() {
		Log.d(TAG, "startCamera");
		try {
			mCamera = Camera.open();
			Camera.Parameters param = mCamera.getParameters();

			// Set preview size
			List<Camera.Size> previewSizes = param.getSupportedPreviewSizes();
			previewWidth = previewSizes.get(0).width;
			previewHeight = previewSizes.get(0).height;
			for (int i = 0; i < previewSizes.size(); i++) {
				Log.d(TAG, "getSupportedPreviewSizes[" + i + "]: " + previewSizes.get(i).height + " " + previewSizes.get(i).width);
				if (previewSizes.get(i).width <= 640 && previewSizes.get(i).height <= 480) {
					previewWidth = previewSizes.get(i).width;
					previewHeight = previewSizes.get(i).height;
					break;
				}
			}
			Log.d(TAG, "setPreviewSize(" + previewWidth + ", " + previewHeight + ")");
			param.setPreviewSize(previewWidth, previewHeight);

			// Set preview FPS
			int[] fps;
			List<int[]> previewFps = param.getSupportedPreviewFpsRange();
			fps = previewFps.get(0);
			for (int i = 0; i < previewFps.size(); i++) {
				Log.d(TAG, "getSupportedPreviewFpsRange[" + i + "]: " + previewFps.get(i)[0] + " " + previewFps.get(i)[1]);
				if (previewFps.get(i)[0] <= fps[0] && previewFps.get(i)[1] <= fps[1]) {
					fps = previewFps.get(i);
				}
			}
			Log.d(TAG, "setPreviewFpsRange(" + fps[0] + ", " + fps[1] + ")");
			param.setPreviewFpsRange(fps[0], fps[1]);

			mCamera.setParameters(param);
			mCamera.setPreviewTexture(mSurfaceTexture);
			mCamera.setPreviewCallback(mPreviewCallback);
			mCamera.startPreview();
		} catch (IOException e) {
			Log.e(TAG, "Cannot start camera: " + e.toString());
		}
	}

	void stopCamera() {
		Log.d(TAG, "stopCamera");
		if (mCamera != null) {
			mCamera.setPreviewCallback(null);
			mCamera.stopPreview();
			mCamera.release();
			mCamera = null;
		}
	}
}
