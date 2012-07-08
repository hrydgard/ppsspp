package com.turboviking.libnative;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.lang.reflect.Field;
import java.util.UUID;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.ConfigurationInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.content.res.Configuration;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.Display;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Window;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.Toast;



class Installation {
    private static String sID = null;
    private static final String INSTALLATION = "INSTALLATION";

    public synchronized static String id(Context context) {
        if (sID == null) {  
            File installation = new File(context.getFilesDir(), INSTALLATION);
            try {
                if (!installation.exists())
                    writeInstallationFile(installation);
                sID = readInstallationFile(installation);
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        }
        return sID;
    }

    private static String readInstallationFile(File installation) throws IOException {
        RandomAccessFile f = new RandomAccessFile(installation, "r");
        byte[] bytes = new byte[(int) f.length()];
        f.readFully(bytes);
        f.close();
        return new String(bytes);
    }

    private static void writeInstallationFile(File installation) throws IOException {
        FileOutputStream out = new FileOutputStream(installation);
        String id = UUID.randomUUID().toString();
        out.write(id.getBytes());
        out.close();
    }
}


public class NativeActivity extends Activity {
	// Remember to loadLibrary your JNI .so in a static {} block

	// Adjust these as necessary
	private static String TAG = "NativeActivity";
   
	// Graphics and audio interfaces
	private GLSurfaceView mGLSurfaceView;
	private NativeAudioPlayer audioPlayer;
	
	boolean useOpenSL = false;
	
	public static String runCommand;
	public static String commandParameter;
	
	public static String installID;
	
	String getApplicationLibraryDir(ApplicationInfo application) {    
	    String libdir = null;
	    try {
	        // Starting from Android 2.3, nativeLibraryDir is available:
	        Field field = ApplicationInfo.class.getField("nativeLibraryDir");
	        libdir = (String) field.get(application);
	    } catch (SecurityException e1) {
	    } catch (NoSuchFieldException e1) {
	    } catch (IllegalArgumentException e) {
	    } catch (IllegalAccessException e) {
	    }
	    if (libdir == null) {
	        // Fallback for Android < 2.3:
	        libdir = application.dataDir + "/lib";
	    }
	    return libdir;
	}

