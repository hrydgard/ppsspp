package org.ppsspp.ppsspp;

import com.henrikrydgard.libnative.NativeActivity;

public class PpssppActivity extends NativeActivity {
	static { 
		System.loadLibrary("ppsspp_jni"); 
	}

	public PpssppActivity() {
		super();
	} 
	
	@Override
	public boolean overrideKeys()
	{   
		return false;
	}  
}