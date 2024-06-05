#pragma once

#include <map>
#include <string_view>

#import <GameController/GameController.h>
#import <CoreMotion/CoreMotion.h>

#include "iCade/iCadeState.h"
#include "Common/Input/InputState.h"

// Code extracted from ViewController.mm, in order to modularize
// and share it between multiple view controllers.

bool InitController(GCController *controller);
void ShutdownController(GCController *controller);

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

void ProcessAccelerometerData(CMAccelerometerData *accData);
InputKeyCode HIDUsageToInputKeyCode(UIKeyboardHIDUsage usage);

void KeyboardPressesBegan(NSSet<UIPress *> *presses, UIPressesEvent *event);
void KeyboardPressesEnded(NSSet<UIPress *> *presses, UIPressesEvent *event);
void SendKeyboardChars(std::string_view str);