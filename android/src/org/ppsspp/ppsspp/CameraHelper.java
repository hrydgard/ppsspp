package org.ppsspp.ppsspp;

import android.annotation.TargetApi;
import android.content.Context;
import android.graphics.ImageFormat;
import android.graphics.Rect;
import android.graphics.SurfaceTexture;
import android.graphics.YuvImage;
import android.hardware.Camera;
import android.os.SystemClock;
import android.util.Log;
import android.view.Display;
import android.view.Surface;
import android.view.WindowManager;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.List;

@TargetApi(23)
@SuppressWarnings("deprecation")
class CameraHelper {
	private static final String TAG = CameraHelper.class.getSimpleName();
	private Display mDisplay;
	private Camera mCamera = null;
	private boolean mIsCameraRunning = false;
	private int mCameraOrientation = 0;
	private Camera.Size mPreviewSize = null;
	private long mLastFrameTime = 0;
	private SurfaceTexture mSurfaceTexture;

	private int getCameraRotation() {
		int displayRotation = mDisplay.getRotation();
		int displayDegrees = 0;
		switch (displayRotation) {
			case Surface.ROTATION_0:   displayDegrees =   0; break;
			case Surface.ROTATION_90:  displayDegrees =  90; break;
			case Surface.ROTATION_180: displayDegrees = 180; break;
			case Surface.ROTATION_270: displayDegrees = 270; break;
		}
		return (mCameraOrientation - displayDegrees + 360) % 360;
	}

	static byte[] rotateNV21(final byte[] input, final int inWidth, final int inHeight,
							 final int outWidth, final int outHeight, final int rotation) {

		final int inFrameSize = inWidth * inHeight;
		final int outFrameSize = outWidth * outHeight;
		final byte[] output = new byte[outFrameSize + outFrameSize/2];

		if (rotation == 0 || rotation == 180) {
			final int crop_left = (inWidth - outWidth) / 2;
			final int crop_top = (inHeight - outHeight) / 2;
			for (int j = 0; j < outHeight; j++) {
				final int yInCol = (crop_top + j) * inWidth + crop_left;
				final int uvInCol = inFrameSize + ((crop_top + j) >> 1) * inWidth + crop_left;
				final int jOut = rotation == 180 ? outHeight - j - 1 : j;
				final int yOutCol = jOut * outWidth;
				final int uvOutCol = outFrameSize + (jOut >> 1) * outWidth;
				for (int i = 0; i < outWidth; i++) {
					final int yIn = yInCol + i;
					final int uIn = uvInCol + (i & ~1);
					final int vIn = uIn + 1;
					final int iOut = rotation == 180 ? outWidth - i - 1 : i;
					final int yOut = yOutCol + iOut;
					final int uOut = uvOutCol + (iOut & ~1);
					final int vOut = uOut + 1;
					output[yOut] = input[yIn];
					output[uOut] = input[uIn];
					output[vOut] = input[vIn];
				}
			}
		} else if (rotation == 90 || rotation == 270) {
			int crop_left = (inWidth - outHeight) / 2;
			int crop_top  = (inHeight - outWidth) / 2;
			for (int j = 0; j < outWidth; j++) {
				final int yInCol = (crop_top + j) * inWidth + crop_left;
				final int uvInCol = inFrameSize + ((crop_top + j) >> 1) * inWidth + crop_left;
				final int iOut = rotation == 90 ? outWidth - j - 1 : j;
				for (int i = 0; i < outHeight; i++) {
					final int yIn = yInCol + i;
					final int uIn = uvInCol + (i & ~1);
					final int vIn = uIn + 1;
					final int jOut = rotation == 270 ? outHeight - i - 1 : i;
					final int yOut = jOut * outWidth + iOut;
					final int uOut = outFrameSize + (jOut >> 1) * outWidth + (iOut & ~1);
					final int vOut = uOut + 1;
					output[yOut] = input[yIn];
					output[uOut] = input[uIn];
					output[vOut] = input[vIn];
				}
			}
		}
		return output;
	}

	private Camera.PreviewCallback mPreviewCallback = new Camera.PreviewCallback() {
		@Override
		public void onPreviewFrame(byte[] previewData, Camera camera) {
			// throttle at 66 ms
			long currentTime = SystemClock.elapsedRealtime();
			if (currentTime - mLastFrameTime < 66) {
				return;
			}
			mLastFrameTime = currentTime;

			// the expected values arrives in sceUsbCamSetupVideo
			int targetW = 480;
			int targetH = 272;

			int cameraRotation = getCameraRotation();
			byte[] newPreviewData = rotateNV21(previewData, mPreviewSize.width, mPreviewSize.height,
					targetW, targetH, cameraRotation);
			YuvImage yuvImage = new YuvImage(newPreviewData, ImageFormat.NV21, targetW, targetH, null);
			ByteArrayOutputStream baos = new ByteArrayOutputStream();

			// convert to Jpeg
			Rect crop = new Rect(0, 0, targetW, targetH);
			yuvImage.compressToJpeg(crop, 80, baos);
			NativeApp.pushCameraImage(baos.toByteArray());
			try {
				baos.close();
			} catch (IOException e) {
				e.printStackTrace();
			}
		}
	};

	@SuppressWarnings("unused")
	CameraHelper(final Context context) {
		mDisplay = ((WindowManager)context.getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();
		mSurfaceTexture = new SurfaceTexture(10);
	}

	void startCamera() {
		Log.d(TAG, "startCamera");
		try {
			Camera.CameraInfo info = new android.hardware.Camera.CameraInfo();
			Camera.getCameraInfo(0, info);
			mCameraOrientation = info.orientation;

			mCamera = Camera.open();
			Camera.Parameters param = mCamera.getParameters();

			// Set preview size
			List<Camera.Size> previewSizes = param.getSupportedPreviewSizes();
			mPreviewSize = previewSizes.get(0);
			for (int i = 0; i < previewSizes.size(); i++) {
				Log.d(TAG, "getSupportedPreviewSizes[" + i + "]: " + previewSizes.get(i).height + " " + previewSizes.get(i).width);
				if (previewSizes.get(i).width <= 640 && previewSizes.get(i).height <= 480) {
					mPreviewSize = previewSizes.get(i);
					break;
				}
			}
			Log.d(TAG, "setPreviewSize(" + mPreviewSize.width + ", " + mPreviewSize.height + ")");
			param.setPreviewSize(mPreviewSize.width, mPreviewSize.height);

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
			mIsCameraRunning = true;
		} catch (IOException e) {
			Log.e(TAG, "Cannot start camera: " + e.toString());
		}
	}

	void pause() {
		if (mIsCameraRunning && mCamera != null) {
			Log.d(TAG, "pause");
			mCamera.setPreviewCallback(null);
			mCamera.stopPreview();
			mCamera.release();
			mCamera = null;
		}
	}

	void resume() {
		if (mIsCameraRunning) {
			Log.d(TAG, "resume");
			startCamera();
		}
	}

	void stopCamera() {
		pause();
		mIsCameraRunning = false;
	}
}
