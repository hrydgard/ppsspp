package org.ppsspp.ppsspp;

import android.annotation.TargetApi;
import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.database.ContentObserver;
import android.net.Uri;
import android.os.Build;
import android.os.PowerManager;
import android.provider.Settings;
import android.text.TextUtils;
import android.util.Log;

public class PowerSaveModeReceiver extends BroadcastReceiver {
	private static final String TAG = PowerSaveModeReceiver.class.getSimpleName();
	private static BroadcastReceiver saveModeReceiver = null;
	private static boolean isPowerSaving = false;
	private static boolean isBatteryLow = false;

	@Override
	public void onReceive(final Context context, final Intent intent) {
		final String action = intent.getAction();
		if (Intent.ACTION_BATTERY_LOW.equals(action)) {
			isBatteryLow = true;
		} else if (Intent.ACTION_BATTERY_OKAY.equals(action)) {
			isBatteryLow = false;
		}

		sendPowerSaving(context);
	}

	public static void initAndSend(final Activity activity) {
		saveModeReceiver = new BroadcastReceiver() {
			@Override
			public void onReceive(Context context, Intent intent) {
				if (PowerManager.ACTION_POWER_SAVE_MODE_CHANGED.equals(intent.getAction())) {
					sendPowerSaving(context);
				}
			}
		};
		IntentFilter filter = new IntentFilter();
		filter.addAction(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED);
		activity.registerReceiver(saveModeReceiver, filter);

		sendPowerSaving(activity);

		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN) {
			activity.getContentResolver().registerContentObserver(Settings.System.CONTENT_URI, true, new ContentObserver(null) {
				@TargetApi(Build.VERSION_CODES.JELLY_BEAN)
				@Override
				public void onChange(boolean selfChange, Uri uri) {
					super.onChange(selfChange, uri);

					String key = uri.getPath();
					if (key == null) {
						return;
					}
					key = key.substring(key.lastIndexOf("/") + 1, key.length());
					if (key.equals("user_powersaver_enable") || key.equals("psm_switch") || key.equals("powersaving_switch")) {
						sendPowerSaving(activity);
					}
				}
			});
		}
	}

	public static void destroy(final Activity activity) {
		if (saveModeReceiver != null) {
			activity.unregisterReceiver(saveModeReceiver);
			saveModeReceiver = null;
		}
	}

	@TargetApi(21)
	private static boolean getNativePowerSaving(final Context context) {
		final PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
		return pm.isPowerSaveMode();
	}

	private static boolean getExtraPowerSaving(final Context context) {
		// http://stackoverflow.com/questions/25065635/checking-for-power-saver-mode-programically
		// HTC (Sense)
		if (getBooleanSetting(context, "user_powersaver_enable")) {
			return true;
		}
		// Samsung (Touchwiz)
		String s5Value = Settings.System.getString(context.getContentResolver(), "powersaving_switch");
		boolean hasS5Value = !TextUtils.isEmpty(s5Value);
		// On newer devices, psm_switch is always set, and powersaving_switch is used instead.
		if ((!hasS5Value && getBooleanSetting(context, "psm_switch")) || getBooleanSetting(context, "powersaving_switch")) {
			return true;
		}
		return false;
	}

	private static boolean getBooleanSetting(final Context context, final String name) {
		String value = Settings.System.getString(context.getContentResolver(), name);
		return value != null && value.equals("1");
	}

	protected static void sendPowerSaving(final Context context) {
		if (Build.VERSION.SDK_INT >= 21) {
			isPowerSaving = getNativePowerSaving(context);
		} else {
			isPowerSaving = getExtraPowerSaving(context);
		}

		if (!PpssppActivity.libraryLoaded) {
			Log.e(TAG, "Cannot send power saving: Library not loaded");
			return;
		}

		try {
			if (isBatteryLow || isPowerSaving) {
				NativeApp.sendMessage("core_powerSaving", "true");
			} else {
				NativeApp.sendMessage("core_powerSaving", "false");
			}
		} catch (Exception e) {
			Log.e(TAG, "Exception in sendPowerSaving: " + e.toString());
		}
	}
}
