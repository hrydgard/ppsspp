package org.ppsspp.ppsspp;


import android.content.Context;
import android.location.GpsSatellite;
import android.location.GpsStatus;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Bundle;
import android.util.Log;

import java.util.Iterator;

class LocationHelper implements LocationListener, GpsStatus.Listener {
	private static final String TAG = LocationHelper.class.getSimpleName();
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
				mLocationManager.addGpsStatusListener(this);
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
		// Android altitude is in meters above the WGS 84 reference ellipsoid
		float altitude = (float) location.getAltitude();
		float speed = location.getSpeed();
		float bearing = location.getBearing();
		long time = location.getTime() / 1000; // ms to s !!

		NativeApp.setGpsDataAndroid(latitude, longitude, altitude, speed, bearing, time);
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


	@Override
	public void onGpsStatusChanged(int i) {
		switch (i) {
			case GpsStatus.GPS_EVENT_STARTED:
			case GpsStatus.GPS_EVENT_STOPPED:
			case GpsStatus.GPS_EVENT_FIRST_FIX:
				break;
			case GpsStatus.GPS_EVENT_SATELLITE_STATUS: {
				try {
					GpsStatus gpsStatus = mLocationManager.getGpsStatus(null);
					Iterable<GpsSatellite> satellites = gpsStatus.getSatellites();

					short index = 0;
					for (Iterator<GpsSatellite> iterator = satellites.iterator(); iterator.hasNext(); ) {
						GpsSatellite satellite = iterator.next();
						if (satellite.getPrn() > 37) {
							continue;
						}
						NativeApp.setSatInfoAndroid(index, (short) satellite.getPrn(), (short) satellite.getElevation(),
							(short) satellite.getAzimuth(), (short) satellite.getSnr(), satellite.usedInFix() ? (short) 1 : (short) 0);
						index++;
						if (index == 24) {
							break;
						}
					}
				} catch (SecurityException e) {
					Log.e(TAG, e.toString());
				}
				break;
			}
		}
	}
}
