package com.henrikrydgard.libnative;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.lang.reflect.Field;
import java.util.List;
import java.util.UUID;

import android.annotation.SuppressLint;
import android.annotation.TargetApi;
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
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyEvent;
import android.view.MotionEvent;
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
	
	// Settings for best audio latency
	private int optimalFramesPerBuffer;
	private int optimalSampleRate;
	
	@TargetApi(17)
	private void detectOptimalAudioSettings() {
		AudioManager am = (AudioManager)getSystemService(Context.AUDIO_SERVICE);
		optimalFramesPerBuffer = Integer.parseInt(am.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER));		
		optimalSampleRate = Integer.parseInt(am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE));		
	}
	
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
        if (Build.VERSION.SDK_INT >= 17) {
        	// Get the optimal buffer sz
        	detectOptimalAudioSettings();
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
		@SuppressWarnings("deprecation")
        int scrPixelFormat = display.getPixelFormat();
        @SuppressWarnings("deprecation")
        int scrWidth = display.getWidth(); 
        @SuppressWarnings("deprecation")
		int scrHeight = display.getHeight();
        float scrRefreshRate = display.getRefreshRate();
	    String externalStorageDir = sdcard.getAbsolutePath(); 
	    String dataDir = this.getFilesDir().getAbsolutePath();
		String apkFilePath = appInfo.sourceDir; 
		NativeApp.sendMessage("Message from Java", "Hot Coffee");
		DisplayMetrics metrics = new DisplayMetrics();
		getWindowManager().getDefaultDisplay().getMetrics(metrics);
		int dpi = metrics.densityDpi;
		
		// INIT!
		NativeApp.audioConfig(optimalFramesPerBuffer, optimalSampleRate);
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

		if (Build.VERSION.SDK_INT >= 14) {
			darkenOnScreenButtons();
		}
        
        /*
        editText = new EditText(this);
        editText.setText("Hello world");
        
        addContentView(editText, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        */
        // inputBox("Please ener a s", "", "Save");
		// Toast.makeText(this, "Value: " + input.getText().toString(), Toast.LENGTH_LONG).show();
    }

	@TargetApi(Build.VERSION_CODES.ICE_CREAM_SANDWICH)
	public void darkenOnScreenButtons() {
		mGLSurfaceView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LOW_PROFILE);
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
    	Log.i(TAG, "nativeapp pause");
        NativeApp.pause();
    	Log.i(TAG, "gl pause");
        mGLSurfaceView.onPause();
    	Log.i(TAG, "onPause returning");
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
		if (Build.VERSION.SDK_INT >= 14) {
			darkenOnScreenButtons();
		}
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
   
    // Prevent destroying and recreating the main activity when the device rotates etc,
    // since this would stop the sound.
    @Override
    public void onConfigurationChanged(Configuration newConfig) {
    	// Ignore orientation change
    	super.onConfigurationChanged(newConfig);
    } 
    
    public static boolean inputBoxCancelled;

    
    InputDeviceState inputPlayerA;
    String inputPlayerADesc;
    
    // We simply grab the first input device to produce an event and ignore all others that are connected.
    @TargetApi(Build.VERSION_CODES.GINGERBREAD)
	private InputDeviceState getInputDeviceState(InputEvent event) {
        InputDevice device = event.getDevice();
        if (device == null) {
            return null;
        }
        if (inputPlayerA == null) {
            inputPlayerA = new InputDeviceState(device);
            inputPlayerADesc = getInputDesc(device);
        }

        if (inputPlayerA.getDevice() == device) {
            return inputPlayerA;
        }

        /*
        if (inputPlayerB == null) {
            inputPlyerB = new InputDeviceStats(device);
        }

        if (inputPlayerB.getDevice() == device) {
            return inputPlayerB;
        }*/

        return null;
    }

    // We grab the keys before onKeyDown/... even see them. This is also better because it lets us
    // distinguish devices.
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB_MR1) {
			InputDeviceState state = getInputDeviceState(event);
			if (state == null) {
				return super.dispatchKeyEvent(event);
			}
	
			switch (event.getAction()) {
			case KeyEvent.ACTION_DOWN:
				if (state.onKeyDown(event)) {
					return true;
				}
				break;
	
			case KeyEvent.ACTION_UP:
				if (state.onKeyUp(event)) {
					return true;
				}
				break;
			}
        }
        
        // Let's go through the old path (onKeyUp, onKeyDown).
		return super.dispatchKeyEvent(event);
    } 
    
	@TargetApi(16)
	static public String getInputDesc(InputDevice input) {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN) {
			return input.getDescriptor();
		} else {
			List<InputDevice.MotionRange> motions = input.getMotionRanges();
			String fakeid = "";
			for (InputDevice.MotionRange range : motions)
				fakeid += range.getAxis();
			return fakeid;
		}
	}

	@Override
	@TargetApi(12)
	public boolean onGenericMotionEvent(MotionEvent event) {
		Log.d(TAG, "onGenericMotionEvent: " + event);
		if ((event.getSource() & InputDevice.SOURCE_JOYSTICK) != 0) {
	        if (Build.VERSION.SDK_INT >= 12) {
	        	InputDeviceState state = getInputDeviceState(event);
	        	state.onJoystickMotion(event);
	        	return true;
	        }
		}

		if ((event.getSource() & InputDevice.SOURCE_CLASS_POINTER) != 0) {
	         switch (event.getAction()) {
	             case MotionEvent.ACTION_HOVER_MOVE:
	                 // process the mouse hover movement...
	                 return true;
	             case MotionEvent.ACTION_SCROLL:
	                 NativeApp.mouseWheelEvent(event.getX(), event.getY());
	                 return true;
	         }
	    }
		return super.onGenericMotionEvent(event);
	}
	
	@SuppressLint("NewApi")
	@Override
	public boolean onKeyDown(int keyCode, KeyEvent event) {
		// Eat these keys, to avoid accidental exits / other screwups.
		// Maybe there's even more we need to eat on tablets?
		switch (keyCode) {
		case KeyEvent.KEYCODE_BACK:
			if (event.isAltPressed()) {
				NativeApp.keyDown(0, 1004); // special custom keycode
			} else if (NativeApp.isAtTopLevel()) {
				Log.i(TAG, "IsAtTopLevel returned false.");
				return super.onKeyUp(keyCode, event);
			} else {
				NativeApp.keyDown(0, keyCode);
			}
			return true;
		case KeyEvent.KEYCODE_MENU:
		case KeyEvent.KEYCODE_SEARCH:
			NativeApp.keyDown(0, keyCode);
			return true;
		case KeyEvent.KEYCODE_VOLUME_DOWN:
		case KeyEvent.KEYCODE_VOLUME_UP:
			// NativeApp should ignore these.
			return super.onKeyDown(keyCode, event);
			
		case KeyEvent.KEYCODE_DPAD_UP:
		case KeyEvent.KEYCODE_DPAD_DOWN:
		case KeyEvent.KEYCODE_DPAD_LEFT:
		case KeyEvent.KEYCODE_DPAD_RIGHT:
			// Joysticks are supported in Honeycomb MR1 and later via the onGenericMotionEvent method.
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB_MR1 && event.getSource() == InputDevice.SOURCE_JOYSTICK) {
				return super.onKeyUp(keyCode, event);
			}
			// Fall through
		default:
			// send the rest of the keys through.
			// TODO: get rid of the three special cases above by adjusting the native side of the code.
			// Log.d(TAG, "Key down: " + keyCode + ", KeyEvent: " + event);
			NativeApp.keyDown(0, keyCode);
			return true;
		}
	}

	@SuppressLint("NewApi")
	@Override
	public boolean onKeyUp(int keyCode, KeyEvent event) {
		switch (keyCode) {
		case KeyEvent.KEYCODE_BACK:
			if (event.isAltPressed()) {
				NativeApp.keyUp(0, 1004); // special custom keycode
			} else if (NativeApp.isAtTopLevel()) {
				Log.i(TAG, "IsAtTopLevel returned false.");
				return super.onKeyUp(keyCode, event);
			} else {
				NativeApp.keyUp(0, keyCode);
			}
			return true;
		case KeyEvent.KEYCODE_MENU:
		case KeyEvent.KEYCODE_SEARCH:
			// Search probably should also be ignored. We send it to the app.
			NativeApp.keyUp(0, keyCode);
			return true;
		case KeyEvent.KEYCODE_VOLUME_DOWN:
		case KeyEvent.KEYCODE_VOLUME_UP:
			return super.onKeyUp(keyCode, event);
		case KeyEvent.KEYCODE_DPAD_UP:
		case KeyEvent.KEYCODE_DPAD_DOWN:
		case KeyEvent.KEYCODE_DPAD_LEFT:
		case KeyEvent.KEYCODE_DPAD_RIGHT:
			// Joysticks are supported in Honeycomb MR1 and later via the onGenericMotionEvent method.
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB_MR1 && event.getSource() == InputDevice.SOURCE_JOYSTICK) {
				return super.onKeyUp(keyCode, event);
			}
			// Fall through
		default:
			// send the rest of the keys through.
			// TODO: get rid of the three special cases above by adjusting the native side of the code.
			// Log.d(TAG, "Key down: " + keyCode + ", KeyEvent: " + event);
			NativeApp.keyUp(0, keyCode);
			return true;
		}
	}
	
	public String inputBox(String title, String defaultText, String defaultAction) {
    	final FrameLayout fl = new FrameLayout(this);
    	final EditText input = new EditText(this);
    	input.setGravity(Gravity.CENTER);

    	inputBoxCancelled = false;
		FrameLayout.LayoutParams editBoxLayout = new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT);
    	editBoxLayout.setMargins(2, 20, 2, 20);
    	fl.addView(input, editBoxLayout);

    	input.setText(defaultText);
    	input.selectAll();
    	
    	AlertDialog dlg = new AlertDialog.Builder(this)
    		.setView(fl)
    		.setTitle(title)
    		.setPositiveButton(defaultAction, new DialogInterface.OnClickListener(){
    			public void onClick(DialogInterface d, int which) {
    				d.dismiss();
    			}
    		})
    		.setNegativeButton("Cancel", new DialogInterface.OnClickListener(){
    			public void onClick(DialogInterface d, int which) {
    				d.cancel();
        	    	NativeActivity.inputBoxCancelled = false;
    			}
    		}).create();
    	dlg.setCancelable(false);
    	dlg.show();
    	if (inputBoxCancelled)
    		return null;
    	NativeApp.sendMessage("INPUTBOX:" + title, input.getText().toString());
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
    		// Don't need this, can just use launchBrowser with a market:
    		// http://stackoverflow.com/questions/3442366/android-link-to-market-from-inside-another-app
    		// http://developer.android.com/guide/publishing/publishing.html#marketintent
    	} else if (command.equals("toast"))  {
    		Toast toast = Toast.makeText(this, params, Toast.LENGTH_SHORT);
    		toast.show();
    	} else if (command.equals("showKeyboard")) {
    		//InputMethodManager inputMethodManager=(InputMethodManager)getSystemService(Context.INPUT_METHOD_SERVICE);
    	    //inputMethodManager.toggleSoftInputFromWindow(this, InputMethodManager.SHOW_FORCED, 0);
    	} else if (command.equals("inputBox")) {
    		inputBox(params, "", "OK");
    	} else {
    		Log.e(TAG, "Unsupported command " + command + " , param: " + params);
    	}
    }
}
