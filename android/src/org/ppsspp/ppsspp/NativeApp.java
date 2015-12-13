package org.ppsspp.ppsspp;


// Note that the display* methods are in NativeRenderer.java 

public class NativeApp {
	public final static int DEVICE_ID_DEFAULT = 0; 
	public final static int DEVICE_ID_KEYBOARD = 1; 
	public final static int DEVICE_ID_MOUSE = 2; 
	public final static int DEVICE_ID_PAD_0 = 10; 
	
	public final static int DEVICE_TYPE_MOBILE = 0;
	public final static int DEVICE_TYPE_TV = 1;
	public final static int DEVICE_TYPE_DESKTOP = 2;

	public static native void init(String model, int deviceType, int xres, int yres, String languageRegion, String apkPath, String dataDir, String externalDir, String libraryDir, String shortcutParam, String installID, int androidVersion);
	
	public static native void audioConfig(int optimalFramesPerBuffer, int optimalSampleRate);
	public static native void displayConfig(float dpi, float refreshRate);
	
	public static native boolean isLandscape();
	public static native boolean isAtTopLevel();

	public static native void sendMessage(String msg, String arg);
	
	public static native String queryConfig(String queryName);
}
 