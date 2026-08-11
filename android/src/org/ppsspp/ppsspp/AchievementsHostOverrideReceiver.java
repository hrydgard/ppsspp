package org.ppsspp.ppsspp;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.text.TextUtils;
import android.util.Log;

public class AchievementsHostOverrideReceiver extends BroadcastReceiver {
	private static final String TAG = "AchievementsHostOverride";
	public static final String ACTION_SET = "org.ppsspp.ppsspp.action.SET_ACHIEVEMENTS_HOST_OVERRIDE";
	public static final String ACTION_CLEAR = "org.ppsspp.ppsspp.action.CLEAR_ACHIEVEMENTS_HOST_OVERRIDE";
	public static final String EXTRA_HOST = "host";
	private static final String PREFS_NAME = "achievements_host_override";
	private static final String KEY_HOST = "host";

	@Override
	public void onReceive(Context context, Intent intent) {
		if (intent == null || intent.getAction() == null) {
			return;
		}

		SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
		String action = intent.getAction();

		if (ACTION_CLEAR.equals(action)) {
			clearHostOverride(prefs);
			PpssppActivity.clearAchievementsHostOverride();
			Log.i(TAG, "Cleared achievements host override");
			return;
		}

		if (!ACTION_SET.equals(action)) {
			return;
		}

		String host = intent.getStringExtra(EXTRA_HOST);
		if (host != null) {
			host = host.trim();
		}

		if (TextUtils.isEmpty(host)) {
			clearHostOverride(prefs);
			PpssppActivity.clearAchievementsHostOverride();
			Log.i(TAG, "Empty host received, cleared override");
			return;
		}

		if (!isAllowedHost(host)) {
			Log.w(TAG, "Rejected non-loopback achievements host override");
			return;
		}

		prefs.edit().putString(KEY_HOST, host).apply();
		PpssppActivity.applyAchievementsHostOverride(host);
		Log.i(TAG, "Stored achievements host override");
	}

	public static String getAchievementsHostOverride(Context context) {
		SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
		return prefs.getString(KEY_HOST, null);
	}

	private static void clearHostOverride(SharedPreferences prefs) {
		prefs.edit().remove(KEY_HOST).apply();
	}

	private static boolean isAllowedHost(String host) {
		Uri uri = Uri.parse(host);
		if (uri == null) {
			return false;
		}

		if (!"http".equals(uri.getScheme())) {
			return false;
		}

		if (!uri.isHierarchical()) {
			return false;
		}

		String authority = uri.getEncodedAuthority();
		if (TextUtils.isEmpty(authority) || authority.contains("@")) {
			return false;
		}

		if (!TextUtils.isEmpty(uri.getEncodedPath()) && !"/".equals(uri.getEncodedPath())) {
			return false;
		}

		if (!TextUtils.isEmpty(uri.getEncodedQuery()) || !TextUtils.isEmpty(uri.getEncodedFragment())) {
			return false;
		}

		int port = uri.getPort();
		if (port < 0 || port > 65535) {
			return false;
		}

		String parsedHost = uri.getHost();
		if (TextUtils.isEmpty(parsedHost)) {
			return false;
		}

		return "127.0.0.1".equals(parsedHost) || "localhost".equalsIgnoreCase(parsedHost) || "::1".equals(parsedHost);
	}
}
