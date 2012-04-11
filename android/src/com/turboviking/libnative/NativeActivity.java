package com.turboviking.libnative;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
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
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.Display;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Toast;

class NativeRenderer implements GLSurfaceView.Renderer {
	private static String TAG = "RollerballRenderer";
	NativeActivity mActivity;
	
	NativeRenderer(NativeActivity act) {
		mActivity = act;
	}
	
	@Override
	public void onSurfaceCreated(GL10 unused, EGLConfig config) {
		Log.i(TAG, "onSurfaceCreated");
		displayInit();
	}
	
	@Override
	public void onDrawFrame(GL10 unused /*use GLES20*/) {
        displayRender();
	}
	
	@Override
	public void onSurfaceChanged(GL10 unused, int width, int height) {
		Log.i(TAG, "onSurfaceChanged");
		displayResize(width, height);
	}
	
	
	// NATIVE METHODS

	public native void displayInit();
	// Note: This also means "device lost" and you should reload
	// all buffered objects. 
	public native void displayResize(int w, int h);
	public native void displayRender();
	
	// called by the C++ code through JNI. Dispatch anything we can't directly handle
	// on the gfx thread to the UI thread.
	public void postCommand(String command, String parameter) {
		final String cmd = command;
		final String param = parameter;
		mActivity.runOnUiThread(new Runnable() {
			@Override
			public void run() {
				NativeRenderer.this.mActivity.processCommand(cmd, param);
			}
		});
	}
}

// Touch- and sensor-enabled GLSurfaceView.
class NativeGLView extends GLSurfaceView implements SensorEventListener {
	private static String TAG = "NativeGLView";
	private SensorManager mSensorManager;
	private Sensor mAccelerometer;
	
	public NativeGLView(NativeActivity activity) {
		super(activity);
        setEGLContextClientVersion(2);
        // setEGLConfigChooser(5, 5, 5, 0, 16, 0);
        // setDebugFlags(DEBUG_CHECK_GL_ERROR | DEBUG_LOG_GL_CALLS);
		mSensorManager = (SensorManager)activity.getSystemService(Activity.SENSOR_SERVICE);
		mAccelerometer = mSensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
	}

	// This needs fleshing out. A lot.
	// Going to want multitouch eventually.
	public boolean onTouchEvent(final MotionEvent event) {
		int code = 0;
		if (event.getAction() == MotionEvent.ACTION_DOWN) {
			code = 1;
		} else if (event.getAction() == MotionEvent.ACTION_UP) {
			code = 2;     
		} else if (event.getAction() == MotionEvent.ACTION_MOVE) {	
			code = 3;
		} else {
			return true;
		}
		NativeApp.touch((int)event.getRawX(), (int)event.getRawY(), code);
		return true;
	}

	// Sensor management
	@Override
	public void onAccuracyChanged(Sensor sensor, int arg1) {
		Log.i(TAG, "onAccuracyChanged");
	}

	@Override
	public void onSensorChanged(SensorEvent event) {
		if (event.sensor.getType() != Sensor.TYPE_ACCELEROMETER) {
			return;
		}
		// Can also look at event.timestamp for accuracy magic
		NativeApp.accelerometer(event.values[0], event.values[1], event.values[2]);
	}
	
	@Override
	public void onPause() {
		super.onPause();
		mSensorManager.unregisterListener(this);
	}
	 
	@Override
	public void onResume() {
		super.onResume();
		mSensorManager.registerListener(this, mAccelerometer, SensorManager.SENSOR_DELAY_GAME);
	}
}


class NativeAudioPlayer {
	private String TAG = "NativeAudioPlayer";
	private Thread thread;
	private boolean playing_;

	// Calling stop() is allowed at any time, whether stopped or not.
	// If playing, blocks until not.
	public synchronized void stop() {
		if (thread != null) {
			waitUntilDone();
		} else {
			Log.e(TAG, "Was already stopped");
		}
	}
	
	// If not playing, make sure we're playing.
	public synchronized void play() {
		if (thread == null) {
			playStreaming();
		} else {
			Log.e(TAG, "Was already playing");
		}
	} 

	private void playStreaming() {
		playing_ = true;
		thread = new Thread(new Runnable() {
			public void run() {
				playThread();
			}
		});
		thread.start();
	}
	
	private void waitUntilDone() {
		if (!playing_) {
			Log.i(TAG, "not playing.");
		}
		try {
			playing_ = false;
			Log.e(TAG, "waitUntilDone: Joining audio thread.");
			thread.join();
		} catch (InterruptedException e) {
			Log.e(TAG, "INterrupted!");
			e.printStackTrace();
		}
		thread = null;
		Log.i(TAG, "Finished waitUntilDone");
	}

	private void playThread() {
		try {
			Log.i(TAG, "Thread started.");
			// Get the smallest possible buffer size (which is annoyingly huge).
			int buffer_size_bytes = AudioTrack.getMinBufferSize(
				44100,
				AudioFormat.CHANNEL_CONFIGURATION_STEREO,
				AudioFormat.ENCODING_PCM_16BIT);
			
			// Round buffer_size_bytes up to an even multiple of 128 for convenience.
			buffer_size_bytes = (buffer_size_bytes + 127) & ~127;

			AudioTrack audioTrack = new AudioTrack(AudioManager.STREAM_MUSIC,
					44100,
					AudioFormat.CHANNEL_CONFIGURATION_STEREO,
					AudioFormat.ENCODING_PCM_16BIT,
					buffer_size_bytes,
					AudioTrack.MODE_STREAM);
			
			int buffer_size = buffer_size_bytes / 2;			
			short [] buffer = new short[buffer_size];
			audioTrack.play();
			Log.i(TAG, "Playing... minBuffersize = " + buffer_size);
			while (playing_) {
				NativeApp.audioRender(buffer);
				audioTrack.write(buffer, 0, buffer_size);
			}
			audioTrack.stop();
			audioTrack.release();
			Log.i(TAG, "Stopped playing.");
		} catch (Throwable t) {
			Log.e(TAG, "Playback Failed");
			t.printStackTrace();
			Log.e(TAG, t.toString());
		}	
	}
}

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
	String packageName = "com.turboviking.rollerball";
   
	// Graphics and audio interfaces
	private GLSurfaceView mGLSurfaceView;
	private NativeAudioPlayer audioPlayer;
	
	
	public static String runCommand;
	public static String commandParameter;
	
	public static String installID;
	
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
		try {
		    appInfo = packMgmr.getApplicationInfo(packageName, 0);
	    } catch  (NameNotFoundException e) {
		    e.printStackTrace();
		    throw new RuntimeException("Unable to locate assets, aborting...");
	    }
	    File sdcard = Environment.getExternalStorageDirectory();
        Display display = ((WindowManager)this.getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();
        int scrPixelFormat = display.getPixelFormat();
        int scrWidth = display.getWidth(); 
        int scrHeight = display.getHeight();
        float scrRefreshRate = (float)display.getRefreshRate();
	    String externalStorageDir = sdcard.getAbsolutePath(); 
	    String dataDir = this.getFilesDir().getAbsolutePath();
		String apkFilePath = appInfo.sourceDir; 
		NativeApp.init(scrWidth, scrHeight, apkFilePath, dataDir, externalStorageDir, installID);
     
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
    	} else {
    		Log.e(TAG, "Unsupported command " + command + " , param: " + params);
    	}
    }
}
