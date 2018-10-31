package com.bda.controller;

public interface ControllerListener {
	public abstract void onKeyEvent(KeyEvent event);
	
	public abstract void onMotionEvent(MotionEvent event);
	
	public abstract void onStateEvent(StateEvent state);
}
