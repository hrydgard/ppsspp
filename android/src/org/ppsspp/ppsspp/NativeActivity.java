package org.ppsspp.ppsspp;

import java.io.File;
import java.lang.reflect.Field;
import java.util.Locale;

import android.annotation.SuppressLint;
import android.annotation.TargetApi;
import android.app.AlertDialog;
import android.app.UiModeManager;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.content.res.Configuration;
import android.graphics.Point;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Vibrator;
import android.text.InputType;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.Gravity;
import android.view.View.OnSystemUiVisibilityChangeListener;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.Toast;

public class NativeActivity extends android.app.NativeActivity {
	// Remember to loadLibrary your JNI .so in a static {} block

	// Adjust these as necessary
	private static String TAG = "NativeActivity";
	
	// Allows us to skip a lot of initialization on secondary calls to onCreate.
	private static boolean initialized = false;
	
	private String shortcutParam = "";
	
	public static String runCommand;
	public static String commandParameter;
	public static String installID;
	
	// Remember settings for best audio latency
	private int optimalFramesPerBuffer;
	private int optimalSampleRate;
	
	private Vibrator vibrator;

    String inputPlayerADesc;
    
    // Functions for the app activity to override to change behaviour.
    
    public boolean useLowProfileButtons() {
    	return true;
    }
    
