package org.ppsspp.ppsspp;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.database.ContentObserver;
import android.net.Uri;
import android.os.PowerManager;
import android.provider.Settings;
import android.util.Log;

public class PowerSaveModeReceiver extends BroadcastReceiver {
	private static final String TAG = PowerSaveModeReceiver.class.getSimpleName();
	private static boolean isBatteryLow = false;

	@Override
	public void onReceive(final Context context, final Intent intent) {
		final String action = intent.getAction();
		if (Intent.ACTION_BATTERY_LOW.equals(action)) {
			isBatteryLow = true;
		} else if (Intent.ACTION_BATTERY_OKAY.equals(action)) {
			isBatteryLow = false;
		} else if (PowerManager.ACTION_POWER_SAVE_MODE_CHANGED.equals(action)) {
			// sendPowerSaving()
		}

		sendPowerSaving(context);
	}

	public PowerSaveModeReceiver(final Activity activity) {
		IntentFilter filter = new IntentFilter();
		filter.addAction(Intent.ACTION_BATTERY_LOW);
		filter.addAction(Intent.ACTION_BATTERY_OKAY);
		filter.addAction(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED);
		activity.registerReceiver(this, filter);

		activity.getContentResolver().registerContentObserver(Settings.System.CONTENT_URI, true, new ContentObserver(null) {
			@Override
			public void onChange(boolean selfChange, Uri uri) {
				super.onChange(selfChange, uri);

				String key = uri.getPath();
				if (key == null) {
					return;
				}
				key = key.substring(key.lastIndexOf("/") + 1);
				if (key.equals("user_powersaver_enable") || key.equals("psm_switch") || key.equals("powersaving_switch")) {
					sendPowerSaving(activity);
				}
			}
		});
		sendPowerSaving(activity);
	}

	public void destroy(final Context context) {
		context.unregisterReceiver(this);
	}

	private static boolean getNativePowerSaving(final Context context) {
		final PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
		return pm.isPowerSaveMode();
	}

	protected void sendPowerSaving(final Context context) {
		boolean isPowerSaving = getNativePowerSaving(context);

		if (!PpssppActivity.libraryLoaded) {
			Log.e(TAG, "Cannot send power saving: Library not loaded");
			return;
		}

		try {
			if (isBatteryLow || isPowerSaving) {
				NativeApp.sendMessageFromJava("core_powerSaving", "true");
			} else {
				NativeApp.sendMessageFromJava("core_powerSaving", "false");
			}
		} catch (Exception e) {
			Log.e(TAG, "Exception in sendPowerSaving: " + e);
		}
	}
}
