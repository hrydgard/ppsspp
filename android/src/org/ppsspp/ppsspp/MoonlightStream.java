package org.ppsspp.ppsspp;

import android.app.Activity;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.Log;
import android.util.SparseIntArray;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;

import io.github.rasulnurmamedow.moonlighthost.GamepadState;
import io.github.rasulnurmamedow.moonlighthost.InputListener;
import io.github.rasulnurmamedow.moonlighthost.MoonlightHost;
import io.github.rasulnurmamedow.moonlighthost.SurfaceListener;

// Makes PPSSPP a Moonlight (GameStream) host, through moonlight-host-android (ext/moonlight-host-android).
// While a client watches, the game goes to it as a Game output, like on a TV on HDMI (see
// ExternalDisplayPresentation), its sound goes to the stream instead of the speaker, and the client's
// controllers and keyboard come back as input. The phone keeps the touch controls and the menus.
public class MoonlightStream implements SurfaceListener, InputListener {
	private static final String TAG = "PPSSPPMoonlight";
	private static final int MAX_PADS = 16;
	// Must be at least the size of the native ring, see readStreamAudio.
	private static final int AUDIO_BUFFER_SAMPLES = 8192;

	private final MoonlightHost host;
	// Input comes in on a network thread. It's handled on the UI thread, like the device's own input.
	private final Handler uiHandler = new Handler(Looper.getMainLooper());
	private int outputId = 0;
	private Thread audioThread;
	private volatile boolean audioRunning;
	private final int[] padButtons = new int[MAX_PADS];

	private static final int[] PAD_BUTTONS = {
		GamepadState.A, KeyEvent.KEYCODE_BUTTON_A,
		GamepadState.B, KeyEvent.KEYCODE_BUTTON_B,
		GamepadState.X, KeyEvent.KEYCODE_BUTTON_X,
		GamepadState.Y, KeyEvent.KEYCODE_BUTTON_Y,
		GamepadState.LB, KeyEvent.KEYCODE_BUTTON_L1,
		GamepadState.RB, KeyEvent.KEYCODE_BUTTON_R1,
		GamepadState.START, KeyEvent.KEYCODE_BUTTON_START,
		GamepadState.BACK, KeyEvent.KEYCODE_BUTTON_SELECT,
		GamepadState.GUIDE, KeyEvent.KEYCODE_BUTTON_MODE,
		GamepadState.LS_CLICK, KeyEvent.KEYCODE_BUTTON_THUMBL,
		GamepadState.RS_CLICK, KeyEvent.KEYCODE_BUTTON_THUMBR,
		GamepadState.UP, KeyEvent.KEYCODE_DPAD_UP,
		GamepadState.DOWN, KeyEvent.KEYCODE_DPAD_DOWN,
		GamepadState.LEFT, KeyEvent.KEYCODE_DPAD_LEFT,
		GamepadState.RIGHT, KeyEvent.KEYCODE_DPAD_RIGHT,
	};

	private static final int[] PAD_AXES = {
		MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ,
		MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_RTRIGGER,
	};