    @Override
    public void onCreate(Bundle savedInstanceState) {
    	if (NativeApp.isLandscape()) {
    		setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
    	} else {
    		setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
    	}
		super.onCreate(savedInstanceState); 
    	Log.i(TAG, "onCreate");
    	installID = Installation.id(this);
    	// Get system information
		ApplicationInfo appInfo = null;  
		PackageManager packMgmr = getPackageManager();
		String packageName = getPackageName();
		try {
		    appInfo = packMgmr.getApplicationInfo(packageName, 0);
	    } catch  (NameNotFoundException e) {
		    e.printStackTrace();
		    throw new RuntimeException("Unable to locate assets, aborting...");
	    }
		String libraryDir = getApplicationLibraryDir(appInfo);
	    File sdcard = Environment.getExternalStorageDirectory();
        Display display = ((WindowManager)this.getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();
        int scrPixelFormat = display.getPixelFormat();
        int scrWidth = display.getWidth(); 
        int scrHeight = display.getHeight();
        float scrRefreshRate = (float)display.getRefreshRate();
	    String externalStorageDir = sdcard.getAbsolutePath(); 
	    String dataDir = this.getFilesDir().getAbsolutePath();
		String apkFilePath = appInfo.sourceDir; 
		NativeApp.sendMessage("Message from Java", "Hot Coffee");
		NativeApp.init(scrWidth, scrHeight, apkFilePath, dataDir, externalStorageDir, libraryDir, installID, useOpenSL);
     
 		// Keep the screen bright - very annoying if it goes dark when tilting away
		Window window = this.getWindow();
		window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
		setVolumeControlStream(AudioManager.STREAM_MUSIC);
        Log.i(TAG, "W : " + scrWidth + " H: " + scrHeight + " rate: " + scrRefreshRate + " fmt: " + scrPixelFormat);
             
        // Initialize Graphics
        if (!detectOpenGLES20()) {
		    throw new RuntimeException("Application requires OpenGL ES 2.0.");
        } else {
        	Log.i(TAG, "OpenGL ES 2.0 detected.");
        }
    
        mGLSurfaceView = new NativeGLView(this);
        mGLSurfaceView.setRenderer(new NativeRenderer(this));
        setContentView(mGLSurfaceView);
        if (Build.VERSION.SDK_INT >= 9) {
        	// Native OpenSL is available. Let's not use the Java player in the future.
        	// TODO: code for that.
        }
        if (!useOpenSL)
        	audioPlayer = new NativeAudioPlayer();
    }  

    private boolean detectOpenGLES20() {
        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        ConfigurationInfo info = am.getDeviceConfigurationInfo();
        return info.reqGlEsVersion >= 0x20000;
    }
         
   
    @Override 
    protected void onPause() {
        super.onPause();
    	Log.i(TAG, "onPause");
        if (audioPlayer != null) {
        	audioPlayer.stop();
        }
        NativeApp.pause();
        mGLSurfaceView.onPause();
    }
      
    @Override
    protected void onResume() {
    	super.onResume();
    	Log.i(TAG, "onResume");
        mGLSurfaceView.onResume();
        if (audioPlayer != null) {
        	audioPlayer.play();
        }
        NativeApp.resume();
    }

    @Override
    protected void onStop() {
    	super.onStop(); 
    	Log.i(TAG, "onStop");
    } 
  
	@Override
	protected void onDestroy() {
		super.onDestroy();
      	Log.e(TAG, "onDestroy");
		NativeApp.shutdown();
		audioPlayer = null;
		mGLSurfaceView = null;
	}  
     
	public boolean overrideKeys() {
		return true;
	}
	
    @Override 
    public boolean onKeyDown(int keyCode, KeyEvent event) {
    	if (!overrideKeys())
    		return false;
    	// Eat these keys, to avoid accidental exits / other screwups.
    	// Maybe there's even more we need to eat on tablets?
        if (keyCode == KeyEvent.KEYCODE_BACK) {
        	NativeApp.keyDown(1);
            return true;
        }
        else if (keyCode == KeyEvent.KEYCODE_MENU) {
        	NativeApp.keyDown(2);  
        	return true;
        }
        else if (keyCode == KeyEvent.KEYCODE_SEARCH) {
        	NativeApp.keyDown(3);
        	return true;
        }
        // Don't process any other keys.
        return false;
    } 
    
    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
    	// Eat these keys, to avoid accidental exits / other screwups.
    	// Maybe there's even more we need to eat on tablets?
        if (keyCode == KeyEvent.KEYCODE_BACK) {
        	NativeApp.keyUp(1);
            return true;
        }
        else if (keyCode == KeyEvent.KEYCODE_MENU) {
        	// Menu should be ignored from android 3 forwards
        	NativeApp.keyUp(2);  
        	return true;
        }
        else if (keyCode == KeyEvent.KEYCODE_SEARCH) {
        	// Search probably should also be ignored.
        	NativeApp.keyUp(3);
        	return true;
        } 
		return false; 
    }  
   
    // Prevent destroying and recreating the main activity when the device rotates etc,
    // since this would stop the sound.
    @Override
    public void onConfigurationChanged(Configuration newConfig) {
    	// Ignore orientation change
    	super.onConfigurationChanged(newConfig);
    } 
    
    public void processCommand(String command, String params) {
    	if (command.equals("launchBrowser")) {
    		Intent i = new Intent(Intent.ACTION_VIEW, Uri.parse(params));
    		startActivity(i);
    	} else if (command.equals("launchEmail")) {
    		Intent send = new Intent(Intent.ACTION_SENDTO);
    		String uriText;
    		uriText = "mailto:email@gmail.com" + 
    		          "?subject=Rollfish is..." + 
    		          "&body=great! Or?";
    		uriText = uriText.replace(" ", "%20");
    		Uri uri = Uri.parse(uriText);
    		send.setData(uri);
    		startActivity(Intent.createChooser(send, "E-mail Henrik!"));
    	} else if (command.equals("launchMarket")) {
    		// http://stackoverflow.com/questions/3442366/android-link-to-market-from-inside-another-app
    		// http://developer.android.com/guide/publishing/publishing.html#marketintent
    	} else if (command.equals("toast"))  {
    		Toast toast = Toast.makeText(this, params, 2000);
    		toast.show();
    	} else if (command.equals("showkeyboard")) {
    		//InputMethodManager inputMethodManager=(InputMethodManager)getSystemService(Context.INPUT_METHOD_SERVICE);
    	    //inputMethodManager.toggleSoftInputFromWindow(this, InputMethodManager.SHOW_FORCED, 0);
    	} else {
    		Log.e(TAG, "Unsupported command " + command + " , param: " + params);
    	}
    }
}
