package org.ppsspp.ppsspp;

import android.annotation.TargetApi;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.PowerManager;

public class PowerSaveModeReceiver extends BroadcastReceiver {
	private boolean isPowerSaving = false;
	private boolean isBatteryLow = false;

	@Override
	public void onReceive(final Context context, final Intent intent) {
		if (Build.VERSION.SDK_INT >= 21) {
			isPowerSaving = getPowerSaving(context);
		} else {
			isPowerSaving = false;
		}

		final String action = intent.getAction();
		if (action.equals(Intent.ACTION_BATTERY_LOW)) {
			isBatteryLow = true;
		} else if (action.equals(Intent.ACTION_BATTERY_OKAY)) {
			isBatteryLow = false;
		}

		if (isBatteryLow || isPowerSaving) {
			NativeApp.sendMessage("core_powerSaving", "true");
		} else {
			NativeApp.sendMessage("core_powerSaving", "false");
		}
	}

	@TargetApi(21)
	private boolean getPowerSaving(final Context context) {
		final PowerManager pm = (PowerManager)context.getSystemService(Context.POWER_SERVICE);
		return pm.isPowerSaveMode();
	}
}
