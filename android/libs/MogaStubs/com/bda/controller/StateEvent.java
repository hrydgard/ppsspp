package com.bda.controller;

public class StateEvent {
	public static final int STATE_POWER_LOW = 0;
	public static final int STATE_CONNECTION = 1;

	public static final int ACTION_FALSE = 0;
	public static final int ACTION_TRUE = 1;
	public static final int ACTION_DISCONNECTED = 2;
	public static final int ACTION_CONNECTING = 3;
	public static final int ACTION_CONNECTED = 4;

	public int getAction() {
		return 0;
	}
	public int getState() {
		return 0;
	}
}