	@TargetApi(17)
	private void detectOptimalAudioSettings() {
		AudioManager am = (AudioManager)getSystemService(Context.AUDIO_SERVICE); 
		try {
			optimalFramesPerBuffer = Integer.parseInt(am.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER));
		} catch (NumberFormatException e) {
			// Ignore, if we can't parse it it's bogus and zero is a fine value (means we couldn't detect it).
		}
		try {
			optimalSampleRate = Integer.parseInt(am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE));
		} catch (NumberFormatException e) {
			// Ignore, if we can't parse it it's bogus and zero is a fine value (means we couldn't detect it).
		}
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

	@TargetApi(Build.VERSION_CODES.JELLY_BEAN_MR1)
	void GetScreenSizeJB(Point size, boolean real) {
        WindowManager w = getWindowManager();
		if (real) {
			w.getDefaultDisplay().getRealSize(size);
		}
	}

	@TargetApi(Build.VERSION_CODES.HONEYCOMB_MR2)
	void GetScreenSizeHC(Point size, boolean real) {
        WindowManager w = getWindowManager();
		if (real && Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1) {
			GetScreenSizeJB(size, real);
		} else {
			w.getDefaultDisplay().getSize(size);
		}
	}

	@SuppressWarnings("deprecation")
	public void GetScreenSize(Point size) {
        boolean real = useImmersive();
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB_MR2) {
			GetScreenSizeHC(size, real);
		} else {
	        WindowManager w = getWindowManager();
	        Display d = w.getDefaultDisplay();
			size.x = d.getWidth();
			size.y = d.getHeight();
		}
	}
	
	public void setShortcutParam(String shortcutParam) {
		this.shortcutParam = ((shortcutParam == null) ? "" : shortcutParam);
	}
	
	public void Initialize() {
        if (initialized) {
    		updateScreenRotation();
        	return;
        }
        initialized = true;

        if (Build.VERSION.SDK_INT >= 17) {
        	// Get the optimal buffer sz
        	detectOptimalAudioSettings();
        }
        
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

		int deviceType = NativeApp.DEVICE_TYPE_MOBILE;
		UiModeManager uiModeManager = (UiModeManager) getSystemService(UI_MODE_SERVICE);
		switch (uiModeManager.getCurrentModeType()) {
		case Configuration.UI_MODE_TYPE_TELEVISION:
			deviceType = NativeApp.DEVICE_TYPE_TV;
		    Log.i(TAG, "Running on an Android TV Device");
			break;
		case Configuration.UI_MODE_TYPE_DESK:
			deviceType = NativeApp.DEVICE_TYPE_DESKTOP;
		    Log.i(TAG, "Running on an Android desktop computer (!)");
			break;
		// All other device types are treated the same.
		}

		String libraryDir = getApplicationLibraryDir(appInfo);
	    File sdcard = Environment.getExternalStorageDirectory();

	    String externalStorageDir = sdcard.getAbsolutePath(); 
	    String dataDir = this.getFilesDir().getAbsolutePath();
		String apkFilePath = appInfo.sourceDir; 

		String model = Build.MANUFACTURER + ":" + Build.MODEL;
		String languageRegion = Locale.getDefault().getLanguage() + "_" + Locale.getDefault().getCountry(); 

		Point displaySize = new Point();
		GetScreenSize(displaySize);

		NativeApp.audioConfig(optimalFramesPerBuffer, optimalSampleRate);
		NativeApp.init(model, deviceType, displaySize.x, displaySize.y, languageRegion, apkFilePath, dataDir, externalStorageDir, libraryDir, shortcutParam, installID, Build.VERSION.SDK_INT);

		DisplayMetrics metrics = new DisplayMetrics();
		Display display = getWindowManager().getDefaultDisplay();
		display.getMetrics(metrics);
		NativeApp.displayConfig(metrics.densityDpi, display.getRefreshRate());
		NativeApp.sendMessage("cacheDir", getCacheDir().getAbsolutePath());

		// OK, config should be initialized, we can query for screen rotation.
		updateScreenRotation();

        vibrator = (Vibrator)getSystemService(VIBRATOR_SERVICE);
        if (Build.VERSION.SDK_INT >= 11) {
        	checkForVibrator();
        }
	}

	@TargetApi(9)
	private void updateScreenRotation() {
		// Query the native application on the desired rotation.
		int rot = 0;
		String rotString = NativeApp.queryConfig("screenRotation");
		try {
			rot = Integer.parseInt(rotString);
		} catch (NumberFormatException e) {
			Log.e(TAG, "Invalid rotation: " + rotString);
			return;
		}
		Log.i(TAG, "Setting requested orientation to " + rot);
		switch (rot) {
		case 0:
			setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
			break;
		case 1:
			setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
			break;
		case 2:
			setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
			break;
		case 3:
			setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE);
			break;
		case 4:
			setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT);
			break;
		}
	}
	
	private boolean useImmersive() {
		String immersive = NativeApp.queryConfig("immersiveMode");
		return immersive.equals("1") && Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT;
	}

	@SuppressLint("InlinedApi")
	@TargetApi(14)
	private void updateSystemUiVisibility() {
		/*
		int flags = 0;
		if (useLowProfileButtons()) {
			flags |= View.SYSTEM_UI_FLAG_LOW_PROFILE;
		}
		if (useImmersive()) {
			flags |= View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION;
		}
		if (getWindow().getDecorView() != null) {
			getWindow().getDecorView().setSystemUiVisibility(flags);
		} else {
			Log.e(TAG, "updateSystemUiVisibility: decor view not yet created, ignoring");
		}
		*/
	}
	
	// Need API 11 to check for existence of a vibrator? Zany.
	@TargetApi(11)
	public void checkForVibrator() {
        if (Build.VERSION.SDK_INT >= 11) {
	        if (!vibrator.hasVibrator()) {
	        	vibrator = null;
	        }
        }
	}

    @Override
    public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
    	installID = Installation.id(this);

		if (!initialized) {
			Initialize();
			initialized = true;
		}
		// Keep the screen bright - very annoying if it goes dark when tilting away
		//Window window = this.getWindow();
		//window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    	if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.ICE_CREAM_SANDWICH) {
			updateSystemUiVisibility();
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
				setupSystemUiCallback();
			}
		}
    }

    @TargetApi(19)
	void setupSystemUiCallback() {
        getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(new OnSystemUiVisibilityChangeListener() {
            @Override
            public void onSystemUiVisibilityChange(int visibility) {
                if (visibility == 0) {
                	updateSystemUiVisibility();
                }
            }
        });
    }

    @TargetApi(11)
	private AlertDialog.Builder createDialogBuilderWithTheme() {
   		return new AlertDialog.Builder(this, AlertDialog.THEME_HOLO_DARK);
	}
	
	// The return value is sent elsewhere. TODO in java, in SendMessage in C++.
	public void inputBox(String title, String defaultText, String defaultAction) {
    	final FrameLayout fl = new FrameLayout(this);
    	final EditText input = new EditText(this);
    	input.setGravity(Gravity.CENTER);

    	FrameLayout.LayoutParams editBoxLayout = new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT);
    	editBoxLayout.setMargins(2, 20, 2, 20);
    	fl.addView(input, editBoxLayout);

    	input.setInputType(InputType.TYPE_CLASS_TEXT);
    	input.setText(defaultText);
    	input.selectAll();
    	
    	AlertDialog.Builder bld = null;
    	if (Build.VERSION.SDK_INT < Build.VERSION_CODES.HONEYCOMB)
    		bld = new AlertDialog.Builder(this);
    	else
    		bld = createDialogBuilderWithTheme();

    	AlertDialog dlg = bld
    		.setView(fl)
    		.setTitle(title)
    		.setPositiveButton(defaultAction, new DialogInterface.OnClickListener(){
    			public void onClick(DialogInterface d, int which) {
    	    		NativeApp.sendMessage("inputbox_completed", input.getText().toString());
    				d.dismiss();
    			}
    		})
    		.setNegativeButton("Cancel", new DialogInterface.OnClickListener(){
    			public void onClick(DialogInterface d, int which) {
    	    		NativeApp.sendMessage("inputbox_failed", "");
    				d.cancel();
    			}
    		}).create();
    	
    	dlg.setCancelable(true);
    	dlg.show();
    }

	// called by the C++ code through JNI. Dispatch anything we can't directly handle
	// on the gfx thread to the UI thread.
	public void postCommand(String command, String parameter) {
		final String cmd = command;
		final String param = parameter;
		runOnUiThread(new Runnable() {
			public void run() {
				processCommand(cmd, param);
			}
		});
	}
	
    public boolean processCommand(String command, String params) {
		if (command.equals("launchBrowser")) {
			try {
				Intent i = new Intent(Intent.ACTION_VIEW, Uri.parse(params));
				startActivity(i);
				return true;
			} catch (Exception e) {
				// No browser?
				Log.e(TAG, e.toString());
				return false;
			}
		} else if (command.equals("launchEmail")) {
			try {
				Intent send = new Intent(Intent.ACTION_SENDTO);
				String uriText;
				uriText = "mailto:email@gmail.com" + "?subject=Your app is..."
						+ "&body=great! Or?";
				uriText = uriText.replace(" ", "%20");
				Uri uri = Uri.parse(uriText);
				send.setData(uri);
				startActivity(Intent.createChooser(send, "E-mail the app author!"));
				return true;
			} catch (Exception e) {  // For example, android.content.ActivityNotFoundException
				Log.e(TAG, e.toString());
				return false;
			}
		} else if (command.equals("sharejpeg")) {
			try {
				Intent share = new Intent(Intent.ACTION_SEND);
				share.setType("image/jpeg");
				share.putExtra(Intent.EXTRA_STREAM, Uri.parse("file://" + params));
				startActivity(Intent.createChooser(share, "Share Picture"));
				return true;
			} catch (Exception e) {  // For example, android.content.ActivityNotFoundException
				Log.e(TAG, e.toString());
				return false;
			}
		} else if (command.equals("sharetext")) {
			try {
				Intent sendIntent = new Intent();
				sendIntent.setType("text/plain");
				sendIntent.putExtra(Intent.EXTRA_TEXT, params);
				sendIntent.setAction(Intent.ACTION_SEND);
				startActivity(sendIntent);
				return true;
			} catch (Exception e) {  // For example, android.content.ActivityNotFoundException
				Log.e(TAG, e.toString());
				return false;
			}
		} else if (command.equals("showTwitter")) {
			try {
				String twitter_user_name = params;
				try {
					startActivity(new Intent(Intent.ACTION_VIEW,
							Uri.parse("twitter://user?screen_name="
									+ twitter_user_name)));
				} catch (Exception e) {
					startActivity(new Intent(
							Intent.ACTION_VIEW,
							Uri.parse("https://twitter.com/#!/" + twitter_user_name)));
				}
				return true;
			} catch (Exception e) {  // For example, android.content.ActivityNotFoundException
				Log.e(TAG, e.toString());
				return false;
			}
		} else if (command.equals("launchMarket")) {
			// Don't need this, can just use launchBrowser with a market:
			// http://stackoverflow.com/questions/3442366/android-link-to-market-from-inside-another-app
			// http://developer.android.com/guide/publishing/publishing.html#marketintent
			return false;
		} else if (command.equals("toast")) {
			Toast toast = Toast.makeText(this, params, Toast.LENGTH_SHORT);
			toast.show();
			Log.i(TAG, params);
			return true;
		} else if (command.equals("inputbox")) {
			String title = "Input";
			String defString = "";
			String[] param = params.split(":");
			if (param[0].length() > 0)
				title = param[0];
			if (param.length > 1)
				defString = param[1];
			Log.i(TAG, "Launching inputbox: " + title + " " + defString);
			inputBox(title, defString, "OK");
			return true;
		} else if (command.equals("vibrate")) {
			/*
			int milliseconds = -1;
			if (params != "") {
				try {
					milliseconds = Integer.parseInt(params);
				} catch (NumberFormatException e) {
				}
			}
			// Special parameters to perform standard haptic feedback
			// operations
			// -1 = Standard keyboard press feedback
			// -2 = Virtual key press
			// -3 = Long press feedback
			// Note that these three do not require the VIBRATE Android
			// permission.
			switch (milliseconds) {
			case -1:
				mGLSurfaceView.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
				break;
			case -2:
				mGLSurfaceView.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
				break;
			case -3:
				mGLSurfaceView.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
				break;
			default:
				if (vibrator != null) {
					vibrator.vibrate(milliseconds);
				}
				break;
			}
			*/
			return true;
		} else if (command.equals("finish")) {
			finish();
		} else if (command.equals("rotate")) {
			if (Build.VERSION.SDK_INT >= 9) {
				updateScreenRotation();
			}	
		} else if (command.equals("immersive")) {
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
				updateSystemUiVisibility();
			}
		} else if (command.equals("recreate")) {
			recreate();
		}
    	return false;
    }

    @SuppressLint("NewApi")
	@Override
    public void recreate()
    {
        if (android.os.Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB)
        {
            super.recreate();
        }
        else
        {
            startActivity(getIntent());
            finish();
        }
    }
}