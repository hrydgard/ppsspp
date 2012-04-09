package com.turboviking.libnative;

public class NativeApp {      
	public static native void init(
			int xxres, int yyres, String apkPath,
			String dataDir, String externalDir, String installID);
	public static native void shutdown();
	 
	public static native void keyDown(int key);
	public static native void keyUp(int key);

	// will only be called between init() and shutdown()
	public static native void audioRender(short [] buffer);
	 
	// Sensor/input data. These are asynchronous, beware!
	public static native void touch(int x, int y, int data);
	public static native void accelerometer(float x, float y, float z);
} 