	// Moonlight sends Windows virtual-key codes.
	private static final SparseIntArray VK_TO_KEYCODE = new SparseIntArray();
	static {
		for (int i = 0; i < 26; i++) {
			VK_TO_KEYCODE.put(0x41 + i, KeyEvent.KEYCODE_A + i);
		}
		for (int i = 0; i < 10; i++) {
			VK_TO_KEYCODE.put(0x30 + i, KeyEvent.KEYCODE_0 + i);
			VK_TO_KEYCODE.put(0x60 + i, KeyEvent.KEYCODE_NUMPAD_0 + i);
		}
		for (int i = 0; i < 12; i++) {
			VK_TO_KEYCODE.put(0x70 + i, KeyEvent.KEYCODE_F1 + i);
		}
		int[] pairs = {
			0x08, KeyEvent.KEYCODE_DEL,
			0x09, KeyEvent.KEYCODE_TAB,
			0x0D, KeyEvent.KEYCODE_ENTER,
			0x10, KeyEvent.KEYCODE_SHIFT_LEFT,
			0x11, KeyEvent.KEYCODE_CTRL_LEFT,
			0x12, KeyEvent.KEYCODE_ALT_LEFT,
			0x1B, KeyEvent.KEYCODE_ESCAPE,
			0x20, KeyEvent.KEYCODE_SPACE,
			0x21, KeyEvent.KEYCODE_PAGE_UP,
			0x22, KeyEvent.KEYCODE_PAGE_DOWN,
			0x23, KeyEvent.KEYCODE_MOVE_END,
			0x24, KeyEvent.KEYCODE_MOVE_HOME,
			0x25, KeyEvent.KEYCODE_DPAD_LEFT,
			0x26, KeyEvent.KEYCODE_DPAD_UP,
			0x27, KeyEvent.KEYCODE_DPAD_RIGHT,
			0x28, KeyEvent.KEYCODE_DPAD_DOWN,
			0x2D, KeyEvent.KEYCODE_INSERT,
			0x2E, KeyEvent.KEYCODE_FORWARD_DEL,
			0xA0, KeyEvent.KEYCODE_SHIFT_LEFT,
			0xA1, KeyEvent.KEYCODE_SHIFT_RIGHT,
			0xA2, KeyEvent.KEYCODE_CTRL_LEFT,
			0xA3, KeyEvent.KEYCODE_CTRL_RIGHT,
			0xA4, KeyEvent.KEYCODE_ALT_LEFT,
			0xA5, KeyEvent.KEYCODE_ALT_RIGHT,
			0xBA, KeyEvent.KEYCODE_SEMICOLON,
			0xBB, KeyEvent.KEYCODE_EQUALS,
			0xBC, KeyEvent.KEYCODE_COMMA,
			0xBD, KeyEvent.KEYCODE_MINUS,
			0xBE, KeyEvent.KEYCODE_PERIOD,
			0xBF, KeyEvent.KEYCODE_SLASH,
			0xC0, KeyEvent.KEYCODE_GRAVE,
			0xDB, KeyEvent.KEYCODE_LEFT_BRACKET,
			0xDC, KeyEvent.KEYCODE_BACKSLASH,
			0xDD, KeyEvent.KEYCODE_RIGHT_BRACKET,
			0xDE, KeyEvent.KEYCODE_APOSTROPHE,
		};
		for (int i = 0; i < pairs.length; i += 2) {
			VK_TO_KEYCODE.put(pairs[i], pairs[i + 1]);
		}
	}

