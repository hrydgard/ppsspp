package org.ppsspp.ppsspp;

import android.content.Context;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Bundle;
import android.util.Log;

class LocationHelper implements LocationListener {
	private static final String TAG = "LocationHelper";
	private LocationManager mLocationManager;
	private boolean mLocationEnable;

	LocationHelper(Context context) {
		mLocationManager = (LocationManager) context.getSystemService(Context.LOCATION_SERVICE);
		mLocationEnable = false;
	}

	void startLocationUpdates() {
		Log.d(TAG, "startLocationUpdates");
		if (!mLocationEnable) {
			boolean isGPSEnabled = false;
			boolean isNetworkEnabled = false;
			try {
				isGPSEnabled = mLocationManager.isProviderEnabled(LocationManager.GPS_PROVIDER);
				isNetworkEnabled = mLocationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER);
				mLocationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000, 0, this);
				mLocationManager.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 1000, 0, this);
				mLocationEnable = true;
			} catch (SecurityException e) {
				Log.e(TAG, "Cannot start location updates: " + e.toString());
			}
			if (!isGPSEnabled && !isNetworkEnabled) {
				Log.i(TAG, "No location provider found");
				// TODO: notify user
			}
		}
	}

	void stopLocationUpdates() {
		Log.d(TAG, "stopLocationUpdates");
		if (mLocationEnable) {
			mLocationEnable = false;
			mLocationManager.removeUpdates(this);
		}
	}

	@Override
	public void onLocationChanged(Location location) {
		float latitude = (float) location.getLatitude();
		float longitude = (float) location.getLongitude();
		float altitude = (float) location.getAltitude();
		float speed = location.getSpeed();
		float bearing = location.getBearing();
		long time = location.getTime() / 1000; // ms to s !!

		NativeApp.pushNewGpsData(latitude, longitude, altitude, speed, bearing, time);
	}

	@Override
	public void onStatusChanged(String provider, int status, Bundle extras) {
	}

	@Override
	public void onProviderEnabled(String provider) {
	}

	@Override
	public void onProviderDisabled(String provider) {
	}
}
