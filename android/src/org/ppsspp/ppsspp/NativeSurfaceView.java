package org.ppsspp.ppsspp;

// Touch-enabled SurfaceView.
// Supports simple multitouch and pressure.
// Used by the Vulkan backend.

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.os.SystemClock;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceView;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

public class NativeSurfaceView extends SurfaceView {
	// When set, the view pretends to be a text editor so Android will really show the soft keyboard for it
	// (a plain SurfaceView is ignored by showSoftInput), and typed text is passed on to the native side.
	private boolean keyboardForced = false;
	// Bring the keyboard back by itself when the app regains focus (after an app switch).
	private boolean keyboardPersist = false;

	public NativeSurfaceView(Context context) {
		super(context);
	}

	@SuppressLint("ClickableViewAccessibility")
	@Override
	public boolean onTouchEvent(final MotionEvent ev) {
		if (ev.getAction() == MotionEvent.ACTION_UP) {
			super.performClick();
		}
		NativeApp.processTouchEvent(ev);
		return true;
	}

	// Must be called on the UI thread.
	public void setKeyboardForced(boolean forced, boolean persist) {
		keyboardForced = forced;
		keyboardPersist = persist;
		InputMethodManager imm = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
		if (imm == null) {
			return;
		}
		if (forced) {
			setFocusable(true);
			setFocusableInTouchMode(true);
			requestFocus();
			imm.restartInput(this);
			showKeyboardSoon();
		} else {
			imm.hideSoftInputFromWindow(getWindowToken(), 0);
			imm.restartInput(this);
			clearFocus();
			setFocusableInTouchMode(false);
			setFocusable(false);
		}
	}

	private void showKeyboardSoon() {
		post(() -> {
			InputMethodManager imm = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
			if (imm != null && keyboardForced) {
				imm.showSoftInput(this, 0);
			}
		});
	}

	// Android hides the keyboard when the window loses focus (app switch, dialogs). Bring it back.
	@Override
	public void onWindowFocusChanged(boolean hasWindowFocus) {
		super.onWindowFocusChanged(hasWindowFocus);
		if (hasWindowFocus && keyboardForced && keyboardPersist) {
			requestFocus();
			showKeyboardSoon();
		}
	}

	// Back is offered to the view before the keyboard sees it, but only while the keyboard is up. Tell the native side
	// so its keyboard toggle follows (the keyboard itself is hidden by the system as usual).
	@Override
	public boolean onKeyPreIme(int keyCode, KeyEvent event) {
		if (keyboardForced && keyCode == KeyEvent.KEYCODE_BACK && event.getAction() == KeyEvent.ACTION_UP) {
			NativeApp.sendMessageFromJava("softKeyboardHidden", "");
		}
		return super.onKeyPreIme(keyCode, event);
	}

	// Called from the activity's onStop. Without persistence, switching to another app ends the forced keyboard.
	public void onActivityStopped() {
		if (keyboardForced && !keyboardPersist) {
			setKeyboardForced(false, false);
			NativeApp.sendMessageFromJava("softKeyboardHidden", "");
		}
	}

	@Override
	public boolean onCheckIsTextEditor() {
		return keyboardForced;
	}

	@Override
	public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
		if (!keyboardForced) {
			return null;
		}
		// TYPE_NULL asks the keyboard for plain key events instead of treating this as a text field.
		outAttrs.inputType = InputType.TYPE_NULL;
		outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_ACTION_NONE;
		return new BaseInputConnection(this, false) {
			@Override
			public boolean commitText(CharSequence text, int newCursorPosition) {
				for (int i = 0; i < text.length(); ) {
					int cp = Character.codePointAt(text, i);
					i += Character.charCount(cp);
					if (cp == '\n') {
						sendKeyPress(KeyEvent.KEYCODE_ENTER);
					} else if (!Character.isISOControl(cp)) {
						NativeApp.keyChar(NativeApp.DEVICE_ID_KEYBOARD, cp);
					}
				}
				return true;
			}

			@Override
			public boolean deleteSurroundingText(int beforeLength, int afterLength) {
				for (int i = 0; i < beforeLength; i++) {
					sendKeyPress(KeyEvent.KEYCODE_DEL);
				}
				for (int i = 0; i < afterLength; i++) {
					sendKeyPress(KeyEvent.KEYCODE_FORWARD_DEL);
				}
				return true;
			}
		};
	}

	// Goes through the activity's normal key handling, same as a hardware keyboard would.
	private void sendKeyPress(int keyCode) {
		Context context = getContext();
		if (!(context instanceof Activity)) {
			return;
		}
		Activity activity = (Activity) context;
		long now = SystemClock.uptimeMillis();
		activity.dispatchKeyEvent(new KeyEvent(now, now, KeyEvent.ACTION_DOWN, keyCode, 0));
		activity.dispatchKeyEvent(new KeyEvent(now, now, KeyEvent.ACTION_UP, keyCode, 0));
	}
}