	private MoonlightStream(Activity activity) {
		String deviceName = null;
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N_MR1) {
			deviceName = Settings.Global.getString(activity.getContentResolver(), Settings.Global.DEVICE_NAME);
		}
		if (deviceName == null || deviceName.isEmpty()) {
			deviceName = Build.MODEL;
		}
		host = new MoonlightHost.Builder(activity)
			.setHostName("PPSSPP on " + deviceName)
			.setAppTitle("PPSSPP")
			.setSurfaceListener(this)
			.setInputListener(this)
			.build();
	}

	// Starts listening for clients, and announces the host on the network. Null if that fails. UI thread.
	public static MoonlightStream start(Activity activity) {
		MoonlightStream stream = new MoonlightStream(activity);
		try {
			stream.host.start();
		} catch (Exception e) {
			Log.e(TAG, "Couldn't start the Moonlight host: " + e);
			stream.host.stop();
			return null;
		}
		return stream;
	}

	// Ends the stream, if a client is watching, and stops the host. UI thread, before the render thread stops.
	public void stop() {
		host.stop();
	}

	// A client is watching: render the game into the stream's encoder, and send it the sound.
	@Override
	public void onSurfaceCreated(Surface surface, int width, int height) {
		outputId = NativeApp.addOutput(surface, width, height);
		Log.i(TAG, "Streaming " + width + "x" + height + ", output " + outputId);
		NativeApp.setStreamAudio(true);
		audioRunning = true;
		audioThread = new Thread(this::audioLoop, "MoonlightAudio");
		audioThread.start();
	}

	@Override
	public void onSurfaceDestroyed(Surface surface) {
		audioRunning = false;
		try {
			audioThread.join();
		} catch (InterruptedException e) {
			Thread.currentThread().interrupt();
		}
		NativeApp.setStreamAudio(false);
		if (outputId != 0) {
			NativeApp.removeOutput(outputId);
			outputId = 0;
		}
		releaseAllPads();
		Log.i(TAG, "Stream ended");
	}

	// The audio callback mixes into a ring for us (see AndroidMix). Moves it on, as little-endian PCM.
	private void audioLoop() {
		short[] samples = new short[AUDIO_BUFFER_SAMPLES];
		byte[] pcm = new byte[AUDIO_BUFFER_SAMPLES * 2];
		while (audioRunning) {
			int count = NativeApp.readStreamAudio(samples);
			if (count == 0) {
				try {
					Thread.sleep(2);
				} catch (InterruptedException e) {
					return;
				}
				continue;
			}
			for (int i = 0; i < count; i++) {
				pcm[i * 2] = (byte)samples[i];
				pcm[i * 2 + 1] = (byte)(samples[i] >> 8);
			}
			host.pushAudio(pcm, count * 2);
		}
	}

	@Override
	public void onKey(int windowsVk, boolean down, int modifiers) {
		int keyCode = VK_TO_KEYCODE.get(windowsVk, 0);
		if (keyCode == 0) {
			return;
		}
		uiHandler.post(() -> {
			if (down) {
				NativeApp.keyDown(NativeApp.DEVICE_ID_KEYBOARD, keyCode, false);
			} else {
				NativeApp.keyUp(NativeApp.DEVICE_ID_KEYBOARD, keyCode);
			}
		});
	}

	@Override
	public void onGamepad(GamepadState state) {
		if (state.index < 0 || state.index >= MAX_PADS) {
			return;
		}
		// The state object is reused, so take what we need now.
		final int index = state.index;
		final int buttons = state.buttons;
		// XInput sticks have Y pointing up, Android's point down.
		final float[] values = {
			stick(state.leftStickX), -stick(state.leftStickY),
			stick(state.rightStickX), -stick(state.rightStickY),
			state.leftTrigger / 255.0f, state.rightTrigger / 255.0f,
		};
		uiHandler.post(() -> {
			updatePadButtons(index, buttons);
			NativeApp.joystickAxis(NativeApp.DEVICE_ID_PAD_0, PAD_AXES, values, PAD_AXES.length);
		});
	}

	@Override
	public void onGamepadRemoved(int index) {
		if (index < 0 || index >= MAX_PADS) {
			return;
		}
		uiHandler.post(() -> releasePad(index));
	}

	private static float stick(short value) {
		return Math.max(-1.0f, value / 32767.0f);
	}

	// UI thread.
	private void updatePadButtons(int index, int buttons) {
		int changed = buttons ^ padButtons[index];
		padButtons[index] = buttons;
		for (int i = 0; i < PAD_BUTTONS.length; i += 2) {
			if ((changed & PAD_BUTTONS[i]) == 0) {
				continue;
			}
			if ((buttons & PAD_BUTTONS[i]) != 0) {
				NativeApp.keyDown(NativeApp.DEVICE_ID_PAD_0, PAD_BUTTONS[i + 1], false);
			} else {
				NativeApp.keyUp(NativeApp.DEVICE_ID_PAD_0, PAD_BUTTONS[i + 1]);
			}
		}
	}

	// UI thread.
	private void releasePad(int index) {
		updatePadButtons(index, 0);
		NativeApp.joystickAxis(NativeApp.DEVICE_ID_PAD_0, PAD_AXES, new float[PAD_AXES.length], PAD_AXES.length);
	}

	// UI thread.
	private void releaseAllPads() {
		for (int i = 0; i < MAX_PADS; i++) {
			updatePadButtons(i, 0);
		}
		NativeApp.joystickAxis(NativeApp.DEVICE_ID_PAD_0, PAD_AXES, new float[PAD_AXES.length], PAD_AXES.length);
	}
}
