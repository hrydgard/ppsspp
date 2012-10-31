package com.turboviking.libnative;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.lang.reflect.Field;
import java.util.UUID;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.ConfigurationInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.content.res.Configuration;
import android.media.AudioManager;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.FrameLayout;
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
        if (Build.VERSION.SDK_INT >= 9) {
        	// Native OpenSL is available. Let's use it!
        	useOpenSL = true;
        }

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
        @SuppressWarnings("deprecation")
        int scrWidth = display.getWidth(); 
        @SuppressWarnings("deprecation")
		int scrHeight = display.getHeight();
        float scrRefreshRate = (float)display.getRefreshRate();
	    String externalStorageDir = sdcard.getAbsolutePath(); 
	    String dataDir = this.getFilesDir().getAbsolutePath();
		String apkFilePath = appInfo.sourceDir; 
		NativeApp.sendMessage("Message from Java", "Hot Coffee");
		DisplayMetrics metrics = new DisplayMetrics();
		getWindowManager().getDefaultDisplay().getMetrics(metrics);
		int dpi = metrics.densityDpi;
		
		// INIT!
		NativeApp.init(scrWidth, scrHeight, dpi, apkFilePath, dataDir, externalStorageDir, libraryDir, installID, useOpenSL);
     
 		// Keep the screen bright - very annoying if it goes dark when tilting away
		Window window = this.getWindow();
		window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
		setVolumeControlStream(AudioManager.STREAM_MUSIC);
        Log.i(TAG, "W : " + scrWidth + " H: " + scrHeight + " rate: " + scrRefreshRate + " fmt: " + scrPixelFormat);
             
        // Initialize Graphics
        
        if (!detectOpenGLES20()) {
        	Log.i(TAG, "OpenGL ES 2.0 NOT detected.");
        } else {
        	Log.i(TAG, "OpenGL ES 2.0 detected.");
        }
    
        mGLSurfaceView = new NativeGLView(this);
        mGLSurfaceView.setRenderer(new NativeRenderer(this));
        setContentView(mGLSurfaceView);
        if (!useOpenSL)
        	audioPlayer = new NativeAudioPlayer();
        lightsOut();
        
        /*
        editText = new EditText(this);
        editText.setText("Hello world");
        
        addContentView(editText, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        */
        // inputBox("Please ener a s", "", "Save");
		// Toast.makeText(this, "Value: " + input.getText().toString(), Toast.LENGTH_LONG).show();
    }  
    
	public void lightsOut() {
	     if (Build.VERSION.SDK_INT >= 11) {
	    	 // mGLSurfaceView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LOW_PROFILE);
	     }
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
    	Log.i(TAG, "onStop - do nothing");
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
        	if (NativeApp.isAtTopLevel()) {
        		return false;
        	} else {
        		NativeApp.keyUp(1);
        		return true;
        	}
        }
        else if (keyCode == KeyEvent.KEYCODE_MENU) {
        	// Menu should be ignored from SDK 11 forwards. We send it to the app.
        	NativeApp.keyUp(2);  
        	return true;
        }
        else if (keyCode == KeyEvent.KEYCODE_SEARCH) {
        	// Search probably should also be ignored. We send it to the app.
        	NativeApp.keyUp(3);
        	return true;
        } 
        // All other keys retain their default behavior.
		return false; 
    }  
   
    // Prevent destroying and recreating the main activity when the device rotates etc,
    // since this would stop the sound.
    @Override
    public void onConfigurationChanged(Configuration newConfig) {
    	// Ignore orientation change
    	super.onConfigurationChanged(newConfig);
    } 
    
    public static boolean inputBoxCancelled;
    @SuppressWarnings("deprecation")
	public String inputBox(String title, String defaultText, String defaultAction) {
    	final FrameLayout fl = new FrameLayout(this);
    	final EditText input = new EditText(this);
    	input.setGravity(Gravity.CENTER);

    	inputBoxCancelled = false;
    	FrameLayout.LayoutParams editBoxLayout = new FrameLayout.LayoutParams(FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT);
    	editBoxLayout.setMargins(2, 20, 2, 20);
    	fl.addView(input, editBoxLayout);

    	input.setText(defaultText);
    	input.selectAll();
    	
    	AlertDialog dlg = new AlertDialog.Builder(this)
    		.setView(fl)
    		.setTitle(title)
    		.setPositiveButton(defaultAction, new DialogInterface.OnClickListener(){
    			@Override
    			public void onClick(DialogInterface d, int which) {
    				d.dismiss();
    			}
    		})
    		.setNegativeButton("Cancel", new DialogInterface.OnClickListener(){
    			@Override
    			public void onClick(DialogInterface d, int which) {
    				d.cancel();
        	    	NativeActivity.inputBoxCancelled = false;
    			}
    		}).create();
    	dlg.setCancelable(false);
    	dlg.show();
    	if (inputBoxCancelled)
    		return null;
    	return input.getText().toString();
    }
     
    public void processCommand(String command, String params) {
    	if (command.equals("launchBrowser")) {
    		Intent i = new Intent(Intent.ACTION_VIEW, Uri.parse(params));
    		startActivity(i);
    	} else if (command.equals("launchEmail")) {
    		Intent send = new Intent(Intent.ACTION_SENDTO);
    		String uriText;
    		uriText = "mailto:email@gmail.com" + 
    		          "?subject=Your app is..." + 
    		          "&body=great! Or?";
    		uriText = uriText.replace(" ", "%20");
    		Uri uri = Uri.parse(uriText);
    		send.setData(uri);
    		startActivity(Intent.createChooser(send, "E-mail the app author!"));
    	} else if (command.equals("launchMarket")) {
    		// http://stackoverflow.com/questions/3442366/android-link-to-market-from-inside-another-app
    		// http://developer.android.com/guide/publishing/publishing.html#marketintent
    	} else if (command.equals("toast"))  {
    		Toast toast = Toast.makeText(this, params, Toast.LENGTH_SHORT);
    		toast.show();
    	} else if (command.equals("showkeyboard")) {
    		//InputMethodManager inputMethodManager=(InputMethodManager)getSystemService(Context.INPUT_METHOD_SERVICE);
    	    //inputMethodManager.toggleSoftInputFromWindow(this, InputMethodManager.SHOW_FORCED, 0);
    	} else if (command.equals("gettext")) {
    		inputBox("Enter text", params, "OK");
    	} else {
    		Log.e(TAG, "Unsupported command " + command + " , param: " + params);
    	}
    }
}
