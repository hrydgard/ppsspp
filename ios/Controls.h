#pragma once

#include <map>

#import <GameController/GameController.h>
#include "iCade/iCadeState.h"
#include "Common/Input/InputState.h"

// Code extracted from ViewController.mm, in order to modularize
// and share it between multiple view controllers.

bool SetupController(GCController *controller);

struct TouchTracker {
public:
	void Began(NSSet *touches, UIView *view);
	void Moved(NSSet *touches, UIView *view);
	void Ended(NSSet *touches, UIView *view);
	void Cancelled(NSSet *touches, UIView *view);
private:
	void SendTouchEvent(float x, float y, int code, int pointerId);
	int ToTouchID(UITouch *uiTouch, bool allowAllocate);
	UITouch *touches_[10]{};
};

// Can probably get rid of this, but let's keep it for now.
struct ICadeTracker {
public:
	void ButtonDown(iCadeState button);
	void ButtonUp(iCadeState button);
	void InitKeyMap();
private:
	bool simulateAnalog = false;
	bool iCadeConnectNotified = false;

	std::map<uint16_t, InputKeyCode> iCadeToKeyMap;

	double lastSelectPress = 0.0f;
	double lastStartPress = 0.0f;
};
