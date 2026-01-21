package org.ppsspp.ppsspp;

import android.content.Context;
import android.hardware.ConsumerIrManager;
import android.hardware.ConsumerIrManager.CarrierFrequencyRange;
import android.os.Build;
import android.util.Log;

import androidx.annotation.RequiresApi;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

class InfraredHelper {
	private static final String TAG = InfraredHelper.class.getSimpleName();
	private static final int SIRC_FREQ = 40000;
	private final ConsumerIrManager mConsumerIrManager;

	InfraredHelper(Context context) throws Exception {
		mConsumerIrManager = (ConsumerIrManager) context.getSystemService(Context.CONSUMER_IR_SERVICE);
		Log.d(TAG, "HasIrEmitter: " + mConsumerIrManager.hasIrEmitter());
		if (!mConsumerIrManager.hasIrEmitter()) {
			throw new Exception("No Ir Emitter");
		}
		boolean sirc_freq_supported = false;
		CarrierFrequencyRange[] carrierFrequencies = mConsumerIrManager.getCarrierFrequencies();
		for (CarrierFrequencyRange freq : carrierFrequencies) {
			Log.d(TAG, "CarrierFrequencies: " + freq.getMinFrequency() + " -> " + freq.getMaxFrequency());
			if (freq.getMinFrequency() <= SIRC_FREQ && SIRC_FREQ <= freq.getMaxFrequency()) {
				sirc_freq_supported = true;
			}
		}
		if (!sirc_freq_supported) {
			throw new Exception("Sirc Frequency unsupported");
		}
	}

	void sendSircCommand(int version, int command, int address, int count) {
		final List<Integer> start = Arrays.asList(2400, 600);
		final List<Integer> one   = Arrays.asList(1200, 600);
		final List<Integer> zero  = Arrays.asList( 600, 600);

		List<Integer> iterList = new ArrayList<>(start);

		for (int i = 0; i < version; i++) {
			List<Integer> val = i < 7
				? ((command >> i    ) & 1) == 1 ? one : zero
				: ((address >> i - 7) & 1) == 1 ? one : zero;
			iterList.addAll(val);
		}

		int iterSum = 0;
		for (int i = 0; i < iterList.size() - 1; i++) {
			iterSum += iterList.get(i);
		}
		int lastVal = 52000 - iterSum; // SIRC cycle = 52ms
		iterList.set(iterList.size() - 1, lastVal);

		List<Integer> patternList = new ArrayList<>();
		// Android is limited to 2 seconds => max 38 loops of 52ms each
		// Limit even further to 4 loops for now
		for (int i = 0; i < count && i < 4; i++) {
			patternList.addAll(iterList);
		}

		int[] pattern = new int[patternList.size()];
		for (int i = 0; i < patternList.size(); i++) {
			pattern[i] = patternList.get(i);
		}
		mConsumerIrManager.transmit(SIRC_FREQ, pattern);
	}
}
