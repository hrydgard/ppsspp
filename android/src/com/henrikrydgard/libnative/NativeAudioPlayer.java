package com.henrikrydgard.libnative;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.util.Log;
import com.henrikrydgard.libnative.AudioFocusChangeListener;

public class NativeAudioPlayer {
	private String TAG = "NativeAudioPlayer";
	private Thread thread;
	private boolean playing_;
	
	// Calling stop() is allowed at any time, whether stopped or not.
	// If playing, blocks until not.
	public synchronized void stop() {
		if (thread != null) {
			waitUntilDone();
			
		} else {
			Log.e(TAG, "Was already stopped");
		}
	}
	
	// If not playing, make sure we're playing.
	public synchronized void play() {
		if (thread == null) {
			playStreaming();
		} else {
			Log.e(TAG, "Was already playing");
		}
	} 
	
	//keep this static so we can call this even if we don't
	//instantiate NativeAudioPlayer
	public static void gainAudioFocus(AudioManager audioManager, AudioFocusChangeListener focusChangeListener) {
		if (audioManager != null) {
			audioManager.requestAudioFocus(focusChangeListener,
					AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN);
		}
	}
	
	//keep this static so we can call this even if we don't
	//instantiate NativeAudioPlayer
	public static void loseAudioFocus(AudioManager audioManager,AudioFocusChangeListener focusChangeListener){
		if (audioManager != null) {
			audioManager.abandonAudioFocus(focusChangeListener);
		}
	}
	
	
	private void playStreaming() {
		playing_ = true;
		thread = new Thread(new Runnable() {
			public void run() {
				playThread();
			}
		});
		thread.setPriority(Thread.MAX_PRIORITY);
		thread.start();
	}
	
	private void waitUntilDone() {
		if (!playing_) {
			Log.i(TAG, "not playing.");
		}
		try {
			playing_ = false;
			Log.e(TAG, "waitUntilDone: Joining audio thread.");
			thread.join();
		} catch (InterruptedException e) {
			Log.e(TAG, "INterrupted!");
			e.printStackTrace();
		}
		thread = null;
		Log.i(TAG, "Finished waitUntilDone");
	}

	private void playThread() {
		try {
			Log.i(TAG, "Thread started.");
			// Get the smallest possible buffer size (which is annoyingly huge).
			int buffer_size_bytes = AudioTrack.getMinBufferSize(
				44100,
				AudioFormat.CHANNEL_OUT_STEREO,
				AudioFormat.ENCODING_PCM_16BIT);
			
			// Round buffer_size_bytes up to an even multiple of 128 for convenience.
			buffer_size_bytes = (buffer_size_bytes + 127) & ~127;

			AudioTrack audioTrack = new AudioTrack(AudioManager.STREAM_MUSIC,
					44100,
					AudioFormat.CHANNEL_OUT_STEREO,
					AudioFormat.ENCODING_PCM_16BIT,
					buffer_size_bytes,
					AudioTrack.MODE_STREAM);
			
			int buffer_size = buffer_size_bytes / 2;			
			short [] buffer = new short[buffer_size];
			audioTrack.play();
			Log.i(TAG, "Playing... minBuffersize = " + buffer_size);
			while (playing_) {
				int validShorts = NativeApp.audioRender(buffer);
				if (validShorts != 0) {
					audioTrack.write(buffer, 0, validShorts);
				}
				Thread.yield();
			}
			audioTrack.stop();
			audioTrack.release();
			Log.i(TAG, "Stopped playing.");
		} catch (Throwable t) {
			Log.e(TAG, "Playback Failed");
			t.printStackTrace();
			Log.e(TAG, t.toString());
		}	
	}
}