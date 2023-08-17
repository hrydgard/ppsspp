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
import java.util.ArrayList;
import java.util.List;

@TargetApi(23)
@SuppressWarnings("deprecation")
class CameraHelper {
	private static final String TAG = CameraHelper.class.getSimpleName();
	private Display mDisplay;
	private int mTargetWidth = 0;
	private int mTargetHeight = 0;
	private Camera mCamera = null;
	private boolean mIsCameraRunning = false;
	private int mCameraFacing = 0;
	private int mCameraOrientation = 0;
	private Camera.Size mPreviewSize = null;
	private long mLastFrameTime = 0;
	private SurfaceTexture mSurfaceTexture;

	private static boolean firstRotation = true;

	private int getCameraRotation() {
		int displayRotation = mDisplay.getRotation();
		int displayDegrees = 0;
		switch (displayRotation) {
			case Surface.ROTATION_0:   displayDegrees =   0; break;
			case Surface.ROTATION_90:  displayDegrees =  90; break;
			case Surface.ROTATION_180: displayDegrees = 180; break;
			case Surface.ROTATION_270: displayDegrees = 270; break;
		}
		if (mCameraFacing == Camera.CameraInfo.CAMERA_FACING_FRONT) {
			return (mCameraOrientation + displayDegrees) % 360;
		} else {
			return (mCameraOrientation - displayDegrees + 360) % 360;
		}
	}

	// Does not work if the source is smaller than the destination!
	static byte[] rotateNV21(final byte[] input, final int inWidth, final int inHeight,
							 final int outWidth, final int outHeight, final int rotation) {
		if (firstRotation) {
			Log.i(TAG, "rotateNV21: in: " + inWidth + "x" + inHeight + " out: " + outWidth + "x" + outHeight + " rotation: " + rotation);
			firstRotation = false;
		}

		final int inFrameSize = inWidth * inHeight;
		final int outFrameSize = outWidth * outHeight;
		final byte[] output = new byte[outFrameSize + outFrameSize/2];

		if (rotation == 0 || rotation == 180) {
			final int crop_left = (inWidth - outWidth) / 2;
			final int crop_top = (inHeight - outHeight) / 2;

			if (crop_left < 0 || crop_top < 0) {
				// Math will fail. Return a black image.
				return output;
			}

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
			final int crop_left = (inWidth - outHeight) / 2;
			final int crop_top  = (inHeight - outWidth) / 2;

			if (crop_left < 0 || crop_top < 0) {
				// Math will fail. Return a black image.
				return output;
			}

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
		} else {
			Log.e(TAG, "Unknown rotation " + rotation);
		}
		return output;
	}

