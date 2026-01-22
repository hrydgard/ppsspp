package org.ppsspp.ppsspp;

import androidx.annotation.NonNull;
import android.util.Log;

// Simple class to validate the app lifecycle progression.
// Useful to debug using logs.
public class LifeCycle {
	private final String TAG = "PPSSPPNativeActivity";

	public enum State {
		LAUNCHED,
		CREATED,
		STARTED,
		RUNNING;

		@NonNull
		@Override
		public String toString() {
			// Optional: return a custom string if needed
			return name(); // "LAUNCHED", "CREATED", etc.
		}
	}

	private State state = State.LAUNCHED;

	public boolean onCreate() {
		if (state != State.LAUNCHED) {
			Log.e(TAG, "Lifecycle: onCreate, but state is " + state + ". Expected LAUNCHED");
			return false;
		}
		Log.i(TAG, "onCreate begin");
		state = State.CREATED;
		return true;
	}

	public boolean onStart() {
		if (state != State.CREATED) {
			Log.e(TAG, "Lifecycle: onStart, but state is " + state + ". Expected CREATED");
			return false;
		}
		Log.i(TAG, "onStart begin");
		state = State.STARTED;
		return true;
	}

	public boolean onResume() {
		if (state != State.STARTED) {
			Log.e(TAG, "Lifecycle: onResume, but state is " + state + ". Expected STARTED");
			return false;
		}
		state = State.RUNNING;
		Log.i(TAG, "onResume begin");
		return true;
	}

	public boolean onPause() {
		if (state != State.RUNNING) {
			Log.e(TAG, "Lifecycle: onPause, but state is " + state + ". Expected RUNNING");
			return false;
		}
		state = State.STARTED;
		Log.i(TAG, "onPause begin");
		return true;
	}

	public boolean onStop() {
		if (state != State.STARTED) {
			Log.e(TAG, "Lifecycle: onStop, but state is " + state + ". Expected STARTED");
			return false;
		}
		state = State.CREATED;
		Log.i(TAG, "onStop begin");
		return true;
	}

	public boolean onDestroy() {
		if (state != State.CREATED) {
			Log.e(TAG, "Lifecycle: onDestroy, but state is " + state + ". Expected CREATED");
			return false;
		}
		state = State.LAUNCHED;
		Log.i(TAG, "onDestroy begin");
		return true;
	}
}
