package com.bda.controller;

import android.app.Activity;
import android.os.Handler;
import android.view.SurfaceView;

public class Controller {
	public static final int ACTION_VERSION_MOGA = 0;
	public static final int STATE_CURRENT_PRODUCT_VERSION = 1;
	
	static Controller sInstance;
	
	private Controller() {}
	
	public static Controller getInstance(Activity activity) {
		if (sInstance == null) {
			sInstance = new Controller();
		}
		return sInstance;
	}
	public int getState(int val) {
		return 0;
	}
	public int setListener(SurfaceView view, Handler handler) {
		return 0;		
	}
	public int init() {
		return 0;
	}
	public int onPause() {
		return 0;
	}
	public int onResume() {
		return 0;
	}
	public int exit() {
		return 0;
	}
}
