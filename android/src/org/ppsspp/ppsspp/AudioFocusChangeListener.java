package org.ppsspp.ppsspp;

import android.media.AudioManager;
import android.media.AudioManager.OnAudioFocusChangeListener;

public class AudioFocusChangeListener implements OnAudioFocusChangeListener {
	// not used right now, but we may need to use it sometime. So just store it
	// for now.
	private boolean hasAudioFocus = false;

	@Override
	public void onAudioFocusChange(int focusChange) {
		switch (focusChange) {
		case AudioManager.AUDIOFOCUS_GAIN:
			hasAudioFocus = true;
			break;

		case AudioManager.AUDIOFOCUS_LOSS:
			hasAudioFocus = false;
			break;
		}
	}

	public boolean hasAudioFocus() {
		return hasAudioFocus;
	}
}