	private Camera.PreviewCallback mPreviewCallback = new Camera.PreviewCallback() {
		@Override
		public void onPreviewFrame(byte[] previewData, Camera camera) {
			// throttle at 16 ms
			long currentTime = SystemClock.elapsedRealtime();
			if (currentTime - mLastFrameTime < 16) {
				return;
			}
			mLastFrameTime = currentTime;

			int cameraRotation = getCameraRotation();
			byte[] newPreviewData = rotateNV21(previewData, mPreviewSize.width, mPreviewSize.height,
					mTargetWidth, mTargetHeight, cameraRotation);
			YuvImage yuvImage = new YuvImage(newPreviewData, ImageFormat.NV21, mTargetWidth, mTargetHeight, null);
			ByteArrayOutputStream baos = new ByteArrayOutputStream();

			// convert to Jpeg
			Rect crop = new Rect(0, 0, mTargetWidth, mTargetHeight);
			yuvImage.compressToJpeg(crop, 80, baos);
			NativeApp.pushCameraImageAndroid(baos.toByteArray());
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

	static ArrayList<String> getDeviceList() {
		ArrayList<String> deviceList = new ArrayList<>();
		if (NativeActivity.isVRDevice()) {
			return deviceList;
		}
		int nrCam = Camera.getNumberOfCameras();
		for (int index = 0; index < nrCam; index++) {
			try {
				Camera.CameraInfo info = new Camera.CameraInfo();
				Camera.getCameraInfo(index, info);
				String devName = index + ":" + (info.facing == Camera.CameraInfo.CAMERA_FACING_BACK ? "Back Camera" : "Front Camera");
				deviceList.add(devName);
			} catch (Exception e) {
				Log.e(TAG, "Failed to get camera info: " + e.toString());
			}
		}
		return deviceList;
	}

	void setCameraSize(int width, int height) {
		mTargetWidth = width;
		mTargetHeight = height;
	}

	void startCamera() {
		try {
			int cameraId = NativeApp.getSelectedCamera();
			Log.d(TAG, "startCamera [id=" + cameraId + ", res=" + mTargetWidth + "x" + mTargetHeight + "]");

			Camera.CameraInfo info = new Camera.CameraInfo();
			Camera.getCameraInfo(cameraId, info);
			mCameraFacing = info.facing;
			mCameraOrientation = info.orientation;

			mCamera = Camera.open(cameraId);
			Camera.Parameters param = mCamera.getParameters();

			// Set preview size
			List<Camera.Size> previewSizes = param.getSupportedPreviewSizes();
			mPreviewSize = null;

			// Find the preview size that's the closest above or equal the requested size.
			// We can not depend on the ordering of the incoming sizes.
			for (int i = 0; i < previewSizes.size(); i++) {
				int width = previewSizes.get(i).width;
				int height = previewSizes.get(i).height;
				Log.d(TAG, "getSupportedPreviewSizes[" + i + "]: " + width + "x" + height);

				// Reject too small preview sizes.
				if (width < mTargetWidth || height < mTargetHeight) {
					continue;
				}

				if (mPreviewSize == null) {
					Log.i(TAG, "Selected first viable preview size: " + width + "x" + height);
					mPreviewSize = previewSizes.get(i);
				} else if (width < mPreviewSize.width || height < mPreviewSize.height) {
					// Only select the new size if it's smaller.
					Log.i(TAG, "Selected better viable preview size: " + width + "x" + height);
					mPreviewSize = previewSizes.get(i);
				}
			}

			if (mPreviewSize == null) {
				throw new Exception("Couldn't find a viable preview size");
			}

			Log.i(TAG, "setPreviewSize(" + mPreviewSize.width + ", " + mPreviewSize.height + ")");
			param.setPreviewSize(mPreviewSize.width, mPreviewSize.height);

			// Set preview FPS
			List<int[]> previewFps = param.getSupportedPreviewFpsRange();

			int idealRate = 30000;

			int bestIndex = -1;
			int bestDistance = 0;  // bestIndex is -1 so irrelevant what the initial value is here.

			for (int i = 0; i < previewFps.size(); i++) {
				int rangeStart = previewFps.get(i)[0];
				int rangeEnd = previewFps.get(i)[1];
				int distance = Integer.max(Math.abs(rangeStart - idealRate), Math.abs(rangeEnd - idealRate));

				if (bestIndex == -1 || distance < bestDistance) {
					bestDistance = distance;
					bestIndex = i;
				}
				Log.d(TAG, "getSupportedPreviewFpsRange[" + i + "]: " + previewFps.get(i)[0] + " " + previewFps.get(i)[1]);
			}

			if (bestIndex == -1) {
				// This is pretty much impossible.
				throw new Exception("Couldn't find a viable preview FPS");
			}

			int[] fps = previewFps.get(bestIndex);
			Log.d(TAG, "setPreviewFpsRange(" + fps[0] + ", " + fps[1] + ")");
			param.setPreviewFpsRange(fps[0], fps[1]);

			mCamera.setParameters(param);
			mCamera.setPreviewTexture(mSurfaceTexture);
			mCamera.setPreviewCallback(mPreviewCallback);
			mCamera.startPreview();
			mIsCameraRunning = true;
		} catch (Exception e) {
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
